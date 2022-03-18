#pragma once

/// Framework Specific
/* Configure the behavior of the flash notify */
#define EC_CMD_FLASH_NOTIFIED 0x3E01

enum ec_flash_notified_flags {
	/* Enable/Disable power button pulses for x86 devices */
	FLASH_ACCESS_SPI = 0,
	FLASH_FIRMWARE_START = BIT(0),
	FLASH_FIRMWARE_DONE = BIT(1),
	FLASH_ACCESS_SPI_DONE = 3,
	FLASH_FLAG_PD = BIT(4),
};

struct ec_params_flash_notified {
	/* See enum ec_flash_notified_flags */
	uint8_t flags;
} __ec_align1;
/// End Framework Specific

enum ec_status flash_read(int offset, int size, char* buffer);
enum ec_status flash_write(int offset, int size, char* buffer);
enum ec_status flash_erase(int offset, int size);
