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
	for(int i = 0; i < 3; ++i) {
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_INFO, 1, NULL, 0, &flashInfo, sizeof(flashInfo));
		if(rv >= 0)
			break;
		else if(rv == -EC_RES_BUSY || rv == -EC_RES_TIMEOUT) {
			gBS->Stall(10000); // wait 100msec; did the EC come back?
		} else // All other errors
			break;
	}

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

int flash_erase(int offset, int size) {
	struct ec_params_get_cmd_versions pgv = {};
	struct ec_response_get_cmd_versions rgv;
	int rv;

	pgv.cmd = EC_CMD_FLASH_ERASE;
	rv = gECProtocol->SendCommand(EC_CMD_GET_CMD_VERSIONS, 0, &pgv, sizeof(pgv), &rgv, sizeof(rgv));
	if(rv < 0) {
		Print(L"Failed to determine supported versions of ERASE\n");
		return rv;
	}

	struct ec_params_flash_erase p;
	p.offset = offset;
	p.size = size;

	if(rgv.version_mask & EC_VER_MASK(1)) {
		// If the chip supports asynchronous erase, **don't use it**.
		struct ec_params_flash_erase_v1 pv1;
		pv1.cmd = FLASH_ERASE_SECTOR;
		pv1.reserved = 0;
		pv1.flag = 0;
		pv1.params = p;
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_ERASE, 1, &pv1, sizeof(pv1), NULL, 0);
	} else {
		rv = gECProtocol->SendCommand(EC_CMD_FLASH_ERASE, 0, &p, sizeof(p), NULL, 0);
	}
	return rv;
}
