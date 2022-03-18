#include <Library/CrosECLib.h>
#include <Library/UefiLib.h>

static const CHAR16* mEcErrorMessages[] = {
	L"success",
	L"invalid command",
	L"error",
	L"invalid param",
	L"access denied",
	L"invalid response",
	L"invalid version",
	L"invalid checksum",
	L"in progress",
	L"unavailable",
	L"timeout",
	L"overflow",
	L"invalid header",
	L"request truncated",
	L"response too big",
	L"bus error",
	L"busy",
	L"invalid header version",
	L"invalid header crc",
	L"invalid data crc",
	L"dup unavailable",
};

void PrintECResponse(int rv) {
	if(rv >= 0)
		return;
	if(rv < -EECRESULT)
		rv += EECRESULT;
	Print(L"%d (%s)", -rv, rv >= -EC_RES_DUP_UNAVAILABLE ? mEcErrorMessages[-rv] : L"<unknown error>");
}
