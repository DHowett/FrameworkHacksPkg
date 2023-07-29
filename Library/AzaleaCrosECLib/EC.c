#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/CrosECLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/CrosEC.h>
#include <Protocol/DevicePath.h>

#define memcpy CopyMem

typedef UINT16 USHORT;
typedef UINT8 UCHAR;

typedef enum _EC_TRANSFER_DIRECTION {
	EC_XFER_WRITE,
	EC_XFER_READ
} EC_TRANSFER_DIRECTION;

static int ECTransfer(EC_TRANSFER_DIRECTION direction, UINT16 address, char* data, UINT16 size) {
	UINT16 basePort = 0x800;
	basePort |= (-((address & 0xFF00) > 0) & 0b0000011000000000);  // Set bits 9, 10 if the address is over 0xFF
	basePort |= (-((address & 0xF000) > 0) & 0b0000000100000000);  // Set bit 8 if the address is over 0xFFF
	address = address & 0xFF;                                      // Mask off the bottom byte
	if(direction == EC_XFER_WRITE) {
		for(int i = 0; i < size; ++i) {
			IoWrite8(basePort + address + i, data[i]);
		}
	} else {
		for(int i = 0; i < size; ++i) {
			data[i] = IoRead8(basePort + address + i);
		}
	}

	return 0;
}

/*
 * Wait for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout.
 */
static int ECWaitForReady(int statusAddr, int timeoutUsec) {
	int i;
	int delay = 5;

	for(i = 0; i < timeoutUsec; i += delay) {
		/*
		 * Delay first, in case we just sent out a command but the EC
		 * hasn't raised the busy flag.  However, I think this doesn't
		 * happen since the LPC commands are executed in order and the
		 * busy flag is set by hardware.  Minor issue in any case,
		 * since the initial delay is very short.
		 */
		gBS->Stall(MIN(delay, timeoutUsec - i));

		if(!(IoRead8(statusAddr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;

		/* Increase the delay interval after a few rapid checks */
		if(i > 20)
			delay = MIN(delay * 2, 10000);
	}
	return -1; /* Timeout */
}

static UCHAR ECChecksumBuffer(char* data, int size) {
	UCHAR sum = 0;
	for(int i = 0; i < size; ++i) {
		sum += data[i];
	}
	return sum;
};

static INTN EFIAPI ECReadMemory(UINTN offset, void* buffer, UINTN length) {
	int off = offset;
	int cnt = 0;
	UCHAR* s = buffer;

	if(offset + length > EC_MEMMAP_SIZE) {
		return -1;
	}

	if(length > 0) {
		// Read specified bytes directly
		ECTransfer(EC_XFER_READ, (USHORT)(0x100 + off), buffer, (USHORT)length);
		cnt = length;
	} else {
		// Read a string until we get a \0
		for(; off < EC_MEMMAP_SIZE; ++off, ++s) {
			ECTransfer(EC_XFER_READ, (USHORT)(0x100 + off), (char*)s, 1);
			cnt++;
			if(!*s) {
				break;
			}
		}
	}

	return cnt;
}

static INTN EFIAPI
ECSendCommand(UINTN command, UINTN version, const void* outdata, UINTN outsize, void* indata, UINTN insize) {
	int res = EC_RES_SUCCESS;
	UCHAR csum = 0;
	int i;

	union {
		struct ec_host_request rq;
		char data[EC_LPC_HOST_PACKET_SIZE];
	} u;

	union {
		struct ec_host_response rs;
		char data[EC_LPC_HOST_PACKET_SIZE];
	} r;

	/* Fail if output size is too big */
	if(outsize + sizeof(u.rq) > EC_LPC_HOST_PACKET_SIZE) {
		res = -EC_RES_REQUEST_TRUNCATED;
		goto Out;
	}

	/* Fill in request packet */
	/* TODO(crosbug.com/p/23825): This should be common to all protocols */
	u.rq.struct_version = EC_HOST_REQUEST_VERSION;
	u.rq.checksum = 0;
	u.rq.command = (USHORT)command;
	u.rq.command_version = (UCHAR)version;
	u.rq.reserved = 0;
	u.rq.data_len = (USHORT)outsize;

	memcpy(&u.data[sizeof(u.rq)], outdata, outsize);
	csum = ECChecksumBuffer(u.data, outsize + sizeof(u.rq));
	u.rq.checksum = (UCHAR)(-csum);

	if(ECWaitForReady(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		res = -EC_RES_TIMEOUT;
		goto Out;
	}

	ECTransfer(EC_XFER_WRITE, 0, u.data, (USHORT)(outsize + sizeof(u.rq)));

	/* Start the command */
	IoWrite8(EC_LPC_ADDR_HOST_CMD, EC_COMMAND_PROTOCOL_3);

	if(ECWaitForReady(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		res = -EC_RES_TIMEOUT;
		goto Out;
	}

	/* Check result */
	i = IoRead8(EC_LPC_ADDR_HOST_DATA);
	if(i) {
		res = -EECRESULT - i;
		goto Out;
	}

	csum = 0;
	ECTransfer(EC_XFER_READ, 0, r.data, sizeof(r.rs));

	if(r.rs.struct_version != EC_HOST_RESPONSE_VERSION) {
		res = -EC_RES_INVALID_HEADER_VERSION;
		goto Out;
	}

	if(r.rs.reserved) {
		res = -EC_RES_INVALID_HEADER;
		goto Out;
	}

	if(r.rs.data_len > insize) {
		res = -EC_RES_RESPONSE_TOO_BIG;
		goto Out;
	}

	if(r.rs.data_len > 0) {
		ECTransfer(EC_XFER_READ, 8, r.data + sizeof(r.rs), r.rs.data_len);
		if(ECChecksumBuffer(r.data, sizeof(r.rs) + r.rs.data_len)) {
			res = -EC_RES_INVALID_CHECKSUM;
			goto Out;
		}

		memcpy(indata, r.data + sizeof(r.rs), r.rs.data_len);
	}
	res = r.rs.data_len;

Out:
	return res;
}

/// PROTOCOL INSTALLATION

static CONST EFI_CROSEC_PROTOCOL mCrosECProtocol = {
	.SendCommand = &ECSendCommand,
	.ReadMemory = &ECReadMemory,
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
AzaleaCrosECLibConstructor() {
	EFI_STATUS Status = EFI_SUCCESS;
	UCHAR Id[2];
	if(2 == ECReadMemory(EC_MEMMAP_ID, Id, 2)) {
		if(Id[0] == 'E' && Id[1] == 'C') {
			Status = gBS->InstallMultipleProtocolInterfaces(&mProtocolHandle, &gEfiDevicePathProtocolGuid,
			                                                &mVendorDevicePath, &gEfiCrosECProtocolGuid,
			                                                (EFI_CROSEC_PROTOCOL*)&mCrosECProtocol, NULL);
		}
	}
	if(Status == EFI_ALREADY_STARTED) {
		// An EC has already been detected. We can safely bail out.
		Status = EFI_SUCCESS;
	}
	return Status;
}

EFI_STATUS
EFIAPI
AzaleaCrosECLibDestructor() {
	EFI_STATUS Status = EFI_SUCCESS;

	if(mProtocolHandle != NULL) {
		Status = gBS->UninstallMultipleProtocolInterfaces(mProtocolHandle, &gEfiDevicePathProtocolGuid,
		                                                  &mVendorDevicePath, &gEfiCrosECProtocolGuid,
		                                                  (EFI_CROSEC_PROTOCOL*)&mCrosECProtocol, NULL);
	}

	return Status;
}
