#ifndef __EFI_CROSEC_PROTOCOL_H__
#define __EFI_CROSEC_PROTOCOL_H__

#define EFI_CROSEC_PROTOCOL_GUID                                                               \
	{                                                                                      \
		0x76945038, 0x21bf, 0x465c, { 0xb9, 0x11, 0x95, 0x61, 0xfc, 0xe0, 0x30, 0xa4 } \
	}

typedef EFI_STATUS(EFIAPI* EFI_CROSEC_XFER)(
	// parameters
);

typedef EFI_STATUS(EFIAPI* EFI_CROSEC_READMEM)(
	// parameters
);

typedef struct _EFI_CROSEC_PROTOCOL {
	EFI_CROSEC_XFER SendCommand;
	EFI_CROSEC_READMEM ReadMem;
} EFI_CROSEC_PROTOCOL;

extern EFI_GUID gEfiCrosECProtocolGuid;

#endif
