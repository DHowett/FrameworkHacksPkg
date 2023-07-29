#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiShellLib/UefiShellLib.h>

#include <Protocol/Shell.h>
#include <Protocol/ShellParameters.h>

#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __ec_align4 __packed __aligned(4)

#define EECRESULT 1000
#define EC_LPC_ADDR_HOST_DATA  0x200
#define EC_LPC_ADDR_HOST_CMD   0x204
#define EC_LPC_ADDR_HOST_PACKET  0x800  /* Offset of version 3 packet */
#define EC_LPC_HOST_PACKET_SIZE  0x100  /* Max size of version 3 packet */
#ifndef BIT
#define BIT(nr)         (1UL << (nr))
#endif
#define EC_LPC_STATUS_FROM_HOST   0x02
#define EC_LPC_STATUS_PROCESSING  0x04
#define EC_LPC_STATUS_BUSY_MASK \
	(EC_LPC_STATUS_FROM_HOST | EC_LPC_STATUS_PROCESSING)

#define EC_LPC_ADDR_MEMMAP       0x900
#define EC_MEMMAP_SIZE         255 /* ACPI IO buffer max is 255 bytes */
#define EC_MEMMAP_ID               0x20 /* 0x20 == 'E', 0x21 == 'C' */

enum ec_status {
	EC_RES_SUCCESS = 0,
	EC_RES_INVALID_COMMAND = 1,
	EC_RES_ERROR = 2,
	EC_RES_INVALID_PARAM = 3,
	EC_RES_ACCESS_DENIED = 4,
	EC_RES_INVALID_RESPONSE = 5,
	EC_RES_INVALID_VERSION = 6,
	EC_RES_INVALID_CHECKSUM = 7,
	EC_RES_IN_PROGRESS = 8,		/* Accepted, command in progress */
	EC_RES_UNAVAILABLE = 9,		/* No response available */
	EC_RES_TIMEOUT = 10,		/* We got a timeout */
	EC_RES_OVERFLOW = 11,		/* Table / data overflow */
	EC_RES_INVALID_HEADER = 12,     /* Header contains invalid data */
	EC_RES_REQUEST_TRUNCATED = 13,  /* Didn't get the entire request */
	EC_RES_RESPONSE_TOO_BIG = 14,   /* Response was too big to handle */
	EC_RES_BUS_ERROR = 15,		/* Communications bus error */
	EC_RES_BUSY = 16,		/* Up but too busy.  Should retry */
	EC_RES_INVALID_HEADER_VERSION = 17,  /* Header version invalid */
	EC_RES_INVALID_HEADER_CRC = 18,      /* Header CRC invalid */
	EC_RES_INVALID_DATA_CRC = 19,        /* Data CRC invalid */
	EC_RES_DUP_UNAVAILABLE = 20,         /* Can't resend response */
};

#define EC_COMMAND_PROTOCOL_3 0xda
#define EC_HOST_REQUEST_VERSION 3

struct ec_host_request {
	UINT8 struct_version;
	UINT8 checksum;
	UINT16 command;
	UINT8 command_version;
	UINT8 reserved;
	UINT16 data_len;
} __ec_align4;

struct ec_host_response {
	UINT8 struct_version;
	UINT8 checksum;
	UINT16 result;
	UINT16 data_len;
	UINT16 reserved;
} __ec_align4;
#define EC_HOST_RESPONSE_VERSION 3

typedef enum _EC_TRANSFER_DIRECTION {
	EC_XFER_WRITE,
	EC_XFER_READ
} EC_TRANSFER_DIRECTION;

typedef int (*EC_XFER_FN)(EC_TRANSFER_DIRECTION, UINT16, char*, UINT16);

static int NpcxXfer(EC_TRANSFER_DIRECTION direction, UINT16 address, char* data, UINT16 size) {
	UINT16 basePort = 0x800;
	basePort |= (-((address & 0xFF00) > 0) & 0b0000011000000000); // Set bits 9, 10 if the address is over 0xFF
	basePort |= (-((address & 0xF000) > 0) & 0b0000000100000000); // Set bit 8 if the address is over 0xFFF
	address = address & 0xFF;  // Mask off the bottom byte
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

static int MecXfer(EC_TRANSFER_DIRECTION direction, UINT16 address, char* data, UINT16 size) {
#define MEC_EC_BYTE_ACCESS               0x00
#define MEC_EC_LONG_ACCESS_AUTOINCREMENT 0x03
#define MEC_LPC_ADDRESS_REGISTER0 0x0802
#define MEC_LPC_DATA_REGISTER0    0x0804
#define MEC_LPC_DATA_REGISTER2    0x0806
	int pos = 0;
	UINT16 temp[2];
	if(address % 4 > 0) {
		IoWrite16(MEC_LPC_ADDRESS_REGISTER0, (address & 0xFFFC) | MEC_EC_BYTE_ACCESS);
		/* Unaligned start address */
		for(int i = address % 4; i < 4; ++i) {
			char* storage = &data[pos++];
			if(direction == EC_XFER_WRITE)
				IoWrite8(MEC_LPC_DATA_REGISTER0 + i, *storage);
			else if(direction == EC_XFER_READ)
				*storage = IoRead8(MEC_LPC_DATA_REGISTER0 + i);
		}
		address = (address + 4) & 0xFFFC;  // Up to next multiple of 4
	}

	if(size - pos >= 4) {
		IoWrite16(MEC_LPC_ADDRESS_REGISTER0, (address & 0xFFFC) | MEC_EC_LONG_ACCESS_AUTOINCREMENT);
		// Chunk writing for anything large, 4 bytes at a time
		// Writing to 804, 806 automatically increments dest address
		while(size - pos >= 4) {
			if(direction == EC_XFER_WRITE) {
				CopyMem(temp, &data[pos], sizeof(temp));
				IoWrite16(MEC_LPC_DATA_REGISTER0, temp[0]);
				IoWrite16(MEC_LPC_DATA_REGISTER2, temp[1]);
			} else if(direction == EC_XFER_READ) {
				temp[0] = IoRead16(MEC_LPC_DATA_REGISTER0);
				temp[1] = IoRead16(MEC_LPC_DATA_REGISTER2);
				CopyMem(&data[pos], temp, sizeof(temp));
			}

			pos += 4;
			address += 4;
		}
	}

	if(size - pos > 0) {
		// Unaligned remaining data - R/W it by byte
		IoWrite16(MEC_LPC_ADDRESS_REGISTER0, (address & 0xFFFC) | MEC_EC_BYTE_ACCESS);
		for(int i = 0; i < (size - pos); ++i) {
			char* storage = &data[pos + i];
			if(direction == EC_XFER_WRITE)
				IoWrite8(MEC_LPC_DATA_REGISTER0 + i, *storage);
			else if(direction == EC_XFER_READ)
				*storage = IoRead8(MEC_LPC_DATA_REGISTER0 + i);
		}
	}
	return 0;
}

static int ECWaitForReady(int statusAddr, int timeoutUsec) {
	int i;
	int delay = 5;

	for(i = 0; i < timeoutUsec; i += delay) {
		gBS->Stall(MIN(delay, timeoutUsec - i));

		if(!(IoRead8(statusAddr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;

		if(i > 20)
			delay = MIN(delay * 2, 10000);
	}
	return -1;
}

static UINT8 ECChecksumBuffer(char* data, int size) {
	UINT8 sum = 0;
	for(int i = 0; i < size; ++i) {
		sum += data[i];
	}
	return sum;
}

static INTN ECReadMem(EC_XFER_FN xfer, UINTN offset, void* buffer, UINTN length) {
	int off = offset;
	xfer(EC_XFER_READ, (UINT16)(0x100 + off), buffer, (UINT16)length);
	return length;
}

static INTN ECSendCommandOneWay(EC_XFER_FN xfer, UINTN command, void* indata, UINTN insize) {
	int res = EC_RES_SUCCESS;
	UINT8 csum = 0;
	int i;

	union {
		struct ec_host_request rq;
		char data[EC_LPC_HOST_PACKET_SIZE];
	} u;

	union {
		struct ec_host_response rs;
		char data[EC_LPC_HOST_PACKET_SIZE];
	} r;

	u.rq.struct_version = EC_HOST_REQUEST_VERSION;
	u.rq.checksum = 0;
	u.rq.command = (UINT16)command;
	u.rq.command_version = 0;
	u.rq.reserved = 0;
	u.rq.data_len = 0;

	csum = ECChecksumBuffer(u.data, sizeof(u.rq));
	u.rq.checksum = (UINT8)(-csum);

	if(ECWaitForReady(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		res = -EC_RES_TIMEOUT;
		goto Out;
	}

	xfer(EC_XFER_WRITE, 0, u.data, (UINT16)(sizeof(u.rq)));

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
	xfer(EC_XFER_READ, 0, r.data, sizeof(r.rs));

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
		xfer(EC_XFER_READ, 8, r.data + sizeof(r.rs), r.rs.data_len);
		if(ECChecksumBuffer(r.data, sizeof(r.rs) + r.rs.data_len)) {
			res = -EC_RES_INVALID_CHECKSUM;
			goto Out;
		}

		CopyMem(indata, r.data + sizeof(r.rs), r.rs.data_len);
	}
	res = r.rs.data_len;

Out:
	return res;
}

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_SUCCESS;
	UINT8 Ident[2];
	EC_XFER_FN Xfer = NULL;

	Status = ShellInitialize();
	if(EFI_ERROR(Status)) {
		Print(L"Failed to initialize shell lib: %x\n", Status);
		return Status;
	}

	Print(L"MEC LPC ReadMem... ");
	ECReadMem(&MecXfer, EC_MEMMAP_ID, Ident, 2);
	if(Ident[0] == 'E' && Ident[1] == 'C') {
		Xfer = &MecXfer;
		Print(L"OK!\n");
	} else {
		Print(L"NAK!\n");
	}

	Ident[0] = Ident[1] = 0;
	Print(L"NPCX High LPC ReadMem... ");
	ECReadMem(&NpcxXfer, EC_MEMMAP_ID, Ident, 2);
	if(Ident[0] == 'E' && Ident[1] == 'C') {
		Xfer = &NpcxXfer;
		Print(L"OK!\n");
	} else {
		Print(L"NAK!\n");
	}

	if(!Xfer) {
		Print(L"No EC detected; bailing.\n");
		return EFI_DEVICE_ERROR;
	}

	Print(L"Host Command Interface... ");
	{
		UINT32 ProtoVersion = 0xFFFFFFFF;
		INTN rv = ECSendCommandOneWay(Xfer, 0x0 /* EC_CMD_PROTO_VERSION */, &ProtoVersion, sizeof(ProtoVersion));
		if (rv < 0) {
			Print(L"NAK (%d)\n", rv);
		} else if (rv == 0) {
			Print(L"??? (No Data)\n");
		} else {
			Print(L"OK (%d)\n", ProtoVersion);
		}
	}

	return EFI_SUCCESS;
}
