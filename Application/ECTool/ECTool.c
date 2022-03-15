#include <Library/BaseMemoryLib.h>
#include <Library/CrosECLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/Shell.h>
#include <Protocol/ShellParameters.h>


UINTN Argc;
CHAR16** Argv;
EFI_SHELL_PROTOCOL* mShellProtocol = NULL;

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

EFI_STATUS DumpFlash() {
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

	char* FlashBuffer = AllocatePool(1048576);
	int i = 0;
	const int maxsize = EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_response);
	struct ec_params_flash_read r;
	while(i < 1048576) {
		Print(L".");
		r.offset = i;
		r.size = MIN(maxsize, 1048576 - i);
		rv = ECSendCommandLPCv3(EC_CMD_FLASH_READ, 0, &r, sizeof(r), FlashBuffer + i, r.size);
		if(rv < 0) {
			Print(L"\nEC Error reading offset %u: %d\n", i, rv);
			break;
		}
		i += r.size;
	}
	Print(L"\n");

	// re-lock flash
	flashNotify = 2;  // Flash done
	ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	flashNotify = 3;  // SPI access done
	int lrv = ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	if(lrv < 0) {
		Print(L"Failed to re-lock flash: %d\n", lrv);
	}

	if(rv >= 0) {
		mShellProtocol->CreateFile(L"fs0:\\flash.bin", 0, &File);
		UINTN BufSz = 1048576;
		mShellProtocol->WriteFile(File, &BufSz, FlashBuffer);
		mShellProtocol->CloseFile(File);
		Print(L"Dumped to fs0:\\flash.bin\n");
	}

	FreePool(FlashBuffer);
	return EFI_SUCCESS;
}

EFI_STATUS cmd_version(int argc, CHAR16** argv) {
#define EC_CMD_GET_BUILD_INFO 0x0004
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
	return DumpFlash();
}

typedef EFI_STATUS (*command_handler)(int argc, CHAR16** argv);

struct comspec {
	CHAR16* name;
	command_handler handler;
};

static struct comspec commands[] = {
	{L"version", cmd_version},
	{L"flashread", cmd_flashread},
};

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_SUCCESS;

	GetArg();

	Status = gBS->LocateProtocol(&gEfiShellProtocolGuid, NULL, (VOID**)&mShellProtocol);
	if(EFI_ERROR(Status)) {
		Print(L"Could not locate shell protocol...\n");
		return Status;
	}

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
