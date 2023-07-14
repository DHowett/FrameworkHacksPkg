#include <Uefi.h>
#include <Library/CrosECLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/CrosEC.h>
#include <Protocol/RealTimeClock.h>

EFI_CROSEC_PROTOCOL* gECProtocol = NULL;

#if defined(_MSC_VER)
#define MS_PUSH_PACK __pragma(pack(push, 1))
#define MS_POP_PACK  __pragma(pack(pop))
#define GNUC_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#define MS_PUSH_PACK
#define MS_POP_PACK
#define GNUC_PACKED __attribute__((packed))
#endif

MS_PUSH_PACK
struct GNUC_PACKED EfiDate {
	UINT16 Year;
	UINT8 Month, Day, Hour, Minute, Second;
} MS_POP_PACK;

MS_PUSH_PACK
struct GNUC_PACKED SavedPanicInfo {
	UINT8 DriverVersion;
	struct EfiDate Date;
	CHAR8 ECVersion[32];
} MS_POP_PACK;

MS_PUSH_PACK
struct GNUC_PACKED PartialECPanicData {
	UINT8 Arch;
	UINT8 Version;
	UINT8 Flags;
} MS_POP_PACK;

#define PARTIAL_PANIC_DATA_FLAG_OLD_HOSTCMD (1 << 2)

// {034354F6-0F68-487A-8B3A-1B2F88804276}
static EFI_GUID mCrosECSavedPanicInfoGuid = {0x34354f6,
                                             0xf68,
                                             0x487a,
                                             {0x8b, 0x3a, 0x1b, 0x2f, 0x88, 0x80, 0x42, 0x76}};

EFI_STATUS
EFIAPI
EntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_ABORTED;
	struct SavedPanicInfo* Saved = NULL;
	EFI_TIME CurrentTime;
	EFI_TIME_CAPABILITIES CurrentTimeCaps;
	CHAR8 BuildInfo[255];
	INTN PanicSize, FinalSize;
	INTN rv;

	Status = gBS->LocateProtocol(&gEfiCrosECProtocolGuid, NULL, (VOID**)&gECProtocol);
	if(EFI_ERROR(Status)) {
		goto Out;
	}

	PanicSize = 255;
	FinalSize = sizeof(struct SavedPanicInfo) + PanicSize;

	Status = gRT->GetTime(&CurrentTime, &CurrentTimeCaps);
	if(EFI_ERROR(Status)) {
		goto Out;
	}

	Status = gBS->AllocatePool(EfiBootServicesData, FinalSize, (VOID**)(&Saved));
	if(EFI_ERROR(Status)) {
		goto Out;
	}

	ZeroMem(Saved, FinalSize);

	Saved->DriverVersion = 1;
	Saved->Date.Year = CurrentTime.Year;
	Saved->Date.Month = CurrentTime.Month;
	Saved->Date.Day = CurrentTime.Day;
	Saved->Date.Hour = CurrentTime.Hour;
	Saved->Date.Minute = CurrentTime.Minute;
	Saved->Date.Second = CurrentTime.Second;

	rv = gECProtocol->SendCommand(EC_CMD_GET_BUILD_INFO, 0, NULL, 0, BuildInfo, sizeof(BuildInfo));
	if(rv < 0) {
		Status = EFI_DEVICE_ERROR;
		goto Out;
	}

	CopyMem(Saved->ECVersion, BuildInfo, sizeof(Saved->ECVersion) - 1);

	rv = PanicSize = gECProtocol->SendCommand(EC_CMD_GET_PANIC_INFO, 0, NULL, 0, (VOID*)(Saved + 1), PanicSize);
	if(rv < 0) {
		Status = EFI_DEVICE_ERROR;
		goto Out;
	}

	if(rv == 0) {
		// There was no panic, so we should leave the existing one alone.
		Status = EFI_SUCCESS;
		goto Out;
	}

	rv = PanicSize = gECProtocol->SendCommand(EC_CMD_GET_PANIC_INFO, 0, NULL, 0, (VOID*)(Saved + 1), PanicSize);
	if((((struct PartialECPanicData*)((VOID*)(Saved + 1)))->Flags & PARTIAL_PANIC_DATA_FLAG_OLD_HOSTCMD) != 0) {
		// This panic was already reported by the host command interface, so
		// it is not new. Don't update it (as that would refresh the date and version)
		Status = EFI_SUCCESS;
		goto Out;
	}

	FinalSize = sizeof(struct SavedPanicInfo) + PanicSize;

	Status = gRT->SetVariable(
		L"SavedPanic", &mCrosECSavedPanicInfoGuid,
		EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE, FinalSize,
		(VOID*)Saved);

	if(EFI_ERROR(Status)) {
		goto Out;
	}

Out:
	if(Saved) {
		gBS->FreePool(Saved);
	}

	if(EFI_SUCCESS == Status) {
		// This driver doesn't need to remain loaded, so it should return
		// EFI_ABORTED.
		Status = EFI_ABORTED;
	}

	return Status;
}
