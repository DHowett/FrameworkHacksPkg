#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/FmapLib.h>

EC_IMAGE_FMAP_HEADER* GetImageFlashMap(const VOID* Buffer, UINTN Length) {
	return (EC_IMAGE_FMAP_HEADER*)ScanMem64(Buffer, Length, 0x5F5F50414D465F5FULL /* __FMAP__, little-endian */);
}

EC_IMAGE_FMAP_AREA_HEADER* GetImageFlashArea(EC_IMAGE_FMAP_HEADER* Map, CHAR8* AreaName) {
	EC_IMAGE_FMAP_AREA_HEADER* FmapAreas = (EC_IMAGE_FMAP_AREA_HEADER*)(Map + 1);
	for(int i = 0; i < Map->NAreas; ++i) {
		if (0 == AsciiStrnCmp(AreaName, FmapAreas[i].Name, FMAP_NAMELEN)) {
			return FmapAreas + i;
		}
	}
	return NULL;
}
