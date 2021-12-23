#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/TimerLib.h>
#include "EC.h"

static __inline void outb(unsigned char __val, unsigned short __port)
{
	__asm__ volatile ("outb %0,%1" : : "a" (__val), "dN" (__port));
}

static __inline void outw(unsigned short __val, unsigned short __port)
{
	__asm__ volatile ("outw %0,%1" : : "a" (__val), "dN" (__port));
}

static __inline unsigned char inb(unsigned short __port)
{
	unsigned char __val;
	__asm__ volatile ("inb %1,%0" : "=a" (__val) : "dN" (__port));
	return __val;
}

static __inline unsigned short inw(unsigned short __port)
{
	unsigned short __val;
	__asm__ volatile ("inw %1,%0" : "=a" (__val) : "dN" (__port));
	return __val;
}

typedef enum _ec_transaction_direction { EC_TX_WRITE, EC_TX_READ } ec_transaction_direction;

static int ec_transact(ec_transaction_direction direction, UINT16 address,
		       char *data, UINT16 size)
{
	int pos = 0;
	UINT16 temp[2];
	if (address % 4 > 0) {
		outw(address & 0xFFFC, 0x802);
		/* Unaligned start address */
		for (int i = address % 4; i < 4; ++i) {
			char *storage = &data[pos++];
			if (direction == EC_TX_WRITE)
				outb(*storage, 0x804 + i);
			else if (direction == EC_TX_READ)
				*storage = inb(0x804 + i);
		}
		address = (address + 4) & 0xFFFC; // Up to next multiple of 4
	}

	if (size - pos >= 4) {
		outw((address & 0xFFFC) | 0x3, 0x802);
		// Chunk writing for anything large, 4 bytes at a time
		// Writing to 804, 806 automatically increments dest address
		while (size - pos >= 4) {
			if (direction == EC_TX_WRITE) {
				CopyMem(temp, &data[pos], sizeof(temp));
				outw(temp[0], 0x804);
				outw(temp[1], 0x806);
			} else if (direction == EC_TX_READ) {
				//data[pos] = inb(0x804);
				//data[pos + 1] = inb(0x805);
				//data[pos + 2] = inb(0x806);
				//data[pos + 3] = inb(0x807);
				temp[0] = inw(0x804);
				temp[1] = inw(0x806);
				CopyMem(&data[pos], temp, sizeof(temp));
			}

			pos += 4;
			address += 4;
		}
	}

	if (size - pos > 0) {
		// Unaligned remaining data - R/W it by byte
		outw(address & 0xFFFC, 0x802);
		for (int i = 0; i < (size - pos); ++i) {
			char *storage = &data[pos + i];
			if (direction == EC_TX_WRITE)
				outb(*storage, 0x804 + i);
			else if (direction == EC_TX_READ)
				*storage = inb(0x804 + i);
		}
	}
	return 0;
}

/*
 * Wait for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout.
 */
static int wait_for_ec(int status_addr, int timeout_usec)
{
	int i;
	int delay = 5;

	for (i = 0; i < timeout_usec; i += delay) {
		/*
		 * Delay first, in case we just sent out a command but the EC
		 * hasn't raised the busy flag.  However, I think this doesn't
		 * happen since the LPC commands are executed in order and the
		 * busy flag is set by hardware.  Minor issue in any case,
		 * since the initial delay is very short.
		 */
		MicroSecondDelay(MIN(delay, timeout_usec - i));

		if (!(inb(status_addr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;

		/* Increase the delay interval after a few rapid checks */
		if (i > 20)
			delay = MIN(delay * 2, 10000);
	}
	return -1; /* Timeout */
}

static UINT8 ec_checksum_buffer(char *data, int size)
{
	UINT8 sum = 0;
	for (int i = 0; i < size; ++i) {
		sum += data[i];
	}
	return sum;
};

int ec_command(int command, int version, const void *outdata,
	       int outsize, void *indata, int insize)
{
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

	/* Fail if output size is too big */
	if (outsize + sizeof(u.rq) > EC_LPC_HOST_PACKET_SIZE)
		return -EC_RES_REQUEST_TRUNCATED;

	/* Fill in request packet */
	/* TODO(crosbug.com/p/23825): This should be common to all protocols */
	u.rq.struct_version = EC_HOST_REQUEST_VERSION;
	u.rq.checksum = 0;
	u.rq.command = command;
	u.rq.command_version = version;
	u.rq.reserved = 0;
	u.rq.data_len = outsize;

	CopyMem(&u.data[sizeof(u.rq)], outdata, outsize);
	csum = ec_checksum_buffer(u.data, outsize + sizeof(u.rq));
	u.rq.checksum = (UINT8)(-csum);

	if (wait_for_ec(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		return -EC_RES_ERROR;
	}

	ec_transact(EC_TX_WRITE, 0, u.data, outsize + sizeof(u.rq));

	/* Start the command */
	outb(EC_COMMAND_PROTOCOL_3, EC_LPC_ADDR_HOST_CMD);

	if (wait_for_ec(EC_LPC_ADDR_HOST_CMD, 1000000)) {
		return -EC_RES_ERROR;
	}

	/* Check result */
	i = inb(EC_LPC_ADDR_HOST_DATA);
	if (i) {
		return -EECRESULT - i;
	}

	csum = 0;
	ec_transact(EC_TX_READ, 0, r.data, sizeof(r.rs));

	if (r.rs.struct_version != EC_HOST_RESPONSE_VERSION) {
		return -EC_RES_INVALID_RESPONSE;
	}

	if (r.rs.reserved) {
		return -EC_RES_INVALID_RESPONSE;
	}

	if (r.rs.data_len > insize) {
		return -EC_RES_RESPONSE_TOO_BIG;
	}

	if (r.rs.data_len > 0) {
		ec_transact(EC_TX_READ, 8, r.data + sizeof(r.rs), r.rs.data_len);
		if (ec_checksum_buffer(r.data, sizeof(r.rs) + r.rs.data_len)) {
			return -EC_RES_INVALID_CHECKSUM;
		}

		CopyMem(indata, r.data + sizeof(r.rs), r.rs.data_len);
	}
	return r.rs.data_len;
}
