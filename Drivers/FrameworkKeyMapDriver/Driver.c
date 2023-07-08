#include <Uefi.h>

#include <Library/CrosECLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/CrosEC.h>

EFI_CROSEC_PROTOCOL* gECProtocol = NULL;

#define EC_CMD_UPDATE_KEYBOARD_MATRIX 0x3E0C
struct keyboard_matrix_map {
	uint8_t row;
	uint8_t col;
	uint16_t scanset;
} __ec_align1;
struct ec_params_update_keyboard_matrix {
	uint32_t num_items;
	uint32_t write;
	struct keyboard_matrix_map scan_update[32];
} __ec_align1;

EFI_STATUS
EFIAPI
FrameworkKeyMapDriverEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_SUCCESS;
	struct ec_params_update_keyboard_matrix request = {
		.num_items = 1,
		.write = 1,
		.scan_update =
			{
				{
					.row = 4,
					.col = 4,
					.scanset = 0x0076,
				},
			},
	};
	char response[256];

	Status = gBS->LocateProtocol(&gEfiCrosECProtocolGuid, NULL, (VOID**)&gECProtocol);
	if(EFI_ERROR(Status)) {
		goto Out;
	}

	gECProtocol->SendCommand(EC_CMD_UPDATE_KEYBOARD_MATRIX, 0, &request, sizeof(request), response, 256);

Out:
	if(Status == EFI_SUCCESS) {
		// "Initializing" drivers can safely return ABORTED to indicate that they have no further work to do
		Status = EFI_ABORTED;
	}
	return Status;
}
