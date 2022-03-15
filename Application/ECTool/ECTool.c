#include <Library/BaseMemoryLib.h>
#include <Library/CrosECLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiShellLib/UefiShellLib.h>

#include <Protocol/Shell.h>
#include <Protocol/ShellParameters.h>

#include "FWUpdate.h"

UINTN Argc;
CHAR16** Argv;
EFI_SHELL_PROTOCOL* mShellProtocol = NULL;

#define G_EC_MAX_REQUEST (EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_request))
#define G_EC_MAX_RESPONSE (EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_response))

enum ec_status flash_read(int offset, int size, char* buffer) {
	struct ec_params_flash_read p;
	const int chunk = G_EC_MAX_RESPONSE;
	char buf[G_EC_MAX_RESPONSE] = {0};
	for(int i = 0; i < size; i += chunk) {
		p.offset = offset + i;
		p.size = MIN(size - i, chunk);
		int rv = ECSendCommandLPCv3(EC_CMD_FLASH_READ, 0, &p, sizeof(p), buf, p.size);
		if(rv < 0) {
			Print(L"*** FLASH READ **FAILED** AT OFFSET %x: %d\n", p.offset, rv);
			return rv;
		}
		CopyMem(buffer + i, buf, p.size);
	}
	return 0;
}

enum ec_status flash_write(int offset, int size, char* buffer) {
	struct ec_response_flash_info_1 flashInfo;
	int rv = ECSendCommandLPCv3(EC_CMD_FLASH_INFO, 1, NULL, 0, &flashInfo, sizeof(flashInfo));
	if(rv < 0) {
		Print(L"Failed to query flash info\n");
		return rv;
	}

	int step = (G_EC_MAX_REQUEST / flashInfo.write_ideal_size) * flashInfo.write_ideal_size;
	char commandBuffer[G_EC_MAX_REQUEST] = {0};
	struct ec_params_flash_write* p = (struct ec_params_flash_write*)&commandBuffer[0];
	for(int i = 0; i < size; i += step) {
		p->offset = offset + i;
		p->size = MIN(size - i, step);
		CopyMem(p + 1, buffer + i, p->size);
		rv = ECSendCommandLPCv3(EC_CMD_FLASH_WRITE, 1, p, sizeof(*p) + p->size, NULL, 0);
		if(rv < 0) {
			Print(L"*** FLASH WRITE **FAILED** AT OFFSET %x: %d\n", p->offset, rv);
			return rv;
		}
	}
	return 0;
}

enum ec_status flash_erase(int offset, int size) {
	struct ec_params_flash_erase p;
	p.offset = offset;
	p.size = size;
	return ECSendCommandLPCv3(EC_CMD_FLASH_ERASE, 0, &p, sizeof(p), NULL, 0);
}

EFI_STATUS
GetArg(VOID) {
	EFI_STATUS Status;
	EFI_SHELL_PARAMETERS_PROTOCOL* ShellParameters;

	Status = gBS->HandleProtocol(gImageHandle, &gEfiShellParametersProtocolGuid, (VOID**)&ShellParameters);
	if(EFI_ERROR(Status)) {
		return Status;
	}

	Argc = ShellParameters->Argc;
	Argv = ShellParameters->Argv;
	return EFI_SUCCESS;
}

EFI_STATUS cmd_version(int argc, CHAR16** argv) {
	char buf[248];
	ZeroMem(buf, 248);
	int rv = ECSendCommandLPCv3(EC_CMD_GET_BUILD_INFO, 0, NULL, 0, buf, 248);
	if(rv < 0) {
		return EFI_UNSUPPORTED;
	}
	AsciiPrint("Build: %a\n", buf);
	return 0;
}

EFI_STATUS cmd_flashread(int argc, CHAR16** argv) {
	if(argc < 4) {
		Print(L"ectool flashread OFFSET SIZE FILE\n");
		return 1;
	}

	UINTN offset, size;
	CHAR16* path = argv[3];

	if((offset = ShellStrToUintn(argv[1])) < 0) {
		Print(L"invalid offset\n");
		return 1;
	}

	if((size = ShellStrToUintn(argv[2])) < 0) {
		Print(L"invalid size\n");
		return 1;
	}

	SHELL_FILE_HANDLE File;
	int rv = 0;

	char flashNotify;
	flashNotify = 1;  // Lock down for flashing
	rv = ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	flashNotify = 0;  // Enable SPI access
	rv = ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	if(rv < 0) {
		Print(L"Failed to unlock flash: %d\n", rv);
		return EFI_UNSUPPORTED;
	}

	char* FlashBuffer = AllocatePool(size);
	rv = flash_read(offset, size, FlashBuffer);
	if(rv < 0) {
		Print(L"Failed to read: %d\n", rv);
		FreePool(FlashBuffer);
		return 1;
	}

	// re-lock flash
	flashNotify = 2;  // Flash done
	ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	flashNotify = 3;  // SPI access done
	int lrv = ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	if(lrv < 0) {
		Print(L"Failed to re-lock flash: %d\n", lrv);
	}

	if(rv >= 0) {
		int s = ShellOpenFileByName(path, &File,
		                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
		if(EFI_ERROR(s)) {
			Print(L"Failed to open `%s': %r\n", path, s);
			return 1;
		}
		ShellWriteFile(File, &size, FlashBuffer);
		ShellCloseFile(&File);
		Print(L"Dumped %d bytes to %s\n", size, path);
	}

	FreePool(FlashBuffer);
	return EFI_SUCCESS;
}

EFI_STATUS cmd_fwup2(int argc, CHAR16** argv) {
	EFI_STATUS Status = EFI_SUCCESS;
	SHELL_FILE_HANDLE FirmwareFile = NULL;
	EFI_FILE_INFO* FileInfo = NULL;

	if(argc < 2) {
		Print(L"ectool fwup2 FILE\n");
		return 1;
	}

	Status = CheckReadyForECFlash();
	if(EFI_ERROR(Status)) {
		Print(L"System not ready\n");
		goto Out;
	}

	Status = ShellOpenFileByName(argv[1], &FirmwareFile, EFI_FILE_MODE_READ, 0);
	if(EFI_ERROR(Status)) {
		Print(L"Failed to open `%s': %r\n", argv[1], Status);
		goto Out;
	}

	FileInfo = ShellGetFileInfo(FirmwareFile);

	if(FileInfo->FileSize != (512 * 1024)) {
		Print(L"Firmware image is %d bytes (expected %d).\n", FileInfo->FileSize, (512 * 1024));
		Status = EFI_UNSUPPORTED;
		goto Out;
	}

	Print(L"*** STARTING FLASH (LAST CHANCE TO CANCEL)\n");
	for(int i = 7; i > 0; i--) {
		Print(L"%d...", i);
		gBS->Stall(1000000);
	}
	Print(L"\n");

Out:
	if(FirmwareFile) {
		ShellCloseFile(&FirmwareFile);
	}
	SHELL_FREE_NON_NULL(FileInfo);
	return Status;
}

EFI_STATUS cmd_fwc(int argc, CHAR16** argv) {
	EFI_STATUS Status = CheckReadyForECFlash();
	Print(L"Readiness: %d\n", Status);
	return Status;
}

typedef EFI_STATUS (*command_handler)(int argc, CHAR16** argv);

struct comspec {
	CHAR16* name;
	command_handler handler;
};

static struct comspec commands[] = {
	{L"version", cmd_version},
	{L"flashread", cmd_flashread},
	{L"fwup2", cmd_fwup2},
	//{L"str", cmd_str},
	{L"fwc", cmd_fwc},
};

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_SUCCESS;

	Status = ShellInitialize();
	if(EFI_ERROR(Status)) {
		Print(L"Failed to initialize shell lib: %x\n", Status);
		return Status;
	}

	GetArg();

	if(Argc < 2) {
		Print(L"Invocation: ectool <command> args\n\nCommands:\n");
		for(int i = 0; i < ARRAY_SIZE(commands); ++i) {
			Print(L"- %s\n", commands[i].name);
		}
		return 1;
	}

	for(int i = 0; i < ARRAY_SIZE(commands); ++i) {
		if(0 == StrCmp(Argv[1], commands[i].name)) {
			return commands[i].handler(Argc - 1, Argv + 1);
		}
	}

	return Status;
}
