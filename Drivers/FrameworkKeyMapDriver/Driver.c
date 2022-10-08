#include <Uefi.h>
#include <Library/CrosECLib.h>

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
	ECSendCommandLPCv3(EC_CMD_UPDATE_KEYBOARD_MATRIX, 0, &request, sizeof(request), response, 256);
	return EFI_ABORTED;
}
