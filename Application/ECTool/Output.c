#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/SimpleTextOut.h>

#include "Output.h"

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* Sto;

#define MAX_PRINT_BUFFER_SIZE 4096

UINTN EFIAPI PrintWithAttributes(UINTN Attribute, IN CONST CHAR16* Format, ...) {
	EFI_STATUS Status = EFI_SUCCESS;
	UINTN Return = 0;
	CHAR16* Buffer = NULL;
	UINTN BufferSize;
	UINTN PriorAttribute = 0xFFFFFFFF;
	VA_LIST Marker;

	if(!Sto) {
		Status = gBS->HandleProtocol(gST->ConsoleOutHandle, &gEfiSimpleTextOutProtocolGuid, (VOID**)&Sto);

		if(EFI_ERROR(Status)) {
			goto Out;
		}
	}

	// Cribbed from UefiLib!InternalPrint
	BufferSize = (MAX_PRINT_BUFFER_SIZE + 1) * sizeof(CHAR16);

	Buffer = (CHAR16*)AllocatePool(BufferSize);
	if(!Buffer)
		goto Out;

	VA_START(Marker, Format);
	Return = UnicodeVSPrint(Buffer, BufferSize, Format, Marker);
	VA_END(Marker);

	PriorAttribute = Sto->Mode->Attribute;
	Sto->SetAttribute(Sto, Attribute);
	Sto->OutputString(Sto, Buffer);
Out:
	if(PriorAttribute != 0xFFFFFFFF && Sto)
		Sto->SetAttribute(Sto, PriorAttribute);
	if(Buffer)
		FreePool(Buffer);
	return Return;
}
