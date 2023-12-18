#include <Library/CrosECLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiShellLib/UefiShellLib.h>
#include <Library/FmapLib.h>
#include <Protocol/CrosEC.h>

#include "EC.h"
#include "FWUpdate.h"
#include "Flash.h"

extern EFI_CROSEC_PROTOCOL* gECProtocol;

EFI_STATUS CheckReadyForECFlash() {
	UINT8 ecid[2];
	gECProtocol->ReadMemory(EC_MEMMAP_ID, ecid, 2);
	if(ecid[0] != 'E' || ecid[1] != 'C') {
		Print(L"This machine doesn't look like it has an EC\n");
		return EFI_INVALID_PARAMETER;
	}

	UINT32 batteryLfcc, batteryCap;
	UINT8 batteryFlag;
	gECProtocol->ReadMemory(EC_MEMMAP_BATT_FLAG, &batteryFlag, sizeof(batteryFlag));
	gECProtocol->ReadMemory(EC_MEMMAP_BATT_LFCC, &batteryLfcc, sizeof(batteryLfcc));
	gECProtocol->ReadMemory(EC_MEMMAP_BATT_CAP, &batteryCap, sizeof(batteryCap));

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

#define FLASH_REGION_BOOTBLOCK 0x01
#define FLASH_REGION_SKIP      0x02

#define FLASH_DEVICE_REQUIRES_UNLOCK 0x1

typedef struct _FLASH_REGION {
	const CHAR8* name;
	UINTN base;
	UINTN size;
	UINTN flags;
} FLASH_REGION;

typedef struct _FLASH_MAP {
	const CHAR8* board;
	const FLASH_REGION* regions;
	UINTN flags;
} FLASH_MAP;

const FLASH_REGION hx_flash_regions[] = {
	{ "RO",     0x00000, 0x3C000, FLASH_REGION_BOOTBLOCK },
	{ "RO_VPD", 0x3C000, 0x04000, FLASH_REGION_SKIP },
	{ "RW",     0x40000, 0x39000, 0 },
	{ "RW_VPD", 0x79000, 0x07000, FLASH_REGION_SKIP },
	{ NULL, 0, 0, },
};

const FLASH_REGION azalea_lotus_flash_regions[] = {
	{ "RO",        0x00000, 0x40000, FLASH_REGION_BOOTBLOCK },
	{ "RW",        0x40000, 0x3F000, 0 },
	{ "SPI_FLAGS", 0x7F000, 0x01000, FLASH_REGION_SKIP },
	{ NULL, 0, 0, },
};

/*
 * When we can't tell what board it is, and the user passes -f -f,
 * just assume it is a 50/50 split between RO and RW.
 */
const FLASH_REGION unknown_board_flash_regions[] = {
	{ "RO",        0x00000, 0x40000, FLASH_REGION_BOOTBLOCK },
	{ "RW",        0x40000, 0x40000, 0 },
	{ NULL, 0, 0, },
};
const FLASH_MAP unknown_board_flash_map = {
	"unknown",
	unknown_board_flash_regions,
	FLASH_DEVICE_REQUIRES_UNLOCK,
};

const FLASH_MAP flash_maps[] = {
	{ "hx20", hx_flash_regions, FLASH_DEVICE_REQUIRES_UNLOCK },
	{ "hx30", hx_flash_regions, FLASH_DEVICE_REQUIRES_UNLOCK },
	{ "azalea", azalea_lotus_flash_regions, 0 },
	{ "lotus", azalea_lotus_flash_regions, 0 },
	{ NULL, NULL, 0 },
};

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
	CHAR16* filename = NULL;
	EC_IMAGE_FMAP_HEADER* IncomingImageFlashMap = NULL;
	EC_IMAGE_FMAP_AREA_HEADER* IncomingImageRoFridArea = NULL;
	UINT16 RegionFlashMask = 0;
	BOOLEAN flash_ro_requested = FALSE, flash_rw_requested = FALSE, flash_all_requested = FALSE;
	UINT16 RegionFlashDesireMask = 0xFFFF; // By default, we desire all regions
	const FLASH_MAP* FinalFlashMap = NULL;

	for(int i = 1; i < argc; ++i) {
		if(StrCmp(argv[i], L"-f") == 0)
			++force;
		else if(StrCmp(argv[i], L"--ro") == 0) {
			RegionFlashDesireMask = 0;
			flash_ro_requested = TRUE;
		} else if(StrCmp(argv[i], L"--rw") == 0) {
			RegionFlashDesireMask = 0;
			flash_rw_requested = TRUE;
		} else if(StrCmp(argv[i], L"--all") == 0) {
			flash_all_requested = TRUE;
		} else {
			filename = argv[i];
			// the filename is the last argument. All stop!
			break;
		}
	}

	if(!filename) {
		Print(L"Usage: ectool reflash [options] FILE\n"
		      L"\n"
		      L"Attempts to safely reflash the Framework Laptop's EC\n"
		      L"Preserves vital product data and configuration bits.\n"
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

	rv = gECProtocol->SendCommand(EC_CMD_GET_VERSION, 0, NULL, 0, &VersionResponse, sizeof(VersionResponse));
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

	IncomingImageFlashMap = GetImageFlashMap(FirmwareBuffer, FileInfo->FileSize);
	if(!IncomingImageFlashMap) {
		Print(L"Failed to find flash map header in incoming firmware image.\n");
		Status = EFI_ABORTED;
		goto Out;
	}

	IncomingImageRoFridArea = GetImageFlashArea(IncomingImageFlashMap, "RO_FRID");
	if(!IncomingImageRoFridArea) {
		Print(L"Failed to find RO_FRID (firmware ID) section in incoming firmware image.\n");
		Status = EFI_ABORTED;
		goto Out;
	}

	FirmwareBoardId = FirmwareBuffer + IncomingImageRoFridArea->Offset;

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

	// Figure out which board map to use
	for(const FLASH_MAP* map = flash_maps; map->board; ++map) {
		if(0 == AsciiStrnCmp(map->board, FirmwareBoardId, FirmwareBoardIdLength)) {
			FinalFlashMap = map;
			break;
		}
	}

	if(!FinalFlashMap) {
		Print(L"*** UNKNOWN BOARD %.*a ***\n", FirmwareBoardIdLength, FirmwareBoardId);
		if (force < 2) {
			Status = EFI_ABORTED;
			goto Out;
		}
		Print(L"Assuming you know what you're doing.\n");
		FinalFlashMap = &unknown_board_flash_map;
	}

	// Calculate the mask of regions we intend to flash.
	for(const FLASH_REGION* region = FinalFlashMap->regions; region->name; ++region) {
		UINT16 RegionBit = (1 << (region - FinalFlashMap->regions));
		RegionFlashMask |= RegionBit;
		if(region->flags & FLASH_REGION_SKIP) {
			// Unmark skipped regions as desired; the user can
			// override this with --all
			RegionFlashDesireMask &= ~(RegionBit);
		}
		if(flash_ro_requested && 0 == AsciiStrCmp("RO", region->name)) {
			RegionFlashDesireMask |= RegionBit;
		} else if(flash_rw_requested && 0 == AsciiStrCmp("RW", region->name)) {
			RegionFlashDesireMask |= RegionBit;
		}
	}

	if(flash_all_requested)
		RegionFlashDesireMask = 0xFFFF;

	// Restrict the flashed regions to only the ones we actually want to overwrite.
	RegionFlashMask &= RegionFlashDesireMask;

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
	if(FinalFlashMap->flags & FLASH_DEVICE_REQUIRES_UNLOCK) {
		Print(L"Unlocking flash... ");
		FlashNotifyParams.flags = FLASH_ACCESS_SPI;
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
		if(rv < 0)
			goto EcOut;
		FlashNotifyParams.flags = FLASH_FIRMWARE_START;
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
		if(rv < 0)
			goto EcOut;
		Print(L"OK\n");
	}

	while(RegionFlashMask) {
		// We erase and flash the regions in reverse order; bailing out
		// after RW but before RO stands a chance of preserving the
		// system.
		int index = 31 - __builtin_clz(RegionFlashMask);
		const FLASH_REGION* Region = &FinalFlashMap->regions[index];

		RegionFlashMask &= ~(1 << index);

		Print(L"%a: Erasing... ", Region->name);
		rv = flash_erase(Region->base, Region->size);
		if(rv < 0)
			goto EcOut;
		Print(L"OK. ");

		Print(L"Writing... ");
		rv = flash_write(Region->base, Region->size, FirmwareBuffer + Region->base);
		if(rv < 0)
			goto EcOut;
		Print(L"OK. ");

		Print(L"Verifying: Read... ");
		rv = flash_read(Region->base, Region->size, VerifyBuffer + Region->base);
		if(rv < 0)
			goto EcOut;

		Print(L"OK. Check... ");

		if(CompareMem(VerifyBuffer + Region->base, FirmwareBuffer + Region->base, Region->size) == 0) {
			Print(L"OK!\n");
		} else {
			Print(L"FAILED!\n");
			Print(L"*** %a FAILED VERIFICATION! ABORTING ***\n", Region->name);
			rv = -1;
			goto ErrorOut;
		}
	}

	if(FinalFlashMap->flags & FLASH_DEVICE_REQUIRES_UNLOCK) {
		Print(L"Locking flash... ");
		FlashNotifyParams.flags = FLASH_ACCESS_SPI_DONE;
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
		if(rv < 0)
			goto EcOut;
		FlashNotifyParams.flags = FLASH_FIRMWARE_DONE;
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_NOTIFIED, 0, &FlashNotifyParams, sizeof(FlashNotifyParams), NULL, 0);
		if(rv < 0)
			goto EcOut;
		Print(L"OK\n");
	}

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
