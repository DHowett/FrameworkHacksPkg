#include <Uefi.h>

#include <Library/CrosECLib.h>
#include <Library/HashLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/CrosEC.h>

EFI_CROSEC_PROTOCOL* gECProtocol = NULL;

#define EC_CMD_CHASSIS_INTRUSION 0x3E09

struct ec_params_chassis_intrusion_control {
	uint8_t clear_magic;
	uint8_t clear_chassis_status;
} __ec_align1;

struct ec_response_chassis_intrusion_control {
	uint8_t chassis_ever_opened;
	uint8_t coin_batt_ever_remove;
	uint8_t total_open_count;
	uint8_t vtr_open_count;
} __ec_align1;

EFI_STATUS
EFIAPI
EntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_ABORTED;
	struct ec_params_chassis_intrusion_control request = {};
	struct ec_response_chassis_intrusion_control response = {};
	TPML_DIGEST_VALUES DigestList;
	int rv;

	Status = gBS->LocateProtocol(&gEfiCrosECProtocolGuid, NULL, (VOID**)&gECProtocol);
	if(EFI_ERROR(Status)) {
		goto Out;
	}

	rv = gECProtocol->SendCommand(EC_CMD_CHASSIS_INTRUSION, 0, &request, sizeof(request), &response, sizeof(response));
	if(rv < 0) {
		Status = EFI_DEVICE_ERROR;
		goto Out;
	}

	Status = HashAndExtend(6, &response, sizeof(response), &DigestList);
	if(EFI_SUCCESS == Status) {
		// This driver doesn't need to remain loaded, so it should return
		// EFI_ABORTED.
		Status = EFI_ABORTED;
	}
Out:
	return Status;
}
