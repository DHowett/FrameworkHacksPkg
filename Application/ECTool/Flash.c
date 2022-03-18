#include <Library/BaseMemoryLib.h>
#include <Library/CrosECLib.h>
#include <Library/UefiLib.h>

#include "EC.h"
#include "Flash.h"

enum ec_status flash_read(int offset, int size, char* buffer) {
	struct ec_params_flash_read p;
	const int chunk = G_EC_MAX_RESPONSE;
	char buf[G_EC_MAX_RESPONSE] = {0};
	for(int i = 0; i < size; i += chunk) {
		p.offset = offset + i;
		p.size = MIN(size - i, chunk);
		int rv = ECSendCommandLPCv3(EC_CMD_FLASH_READ, 0, &p, sizeof(p), buf, p.size);
		if(rv < 0) {
			Print(L"*** FLASH READ **FAILED** AT OFFSET %x: %d\n", p.offset, rv);
			return rv;
		}
		CopyMem(buffer + i, buf, p.size);
	}
	return 0;
}

enum ec_status flash_write(int offset, int size, char* buffer) {
	struct ec_response_flash_info_1 flashInfo;
	int rv = ECSendCommandLPCv3(EC_CMD_FLASH_INFO, 1, NULL, 0, &flashInfo, sizeof(flashInfo));
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
		rv = ECSendCommandLPCv3(EC_CMD_FLASH_WRITE, 1, p, sizeof(*p) + p->size, NULL, 0);
		if(rv < 0) {
			Print(L"*** FLASH WRITE **FAILED** AT OFFSET %x: %d\n", p->offset, rv);
			return rv;
		}
	}
	return 0;
}

enum ec_status flash_erase(int offset, int size) {
	struct ec_params_flash_erase p;
	p.offset = offset;
	p.size = size;
	return ECSendCommandLPCv3(EC_CMD_FLASH_ERASE, 0, &p, sizeof(p), NULL, 0);
}
