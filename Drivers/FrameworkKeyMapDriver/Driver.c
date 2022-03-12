#include <Uefi.h>
#include <Library/CrosECLib.h>

EFI_STATUS
EFIAPI
FrameworkKeyMapDriverEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
	 //0x3E0C d1,d1,b4,b4,w76
	char buffer[12] = {0x01, 0x00, 0x00, 0x00,
		           0x01, 0x00, 0x00, 0x00,
			   0x04, 0x04, 0x76, 0x00};
	char response[256];
	ECSendCommandLPCv3(0x3E0C, 0, buffer, 12, response, 256);
	return EFI_ABORTED;
}
