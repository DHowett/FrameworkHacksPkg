#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/AcpiTable.h>

#include <Guid/Acpi.h>

extern CHAR8 frmwc004_aml_code[];
extern CHAR8 goog0004_aml_code[];

EFI_STATUS
EFIAPI
AcpiPatcherEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_SUCCESS;
	EFI_ACPI_DESCRIPTION_HEADER* Table = (EFI_ACPI_DESCRIPTION_HEADER*)frmwc004_aml_code;
	UINTN TableSize;
	EFI_ACPI_TABLE_PROTOCOL* AcpiTable;
	UINTN TableKey = 0;

	if(!_gPcd_FixedAtBuild_PcdUseFrameworkECAcpiNode) {
		Table = (EFI_ACPI_DESCRIPTION_HEADER*)goog0004_aml_code;
	}

	TableSize = ((EFI_ACPI_DESCRIPTION_HEADER*)Table)->Length;

	Status = gBS->LocateProtocol(&gEfiAcpiTableProtocolGuid, NULL, (VOID**)&AcpiTable);
	if(Status != EFI_SUCCESS) {
		goto Out;
	}

	Status = AcpiTable->InstallAcpiTable(AcpiTable, Table, TableSize, &TableKey);
	if(Status != EFI_SUCCESS) {
		goto Out;
	}

Out:
	return Status;
}
