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

static const CHAR16* mEcErrorMessages[] = {
	L"success",
	L"invalid command",
	L"error",
	L"invalid param",
	L"access denied",
	L"invalid response",
	L"invalid version",
	L"invalid checksum",
	L"in progress",
	L"unavailable",
	L"timeout",
	L"overflow",
	L"invalid header",
	L"request truncated",
	L"response too big",
	L"bus error",
	L"busy",
	L"invalid header version",
	L"invalid header crc",
	L"invalid data crc",
	L"dup unavailable",
};

static void PrintECResponse(int rv) {
	if(rv >= 0)
		return;
	if(rv < -EECRESULT)
		rv += EECRESULT;
	Print(L"%d (%s)", -rv, rv >= -EC_RES_DUP_UNAVAILABLE ? mEcErrorMessages[-rv] : L"<unknown error>");
}

/// Framework Specific
/* Configure the behavior of the flash notify */
#define EC_CMD_FLASH_NOTIFIED 0x3E01

enum ec_flash_notified_flags {
	/* Enable/Disable power button pulses for x86 devices */
	FLASH_ACCESS_SPI = 0,
	FLASH_FIRMWARE_START = BIT(0),
	FLASH_FIRMWARE_DONE = BIT(1),
	FLASH_ACCESS_SPI_DONE = 3,
	FLASH_FLAG_PD = BIT(4),
};

struct ec_params_flash_notified {
	/* See enum ec_flash_notified_flags */
	uint8_t flags;
} __ec_align1;
/// End Framework Specific

#define G_EC_MAX_REQUEST  (EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_request))
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
		Print(L"Error: ");
		PrintECResponse(rv);
		Print(L"\n");
		return EFI_UNSUPPORTED;
	}
	AsciiPrint("%a\n", buf);
	return 0;
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

#define FLASH_BASE    0x0  // 0x80000
#define FLASH_RO_BASE 0x0
#define FLASH_RO_SIZE 0x3C000
#define FLASH_RW_BASE 0x40000
#define FLASH_RW_SIZE 0x39000
EFI_STATUS cmd_fwup2(int argc, CHAR16** argv) {
	EFI_STATUS Status = EFI_SUCCESS;
	SHELL_FILE_HANDLE FirmwareFile = NULL;
	EFI_FILE_INFO* FileInfo = NULL;
	EFI_INPUT_KEY Key = {0};
	int rv = 0;
	char* FirmwareBuffer = NULL;
	char* VerifyBuffer = NULL;
	UINTN ReadSize = 0;
	struct ec_params_flash_notified FlashNotifyParams = {0};

	if(argc < 2) {
		Print(L"ectool reflash FILE\n\nAttempts to safely reflash the Framework Laptop's EC\nPreserves flash "
		      L"region 3C000-3FFFF and 79000-7FFFF.\n");
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

	FirmwareBuffer = (char*)AllocatePool(FileInfo->FileSize);
	VerifyBuffer = (char*)AllocatePool(FileInfo->FileSize);
	if(!FirmwareBuffer || !VerifyBuffer) {
		Print(L"Failed to allocate an arena for the firmware image or verification buffer\n");
		Status = EFI_NOT_READY;
		goto Out;
	}

	ReadSize = FileInfo->FileSize;
	Status = ShellReadFile(FirmwareFile, &ReadSize, FirmwareBuffer);
	if(EFI_ERROR(Status)) {
		Print(L"Failed to read firmware: %r\n", Status);
		goto Out;
	}

	if(ReadSize != FileInfo->FileSize) {
		Print(L"Failed to read entire firmware image into memory.\n");
		Status = EFI_END_OF_FILE;
		goto Out;
	}

	Print(L"*** STARTING FLASH (PRESS ANY KEY TO CANCEL)\n");
	Key.ScanCode = SCAN_NULL;
	for(int i = 7; i > 0; i--) {
		Print(L"%d...", i);
		gBS->Stall(1000000);
		EFI_STATUS KeyStatus = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
		if(!EFI_ERROR(KeyStatus)) {
			Print(L"\nABORTED!\n");
			return EFI_ABORTED;
		}
	}
	Print(L"\n");

	Status = CheckReadyForECFlash();
	if(EFI_ERROR(Status)) {
		Print(L"System not ready\n");
		goto Out;
	}

	Print(L"Unlocking flash... ");
	FlashNotifyParams.flags = FLASH_ACCESS_SPI;
	rv = ECSendCommandLPCv3(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
	if(rv < 0)
		goto EcOut;
	FlashNotifyParams.flags = FLASH_FIRMWARE_START;
	rv = ECSendCommandLPCv3(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
	if(rv < 0)
		goto EcOut;
	Print(L"OK\n");

	Print(L"Erasing RO region... ");
	rv = flash_erase(FLASH_BASE + FLASH_RO_BASE, FLASH_RO_SIZE);
	if(rv < 0)
		goto EcOut;
	Print(L"OK\n");

	Print(L"Erasing RW region... ");
	rv = flash_erase(FLASH_BASE + FLASH_RW_BASE, FLASH_RW_SIZE);
	if(rv < 0)
		goto EcOut;
	Print(L"OK\n");

	Print(L"Writing RO region... ");
	rv = flash_write(FLASH_BASE + FLASH_RO_BASE, FLASH_RO_SIZE, FirmwareBuffer + FLASH_RO_BASE);
	if(rv < 0)
		goto EcOut;
	Print(L"OK\n");

	Print(L"Writing RW region... ");
	rv = flash_write(FLASH_BASE + FLASH_RW_BASE, FLASH_RW_SIZE, FirmwareBuffer + FLASH_RW_BASE);
	if(rv < 0)
		goto EcOut;
	Print(L"OK\n");

	Print(L"Verifying: Read... ");

	rv = flash_read(FLASH_BASE + FLASH_RO_BASE, FLASH_RO_SIZE, VerifyBuffer + FLASH_RO_BASE);
	if(rv < 0)
		goto EcOut;
	rv = flash_read(FLASH_BASE + FLASH_RW_BASE, FLASH_RW_SIZE, VerifyBuffer + FLASH_RW_BASE);
	if(rv < 0)
		goto EcOut;
	Print(L"OK. Check... ");

	if(CompareMem(VerifyBuffer + FLASH_RO_BASE, FirmwareBuffer + FLASH_RO_BASE, FLASH_RO_SIZE) == 0) {
		Print(L"RO OK... ");
	} else {
		Print(L"RO FAIL! ");
	}
	if(CompareMem(VerifyBuffer + FLASH_RW_BASE, FirmwareBuffer + FLASH_RW_BASE, FLASH_RW_SIZE) == 0) {
		Print(L"RW OK... ");
	} else {
		Print(L"RW FAIL! ");
	}
	Print(L"OK\n");

	Print(L"Locking flash... ");
	FlashNotifyParams.flags = FLASH_ACCESS_SPI_DONE;
	rv = ECSendCommandLPCv3(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
	if(rv < 0)
		goto EcOut;
	FlashNotifyParams.flags = FLASH_FIRMWARE_DONE;
	rv = ECSendCommandLPCv3(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
	if(rv < 0)
		goto EcOut;
	Print(L"OK\n");

	Print(L"\nLooks like it worked?\nConsider running `ectool reboot` to reset the EC/AP.\n");

EcOut:
	if(rv < 0) {
		PrintECResponse(rv);
		Print(L"\n");
		Print(L"*** YOUR COMPUTER MAY NO LONGER BOOT ***\n");
		Status = EFI_DEVICE_ERROR;
	}
Out:
	if(FirmwareFile) {
		ShellCloseFile(&FirmwareFile);
	}
	SHELL_FREE_NON_NULL(FileInfo);
	SHELL_FREE_NON_NULL(FirmwareBuffer);
	SHELL_FREE_NON_NULL(VerifyBuffer);
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

	rv = ECSendCommandLPCv3(EC_CMD_REBOOT_EC, 0, &p, sizeof(p), NULL, 0);
	// UNREACHABLE ON SUCCESS ON THE FRAMEWORK LAPTOP
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
	{L"reflash", cmd_fwup2},
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
