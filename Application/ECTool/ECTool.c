#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/CrosECLib.h>

#include <Protocol/Shell.h>
#include <Protocol/ShellParameters.h>

#define EC_CMD_FLASH_INFO 0x0010
#define EC_VER_FLASH_INFO 2

typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;

struct ec_response_flash_info_1 {
	/* Version 0 fields; see above for description */
	uint32_t flash_size;
	uint32_t write_block_size;
	uint32_t erase_block_size;
	uint32_t protect_block_size;

	/* Version 1 adds these fields: */
	uint32_t write_ideal_size;
	uint32_t flags;
} __ec_align4;

/*
 * Read flash
 *
 * Response is params.size bytes of data.
 */
#define EC_CMD_FLASH_READ 0x0011

/**
 * struct ec_params_flash_read - Parameters for the flash read command.
 * @offset: Byte offset to read.
 * @size: Size to read in bytes.
 */
struct ec_params_flash_read {
	uint32_t offset;
	uint32_t size;
} __ec_align4;

/* Write flash */
#define EC_CMD_FLASH_WRITE 0x0012
#define EC_VER_FLASH_WRITE 1

struct ec_params_flash_write {
	uint32_t offset;
	uint32_t size;
	/* Followed by data to write */
} __ec_align4;

UINTN              Argc;
CHAR16             **Argv;
EFI_SHELL_PROTOCOL *mShellProtocol = NULL;

EFI_STATUS
GetArg (
  VOID
  )
{
	EFI_STATUS                     Status;
	EFI_SHELL_PARAMETERS_PROTOCOL  *ShellParameters;

	Status = gBS->HandleProtocol(
		gImageHandle,
		&gEfiShellParametersProtocolGuid,
		(VOID **)&ShellParameters
	);
	if (EFI_ERROR (Status)) {
		return Status;
	}

	Argc = ShellParameters->Argc;
	Argv = ShellParameters->Argv;
	return EFI_SUCCESS;
}

EFI_STATUS DumpFlash() {
	SHELL_FILE_HANDLE File;
	int rv = 0;

	char flashNotify;
	flashNotify = 1; // Lock down for flashing
	rv = ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	flashNotify = 0; // Enable SPI access
	rv = ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	if(rv < 0) {
		Print(L"Failed to unlock flash: %d\n", rv);
		return EFI_UNSUPPORTED;
	}

	char* FlashBuffer = AllocatePool(1048576);
	int i = 0;
	const int maxsize = EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_response);
	struct ec_params_flash_read r;
	while(i < 1048576) {
		Print(L".");
		r.offset = i;
		r.size = MIN(maxsize, 1048576 - i);
		rv = ECSendCommandLPCv3(EC_CMD_FLASH_READ, 0, &r, sizeof(r), FlashBuffer + i, r.size);
		if (rv < 0) {
			Print(L"\nEC Error reading offset %u: %d\n", i, rv);
			break;
		}
		i += r.size;
	}
	Print(L"\n");

	// re-lock flash
	flashNotify = 2; // Flash done
	ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	flashNotify = 3; // SPI access done
	int lrv = ECSendCommandLPCv3(0x3E01, 0, &flashNotify, 1, &flashNotify, 0);
	if(lrv < 0) {
		Print(L"Failed to re-lock flash: %d\n", lrv);
	}

	if (rv >= 0) {
		mShellProtocol->CreateFile(L"fs0:\\flash.bin", 0, &File);
		UINTN BufSz = 1048576;
		mShellProtocol->WriteFile(File, &BufSz, FlashBuffer);
		mShellProtocol->CloseFile(File);
		Print(L"Dumped to fs0:\\flash.bin\n");
	}

	FreePool(FlashBuffer);
	return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
	EFI_STATUS Status = EFI_SUCCESS;

	GetArg();

	Status = gBS->LocateProtocol(&gEfiShellProtocolGuid, NULL, (VOID**)&mShellProtocol);
	if (EFI_ERROR(Status)) {
		return Status;
	}

	DumpFlash();
	return Status;
}
