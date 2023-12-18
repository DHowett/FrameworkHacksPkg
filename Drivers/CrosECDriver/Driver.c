#include <Uefi.h>

#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/CrosEC.h>
#include <Protocol/DriverBinding.h>

#include <Library/CrosECLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS EFIAPI DriverSupported(IN EFI_DRIVER_BINDING_PROTOCOL* This,
                                  IN EFI_HANDLE ControllerHandle,
                                  IN EFI_DEVICE_PATH_PROTOCOL* RemainingDevicePath OPTIONAL);
EFI_STATUS EFIAPI DriverStart(IN EFI_DRIVER_BINDING_PROTOCOL* This,
                              IN EFI_HANDLE ControllerHandle,
                              IN EFI_DEVICE_PATH_PROTOCOL* RemainingDevicePath OPTIONAL);
EFI_STATUS EFIAPI DriverStop(IN EFI_DRIVER_BINDING_PROTOCOL* This,
                             IN EFI_HANDLE ControllerHandle,
                             IN UINTN NumberOfChildren,
                             IN EFI_HANDLE* ChildHandleBuffer OPTIONAL);

EFI_STATUS EFIAPI GetDriverName(IN EFI_COMPONENT_NAME2_PROTOCOL* This, IN CHAR8* Language, OUT CHAR16** DriverName) {
	*DriverName = L"CrosECDriver";
	return EFI_SUCCESS;
}

EFI_STATUS EFIAPI GetControllerName(IN EFI_COMPONENT_NAME2_PROTOCOL* This,
                                    IN EFI_HANDLE ControllerHandle,
                                    IN EFI_HANDLE ChildHandle,
                                    IN CHAR8* Language,
                                    OUT CHAR16** ControllerName) {
	return EFI_UNSUPPORTED;
}

#define CROSEC_VERSION 0x10
EFI_DRIVER_BINDING_PROTOCOL gBinding = {DriverSupported, DriverStart, DriverStop, CROSEC_VERSION, NULL, NULL};

GLOBAL_REMOVE_IF_UNREFERENCED
EFI_COMPONENT_NAME_PROTOCOL gComponentName = {(EFI_COMPONENT_NAME_GET_DRIVER_NAME)GetDriverName,
                                              (EFI_COMPONENT_NAME_GET_CONTROLLER_NAME)GetControllerName, "eng"};

GLOBAL_REMOVE_IF_UNREFERENCED
EFI_COMPONENT_NAME2_PROTOCOL gComponentName2 = {GetDriverName, GetControllerName, "en"};

EFI_STATUS EFIAPI DriverSupported(IN EFI_DRIVER_BINDING_PROTOCOL* This,
                                  IN EFI_HANDLE ControllerHandle,
                                  IN EFI_DEVICE_PATH_PROTOCOL* RemainingDevicePath OPTIONAL) {
	return EFI_SUCCESS;
}

EFI_STATUS EFIAPI DriverStart(IN EFI_DRIVER_BINDING_PROTOCOL* This,
                              IN EFI_HANDLE ControllerHandle,
                              IN EFI_DEVICE_PATH_PROTOCOL* RemainingDevicePath OPTIONAL) {
	EFI_CROSEC_PROTOCOL* gECProtocol = NULL;
	EFI_STATUS Status = EFI_UNSUPPORTED;
	Status = gBS->LocateProtocol(&gEfiCrosECProtocolGuid, NULL, (VOID**)&gECProtocol);
	// Our status is entirely contingent upon whether we find the CrosEC Protocol
	return Status;
}

EFI_STATUS EFIAPI DriverStop(IN EFI_DRIVER_BINDING_PROTOCOL* This,
                             IN EFI_HANDLE ControllerHandle,
                             IN UINTN NumberOfChildren,
                             IN EFI_HANDLE* ChildHandleBuffer OPTIONAL) {
	return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
DriverEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_UNSUPPORTED;
	Status = EfiLibInstallDriverBindingComponentName2(ImageHandle, SystemTable, &gBinding, ImageHandle,
	                                                  &gComponentName, &gComponentName2);
	ASSERT_EFI_ERROR(Status);
	return Status;
}
