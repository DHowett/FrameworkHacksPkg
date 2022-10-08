#include <Library/CrosECLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiShellLib/UefiShellLib.h>

#include "EC.h"
#include "FWUpdate.h"
#include "Flash.h"

EFI_STATUS CheckReadyForECFlash() {
	UINT8 ecid[2];
	ECReadMemoryLPC(EC_MEMMAP_ID, ecid, 2);
	if(ecid[0] != 'E' || ecid[1] != 'C') {
		Print(L"This machine doesn't look like it has an EC\n");
		return EFI_INVALID_PARAMETER;
	}

	UINT32 batteryLfcc, batteryCap;
	UINT8 batteryFlag;
	ECReadMemoryLPC(EC_MEMMAP_BATT_FLAG, &batteryFlag, sizeof(batteryFlag));
	ECReadMemoryLPC(EC_MEMMAP_BATT_LFCC, &batteryLfcc, sizeof(batteryLfcc));
	ECReadMemoryLPC(EC_MEMMAP_BATT_CAP, &batteryCap, sizeof(batteryCap));

	if(0 == (batteryFlag & EC_BATT_FLAG_AC_PRESENT)
			|| (
				(batteryFlag & EC_BATT_FLAG_BATT_PRESENT)
				&& ((100ULL * batteryCap) / batteryLfcc) < 20)
			) {
		Print(L"Make sure AC is connected and that if there is a battery present it is at least 20%% charged.\n");
		return EFI_NOT_READY;
	}

	return EFI_SUCCESS;
}

#define FLASH_BASE    0x0  // 0x80000
#define FLASH_RO_BASE 0x0
#define FLASH_RO_SIZE 0x3C000
#define FLASH_RW_BASE 0x40000
#define FLASH_RW_SIZE 0x39000

// This accounts for the MCHP header and the LFW reset vectors
#define FLASH_MCHP_VERSION_OFFSET 0x2434

EFI_STATUS cmd_reflash(int argc, CHAR16** argv) {
	EFI_STATUS Status = EFI_SUCCESS;
	SHELL_FILE_HANDLE FirmwareFile = NULL;
	EFI_FILE_INFO* FileInfo = NULL;
	EFI_INPUT_KEY Key = {0};
	int rv = 0;
	char* FirmwareBuffer = NULL;
	char* VerifyBuffer = NULL;
	UINTN ReadSize = 0;
	struct ec_params_flash_notified FlashNotifyParams = {0};
	struct ec_response_get_version VersionResponse = {0};
	char* CurrentBoardId = NULL;
	char* FirmwareBoardId = NULL;
	int FirmwareBoardIdLength = 0;
	int force = 0;
	BOOLEAN flash_ro = TRUE, flash_rw = TRUE;
	CHAR16* filename = NULL;

	for(int i = 1; i < argc; ++i) {
		if(StrCmp(argv[i], L"-f") == 0)
			++force;
		else if(StrCmp(argv[i], L"--ro") == 0)
			flash_rw = FALSE;
		else if(StrCmp(argv[i], L"--rw") == 0)
			flash_ro = FALSE;
		else {
			filename = argv[i];
			// the filename is the last argument. All stop!
			break;
		}
	}

	if(!filename) {
		Print(L"Usage: ectool reflash [options] FILE\n"
		      L"\n"
		      L"Attempts to safely reflash the Framework Laptop's EC\n"
		      L"Preserves flash region 3C000-3FFFF and 79000-7FFFF.\n"
		      L"\n"
		      L"Options:\n"
		      L"    --ro        Only reflash the RO portion (and bootloader)\n"
		      L"    --rw        Only reflash the RW portion\n"
		      L"    -f          Force: Skip the battery and AC check\n"
		      );
		return 1;
	}

	Status = force > 0 ? EFI_SUCCESS : CheckReadyForECFlash();
	if(EFI_ERROR(Status)) {
		Print(L"System not ready\n");
		goto Out;
	}

	Status = ShellOpenFileByName(filename, &FirmwareFile, EFI_FILE_MODE_READ, 0);
	if(EFI_ERROR(Status)) {
		Print(L"Failed to open `%s': %r\n", filename, Status);
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

	rv = ECSendCommandLPCv3(EC_CMD_GET_VERSION, 0, NULL, 0, &VersionResponse, sizeof(VersionResponse));
	if(rv < 0)
		goto EcOut;

	CurrentBoardId = &VersionResponse.version_string_ro[0];
	if(!*CurrentBoardId) // The hx20 EC will by default only return the current active version; compensate
		CurrentBoardId = &VersionResponse.version_string_rw[0];
	// Versions are of the format board_vX.Y.Z; truncate at the _ to get the board
	for(char* end = CurrentBoardId; end < (CurrentBoardId + 32); ++end) {
		if (*end == '_') {
			*end = '\0';
			break;
		}
	}

	FirmwareBoardId = FirmwareBuffer + FLASH_MCHP_VERSION_OFFSET;
	for(; FirmwareBoardId[FirmwareBoardIdLength] != '_' && FirmwareBoardIdLength < 32; ++FirmwareBoardIdLength)
		;
	if(force < 2 && 0 != AsciiStrnCmp(CurrentBoardId, FirmwareBoardId, FirmwareBoardIdLength)) {
		// We're about to abort the process, so we can edit the board ID in the
		// firmware image we loaded into memory directly to null-terminate it.
		FirmwareBoardId[FirmwareBoardIdLength] = '\0';
		Print(L"*** BOARD MISMATCH ***\n");
		Print(L"Current machine: %a\n", CurrentBoardId);
		Print(L"Firmware image : %a\n", FirmwareBoardId);
		Status = EFI_ABORTED;
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

	Status = force > 0 ? EFI_SUCCESS : CheckReadyForECFlash();
	if(EFI_ERROR(Status)) {
		Print(L"System not ready\n");
		goto Out;
	}

	Print(L"************************************************\n");
	Print(L"*** DO NOT UNPLUG OR POWER OFF YOUR COMPUTER ***\n");
	Print(L"************************************************\n\n");
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

	if (flash_rw == TRUE) {
		Print(L"RW: Erasing... ");
		rv = flash_erase(FLASH_BASE + FLASH_RW_BASE, FLASH_RW_SIZE);
		if(rv < 0)
			goto EcOut;
		Print(L"OK. ");

		Print(L"Writing... ");
		rv = flash_write(FLASH_BASE + FLASH_RW_BASE, FLASH_RW_SIZE, FirmwareBuffer + FLASH_RW_BASE);
		if(rv < 0)
			goto EcOut;
		Print(L"OK. ");

		Print(L"Verifying: Read... ");
		rv = flash_read(FLASH_BASE + FLASH_RW_BASE, FLASH_RW_SIZE, VerifyBuffer + FLASH_RW_BASE);
		if(rv < 0)
			goto EcOut;

		Print(L"OK. Check... ");

		if(CompareMem(VerifyBuffer + FLASH_RW_BASE, FirmwareBuffer + FLASH_RW_BASE, FLASH_RW_SIZE) == 0) {
			Print(L"OK!\n");
		} else {
			Print(L"FAILED!\n");
			Print(L"*** RW FAILED VERIFICATION! NOT PROCEEDING TO RO ***\n");
			// Bailing out after RW but before RO stands a chance of preserving the system
			rv = -1;
			goto ErrorOut;
		}
	}

	if (flash_ro == TRUE) {
		Print(L"RO: Erasing... ");
		rv = flash_erase(FLASH_BASE + FLASH_RO_BASE, FLASH_RO_SIZE);
		if(rv < 0)
			goto EcOut;
		Print(L"OK. ");

		Print(L"Writing... ");
		rv = flash_write(FLASH_BASE + FLASH_RO_BASE, FLASH_RO_SIZE, FirmwareBuffer + FLASH_RO_BASE);
		if(rv < 0)
			goto EcOut;
		Print(L"OK. ");

		Print(L"Verifying: Read... ");

		rv = flash_read(FLASH_BASE + FLASH_RO_BASE, FLASH_RO_SIZE, VerifyBuffer + FLASH_RO_BASE);
		if(rv < 0)
			goto EcOut;

		Print(L"OK. Check... ");

		if(CompareMem(VerifyBuffer + FLASH_RO_BASE, FirmwareBuffer + FLASH_RO_BASE, FLASH_RO_SIZE) == 0) {
			Print(L"OK!\n");
		} else {
			Print(L"FAILED!\n");
		}
	}

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
	}
ErrorOut:
	if(rv < 0) {
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
