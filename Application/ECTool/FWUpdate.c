#include <Library/CrosECLib.h>
#include <Library/UefiLib.h>

#include "FWUpdate.h"

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

	if(0 == (batteryFlag & EC_BATT_FLAG_AC_PRESENT) || ((100ULL * batteryCap) / batteryLfcc) < 20) {
		Print(L"Make sure AC is connected and that the battery is at least 20%% charged.\n");
		return EFI_NOT_READY;
	}

	return EFI_SUCCESS;
}
