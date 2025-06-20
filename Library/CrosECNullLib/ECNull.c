#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CrosECLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FmapLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/CrosEC.h>
#include <Protocol/DevicePath.h>

#define HOST_COMMAND_IMPL(EC_CMD_name) \
	static INTN impl_##EC_CMD_name(const void* params, INTN paramsSize, void* response, INTN responseSize)
#define HOST_COMMAND_CASE(EC_CMD_name)                                       \
	case EC_CMD_##EC_CMD_name:                                           \
		return impl_##EC_CMD_name(outdata, outsize, indata, insize); \
		break

#define HC_PARAMS(type)   struct type* p = (struct type*)params
#define HC_RESPONSE(type) struct type* r = (struct type*)response

// This is a memory dump of EC mapped memory from a Framework Laptop 13 (11th Gen Intel) running EC ver 8109392.
// It was plugged in at the time (so the battery flags includes AC_PRESENT).
unsigned char ECMEM_BIN[] = {
	0x6b, 0x79, 0x6e, 0x5e, 0x98, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xad,
	0x0b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x45, 0x43,
	0x01, 0x02, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcb, 0x40, 0x00, 0x00,
	0x03, 0x00, 0x00, 0x00, 0xf8, 0x0a, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0xf4, 0x0d, 0x00, 0x00, 0x28,
	0x3c, 0x00, 0x00, 0x94, 0x0d, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x4e, 0x56, 0x54, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x46, 0x72, 0x61, 0x6d, 0x65, 0x77, 0x6f, 0x00, 0x30, 0x30, 0x43, 0x44, 0x00, 0x00, 0x00,
	0x00, 0x4c, 0x49, 0x4f, 0x4e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
unsigned int ECMEM_BIN_LEN = 254;

UINT8* gMutableFlash;
static const UINTN gFlashLen = 1048576;

INTN EFIAPI ECReadMemoryNullLpc(UINTN offset, void* buffer, UINTN length) {
	int off = offset;
	int cnt = 0;
	UINT8* s = buffer;

	if(offset + length > EC_MEMMAP_SIZE) {
		return -1;
	}

	if(length > 0) {
		// Read specified bytes directly
		CopyMem(buffer, &ECMEM_BIN[off], length);
		cnt = length;
	} else {
		// Read a string until we get a \0
		for(; off < EC_MEMMAP_SIZE; ++off, ++s) {
			*s = ECMEM_BIN[off];
			cnt++;
			if(!*s) {
				break;
			}
		}
	}

	return cnt;
}

static int flashBytesErased = 0;
static int flashBytesWritten = 0;
static int gConsoleCount = 0;

static const char* STATIC_CONSOLE_LOGS[] = {
	"This is the CrosECNullLib\r\n"
	"simulation console. It wi",

	"\r\nll be read",

	" out in multipl\n",

	"e p",

	"ackets.\r\n",
};

HOST_COMMAND_IMPL(GET_BUILD_INFO) {
	CopyMem(response, "CrosECNullLib Driver", MIN(responseSize, 20));
	return 20;  // bytes
}

HOST_COMMAND_IMPL(GET_VERSION) {
	static struct ec_response_get_version resp = {
		.version_string_rw = "fake_0.0.1",
	};
	EC_IMAGE_FMAP_HEADER* hdr = GetImageFlashMap(gMutableFlash, gFlashLen);
	if(hdr) {
		EC_IMAGE_FMAP_AREA_HEADER* rofrid = GetImageFlashArea(hdr, "RO_FRID");
		if(rofrid) {
			CopyMem(resp.version_string_ro, gMutableFlash + rofrid->Offset, sizeof(resp.version_string_ro));
		}
		EC_IMAGE_FMAP_AREA_HEADER* rwfwid = GetImageFlashArea(hdr, "RW_FWID");
		if(rwfwid) {
			CopyMem(resp.version_string_rw, gMutableFlash + rwfwid->Offset, sizeof(resp.version_string_rw));
		}
	}
	CopyMem(response, &resp, sizeof(resp));
	return sizeof(resp);
}

HOST_COMMAND_IMPL(FLASH_INFO) {
	static struct ec_response_flash_info_1 resp = {
		.flash_size = gFlashLen,
		.write_block_size = 4,
		.erase_block_size = 4096,
		.protect_block_size = 4096,
		.write_ideal_size = 240,
		.flags = 0,
	};
	CopyMem(response, &resp, sizeof(resp));
	return sizeof(resp);
}

HOST_COMMAND_IMPL(FLASH_READ) {  // flash read
	HC_PARAMS(ec_params_flash_read);
	CopyMem(response, &gMutableFlash[p->offset], MIN(responseSize, p->size));
	// If you write the entire 512kb flash with a 457ns stall per 240 bytes, it will take one second.
	gBS->Stall(457);
	return MIN(responseSize, p->size);
}

HOST_COMMAND_IMPL(FLASH_WRITE) {  // flash write
	HC_PARAMS(ec_params_flash_write);
	DebugPrint(DEBUG_VERBOSE, "FLASH_WRITE: %x for %d bytes\n", p->offset, p->size);
	for(int i = 0; i < p->size; ++i) {
		gMutableFlash[p->offset + i] &= ((char*)(p + 1))[i];
	}
	// inject fault
	// gMutableFlash[p->offset] ^= 0x7f;
	flashBytesWritten += p->size;
	// If you write the entire 512kb flash with a 457ns stall per 240 bytes, it will take one second.
	gBS->Stall(457);
	return p->size;
}

HOST_COMMAND_IMPL(FLASH_ERASE) {
	HC_PARAMS(ec_params_flash_erase);
	DebugPrint(DEBUG_VERBOSE, "FLASH_ERASE: %x for %d blocks\n", p->offset, p->size / 4096);
	SetMem(&gMutableFlash[p->offset], p->size, 0xff);
	gBS->Stall(500000);  // Takes a half second.
	flashBytesErased += p->size;
	return 0;
}

HOST_COMMAND_IMPL(CONSOLE_SNAPSHOT) {
	gConsoleCount = 0;
	return 0;  // A-OK! We "snapshotted" the "console!"
}

HOST_COMMAND_IMPL(CONSOLE_READ) {
	HC_PARAMS(ec_params_console_read_v1);

	if(gConsoleCount >= ARRAY_SIZE(STATIC_CONSOLE_LOGS)) {
		return EC_RES_SUCCESS;
	}

	if(p->subcmd == CONSOLE_READ_RECENT || p->subcmd == CONSOLE_READ_NEXT) {
		const char* currentLogEntry = STATIC_CONSOLE_LOGS[gConsoleCount++];
		UINTN entryLength = AsciiStrLen(currentLogEntry);
		CopyMem(response, currentLogEntry, MIN(responseSize, entryLength));
		return MIN(responseSize, entryLength);
	}

	return -EECRESULT - EC_RES_INVALID_PARAM;
}

INTN EFIAPI
ECSendCommandNullLpc(UINTN command, UINTN version, const void* outdata, UINTN outsize, void* indata, UINTN insize) {
	DebugPrint(DEBUG_VERBOSE, "ECSendCommandLPCv3: %X/%d send %d bytes expect %d\n", command, version, outsize,
	           insize);
	switch(command) {
		HOST_COMMAND_CASE(GET_BUILD_INFO);
		HOST_COMMAND_CASE(GET_VERSION);
		HOST_COMMAND_CASE(FLASH_INFO);
		HOST_COMMAND_CASE(FLASH_READ);
		HOST_COMMAND_CASE(FLASH_WRITE);
		HOST_COMMAND_CASE(FLASH_ERASE);
		HOST_COMMAND_CASE(CONSOLE_SNAPSHOT);
		HOST_COMMAND_CASE(CONSOLE_READ);

		case 0x3E01:  // Framework flash lock/unlock
			DebugPrint(DEBUG_VERBOSE, "FW FLASH LOCK STATE: %d\n", (int)(*(char*)outdata));
			return EC_RES_SUCCESS;
		case 0x3EFF:  // Custom EC command to dump flash stats
			DebugPrint(DEBUG_VERBOSE, "FLASH_STAT: %d erased, %d written\n", flashBytesErased,
			           flashBytesWritten);
			return EC_RES_SUCCESS;
	}
	return -EC_RES_INVALID_COMMAND;
}

/// PROTOCOL INSTALLATION

static CONST EFI_CROSEC_PROTOCOL mCrosECProtocol = {
	.SendCommand = &ECSendCommandNullLpc,
	.ReadMemory = &ECReadMemoryNullLpc,
};

typedef struct {
	VENDOR_DEVICE_PATH VendorDevicePath;
	EFI_DEVICE_PATH_PROTOCOL End;
} TERMINATED_VENDOR_DEVICE_PATH;

static TERMINATED_VENDOR_DEVICE_PATH mVendorDevicePath = {
	{{HARDWARE_DEVICE_PATH,
          HW_VENDOR_DP,
          {(UINT8)(sizeof(VENDOR_DEVICE_PATH)), (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8)}},
         EFI_CROSEC_PROTOCOL_GUID},
	{END_DEVICE_PATH_TYPE,
         END_ENTIRE_DEVICE_PATH_SUBTYPE,
         {(UINT8)(END_DEVICE_PATH_LENGTH), (UINT8)((END_DEVICE_PATH_LENGTH) >> 8)}}};

static EFI_HANDLE mProtocolHandle = NULL;

EFI_STATUS
EFIAPI
CrosECNullLibConstructor() {
	EFI_STATUS Status = EFI_SUCCESS;
	gMutableFlash = (UINT8*)AllocatePool(gFlashLen);
	SetMem(gMutableFlash, gFlashLen, 0xFF);
	Status = gBS->InstallMultipleProtocolInterfaces(&mProtocolHandle, &gEfiDevicePathProtocolGuid,
	                                                &mVendorDevicePath, &gEfiCrosECProtocolGuid,
	                                                (EFI_CROSEC_PROTOCOL*)&mCrosECProtocol, NULL);
	if(Status == EFI_ALREADY_STARTED) {
		// An EC has already been detected. We can safely bail out.
		Status = EFI_SUCCESS;
	}
	return Status;
}

EFI_STATUS
EFIAPI
CrosECNullLibDestructor() {
	EFI_STATUS Status = EFI_SUCCESS;
	FreePool(gMutableFlash);
	gMutableFlash = NULL;

	if(mProtocolHandle != NULL) {
		Status = gBS->UninstallMultipleProtocolInterfaces(mProtocolHandle, &gEfiDevicePathProtocolGuid,
		                                                  &mVendorDevicePath, &gEfiCrosECProtocolGuid,
		                                                  (EFI_CROSEC_PROTOCOL*)&mCrosECProtocol, NULL);
	}

	return Status;
}
