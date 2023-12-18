#include <Library/BaseMemoryLib.h>
#include <Library/CrosECLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/CrosEC.h>

#include "EC.h"
#include "Flash.h"

extern EFI_CROSEC_PROTOCOL* gECProtocol;

int flash_read(int offset, int size, char* buffer) {
	struct ec_params_flash_read p;
	const int chunk = G_EC_MAX_RESPONSE;
	char buf[G_EC_MAX_RESPONSE] = {0};
	for(int i = 0; i < size; i += chunk) {
		p.offset = offset + i;
		p.size = MIN(size - i, chunk);
		int rv = gECProtocol->SendCommand(EC_CMD_FLASH_READ, 0, &p, sizeof(p), buf, p.size);
		if(rv < 0) {
			Print(L"*** FLASH READ **FAILED** AT OFFSET %x: %d\n", p.offset, rv);
			return rv;
		}
		CopyMem(buffer + i, buf, p.size);
	}
	return 0;
}

int flash_write(int offset, int size, char* buffer) {
	struct ec_response_flash_info_1 flashInfo;
	int rv;

	rv = gECProtocol->SendCommand(EC_CMD_FLASH_INFO, 1, NULL, 0, &flashInfo, sizeof(flashInfo));
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
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_WRITE, 1, p, sizeof(*p) + p->size, NULL, 0);
		if(rv < 0) {
			Print(L"*** FLASH WRITE **FAILED** AT OFFSET %x: %d\n", p->offset, rv);
			return rv;
		}
	}
	return 0;
}

int flash_erase_async(int offset, int size) {
	int rv = 0;

	struct ec_params_flash_erase_v1 p;
	int asyncTries = 4; // Up to 20 seconds at 500ms/piece
	p.cmd = FLASH_ERASE_SECTOR_ASYNC;
	p.reserved = 0;
	p.flag = 0;
	p.params.offset = offset;
	p.params.size = size;
	rv = gECProtocol->SendCommand(EC_CMD_FLASH_ERASE, 1, &p, sizeof(p), NULL, 0);

	if(rv < 0) {
		// We failed to start the erase.
		return rv;
	}

	p.cmd = FLASH_ERASE_GET_RESULT;
	do {
		gBS->Stall(500000);
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_ERASE, 1, &p, sizeof(p), NULL, 0);
		--asyncTries;
	} while(rv < 0 && asyncTries);

	return rv;
}

int flash_erase(int offset, int size) {
	struct ec_params_get_cmd_versions pgv = {};
	struct ec_response_get_cmd_versions rgv;
	struct ec_params_flash_erase p;
	int asyncTries = 20; // Up to 20 seconds at 100ms/piece
	int rv = 0;

	pgv.cmd = EC_CMD_FLASH_ERASE;
	rv = gECProtocol->SendCommand(EC_CMD_GET_CMD_VERSIONS, 0, &pgv, sizeof(pgv), &rgv, sizeof(rgv));
	if(rv < 0) {
		// If the EC doesn't support GET_CMD_VERSIONS, assume it doesn't support ERASE_V1
		rgv.version_mask = 0;
		rv = 0;
	}

	if(rgv.version_mask & EC_VER_MASK(1))
		return flash_erase_async(offset, size);

	// We only get here if we didn't support async erase.
	p.offset = offset;
	p.size = size;

	rv = gECProtocol->SendCommand(EC_CMD_FLASH_ERASE, 0, &p, sizeof(p), NULL, 0);

	// If the Erase command times out, kick it with a couple HELLOs until the bus clears
	// (some ECs will not respond to host commands while erasing flash)
	while(rv < 0 && asyncTries) {
		struct ec_params_hello ph = {};
		struct ec_response_hello rh = {};
		gBS->Stall(100000); // Wait 100ms
		rv = gECProtocol->SendCommand(EC_CMD_HELLO, 0, &ph, sizeof(ph), &rh, sizeof(rh));
		--asyncTries;
	}

	return rv;
}
