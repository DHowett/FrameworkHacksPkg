#include <Uefi.h>

#pragma pack (push, 1)
// From ec:common/fmap.c
#define FMAP_NAMELEN 32
#define FMAP_SIGNATURE "__FMAP__"
#define FMAP_SIGNATURE_SIZE 8
typedef struct _EC_IMAGE_FMAP_HEADER {
	CHAR8  Signature[FMAP_SIGNATURE_SIZE];
	UINT8  VerMajor;
	UINT8  VerMinor;
	UINT64 Base;
	UINT32 Size;
	CHAR8  Name[FMAP_NAMELEN];
	UINT16 NAreas;
} EC_IMAGE_FMAP_HEADER;
typedef struct _EC_IMAGE_FMAP_AREA_HEADER {
	UINT32 Offset;
	UINT32 Size;
	CHAR8  Name[FMAP_NAMELEN];
	UINT16 Flags;
} EC_IMAGE_FMAP_AREA_HEADER;
#pragma pack (pop)

EC_IMAGE_FMAP_HEADER* GetImageFlashMap(const VOID* Buffer, UINTN Length);

EC_IMAGE_FMAP_AREA_HEADER* GetImageFlashArea(EC_IMAGE_FMAP_HEADER* Map, CHAR8* AreaName);
