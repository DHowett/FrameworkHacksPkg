#include <Library/BaseMemoryLib.h>
#include <Library/CrosECLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiShellLib/UefiShellLib.h>

#include <Protocol/Shell.h>
#include <Protocol/ShellParameters.h>

#include "EC.h"
#include "FWUpdate.h"
#include "Flash.h"

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

EFI_STATUS cmd_version(int argc, CHAR16** argv) {
	EFI_STATUS Status = EFI_SUCCESS;
	struct ec_response_get_version r;
	struct ec_params_flash_notified FlashNotifyParams = {0};
	char buf[248] = {0};
	int rv = 0;
	int unlocked = 0;

	rv = ECSendCommandLPCv3(EC_CMD_GET_BUILD_INFO, 0, NULL, 0, buf, 248);
	if(rv < 0)
		goto EcOut;

	// It's necessary to unlock SPI to read the RW version
	FlashNotifyParams.flags = FLASH_ACCESS_SPI;
	rv = ECSendCommandLPCv3(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
	if(rv < 0)
		goto EcOut;

	unlocked = 1;

	rv = ECSendCommandLPCv3(EC_CMD_GET_VERSION, 0, NULL, 0, &r, sizeof(r));
	if(rv < 0)
		goto EcOut;

	Print(L"RO Version: %a\nRW Version: %a\nCurrent Image: %s\nBuild Info: %a\n", r.version_string_ro,
	      r.version_string_rw,
	      r.current_image == EC_IMAGE_RW   ? L"RW"
	      : r.current_image == EC_IMAGE_RO ? L"RO"
	                                       : L"<unknown>",
	      buf);

EcOut:
	if(rv < 0) {
		PrintECResponse(rv);
		Print(L"\n");
		Status = EFI_DEVICE_ERROR;
	}

	if(unlocked) {
		// last ditch effort: tell the EC we're done with SPI
		FlashNotifyParams.flags = FLASH_ACCESS_SPI_DONE;
		ECSendCommandLPCv3(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
		// discard the response; it won't help now
		unlocked = 0;
	}

	return Status;
}

EFI_STATUS cmd_flashread(int argc, CHAR16** argv) {
	UINTN offset, size;
	CHAR16* path = argv[3];
	char* FlashBuffer = NULL;
	EFI_STATUS Status = EFI_SUCCESS;
	SHELL_FILE_HANDLE File = NULL;
	struct ec_params_flash_notified FlashNotifyParams = {0};
	int rv = 0;
	int unlocked = 0;

	if(argc < 4) {
		Print(L"ectool flashread OFFSET SIZE FILE\n");
		Status = EFI_INVALID_PARAMETER;
		goto Out;
	}

	if((offset = ShellStrToUintn(argv[1])) < 0) {
		Print(L"invalid offset\n");
		Status = EFI_INVALID_PARAMETER;
		goto Out;
	}

	if((size = ShellStrToUintn(argv[2])) < 0) {
		Print(L"invalid size\n");
		Status = EFI_INVALID_PARAMETER;
		goto Out;
	}

	FlashBuffer = AllocatePool(size);
	if(!FlashBuffer) {
		Print(L"Failed to allocate flash read buffer.\n");
		Status = EFI_NOT_READY;
		goto Out;
	}

	Status = ShellOpenFileByName(path, &File, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
	if(EFI_ERROR(Status)) {
		File = NULL;
		Print(L"Failed to open `%s': %r\n", path, Status);
		goto Out;
	}

	FlashNotifyParams.flags = FLASH_ACCESS_SPI;
	rv = ECSendCommandLPCv3(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
	if(rv < 0)
		goto EcOut;

	unlocked = 1;

	rv = flash_read(offset, size, FlashBuffer);
	if(rv < 0) {
		Print(L"Failed to read: ");
		goto EcOut;
	}

	Status = ShellWriteFile(File, &size, FlashBuffer);
	if(EFI_ERROR(Status)) {
		Print(L"Failed to write `%s': %r\n", path, Status);
		goto Out;
	}

	Status = ShellCloseFile(&File);
	if(EFI_ERROR(Status)) {
		Print(L"Failed to close `%s': %r\n", path, Status);
		goto Out;
	}
	File = NULL;

	Print(L"Dumped %d bytes to %s\n", size, path);

EcOut:
	if(rv < 0) {
		PrintECResponse(rv);
		Print(L"\n");
		Status = EFI_DEVICE_ERROR;
	}

Out:
	if(unlocked) {
		// last ditch effort: tell the EC we're done with SPI
		FlashNotifyParams.flags = FLASH_ACCESS_SPI_DONE;
		ECSendCommandLPCv3(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
		// discard the response; it won't help now
		unlocked = 0;
	}

	if(File) {
		ShellCloseFile(&File);
	}
	SHELL_FREE_NON_NULL(FlashBuffer);
	return Status;
}

EFI_STATUS cmd_reboot(int argc, CHAR16** argv) {
	EFI_STATUS Status = EFI_SUCCESS;
	struct ec_params_reboot_ec p;
	int rv = 0;

	p.cmd = EC_REBOOT_COLD;
	p.flags = 0;

	if(argc > 1) {
		if(StrCmp(argv[1], L"ro") == 0)
			p.cmd = EC_REBOOT_JUMP_RO;
		else if(StrCmp(argv[1], L"rw") == 0)
			p.cmd = EC_REBOOT_JUMP_RW;
	}

	if(argc > 2) {
		if(StrCmp(argv[2], L"at-shutdown") == 0)
			p.flags |= EC_REBOOT_FLAG_ON_AP_SHUTDOWN;
	}

	rv = ECSendCommandLPCv3(EC_CMD_REBOOT_EC, 0, &p, sizeof(p), NULL, 0);
	// UNREACHABLE ON SUCCESS ON THE FRAMEWORK LAPTOP
	if(rv < 0) {
		PrintECResponse(rv);
		Status = EFI_DEVICE_ERROR;
	}
	return Status;
}

EFI_STATUS cmd_console(int argc, CHAR16** argv) {
	EFI_STATUS Status = EFI_SUCCESS;
	struct ec_params_console_read_v1 p;
	char buf[G_EC_MAX_RESPONSE] = {0};
	int rv = 0;

	rv = ECSendCommandLPCv3(EC_CMD_CONSOLE_SNAPSHOT, 0, NULL, 0, NULL, 0);
	if(rv < 0)
		goto EcOut;

	p.subcmd = CONSOLE_READ_RECENT;
	do {
		rv = ECSendCommandLPCv3(EC_CMD_CONSOLE_READ, 1, &p, sizeof(p), buf, sizeof(buf));
		if(rv < 0)
			goto EcOut;

		if(rv > 0 && buf[0])
			Print(L"%a", buf);

		p.subcmd = CONSOLE_READ_NEXT;
	} while(rv > 0 && buf[0]);

EcOut:
	if(rv < 0) {
		PrintECResponse(rv);
		Status = EFI_DEVICE_ERROR;
	}
	return Status;
}

typedef EFI_STATUS (*command_handler)(int argc, CHAR16** argv);

struct comspec {
	CHAR16* name;
	command_handler handler;
};

static struct comspec commands[] = {
	{L"version", cmd_version},
	{L"reboot", cmd_reboot},
	{L"flashread", cmd_flashread},
	{L"reflash", cmd_reflash},
	{L"console", cmd_console},
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
		goto OutHelp;
	}

	for(int i = 0; i < ARRAY_SIZE(commands); ++i) {
		if(0 == StrCmp(Argv[1], commands[i].name)) {
			return commands[i].handler(Argc - 1, Argv + 1);
		}
	}

OutHelp:
	Print(L"Invocation: ectool <command> args\n\nCommands:\n");
	for(int i = 0; i < ARRAY_SIZE(commands); ++i) {
		Print(L"- %s\n", commands[i].name);
	}
	return 1;
}
