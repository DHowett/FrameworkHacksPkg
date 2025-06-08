DefinitionBlock ("goog0004.asl", "SSDT", 2, "GOOG", "GOOG0004", 0x20250607)
{
    Scope (_SB)
    {
        Device (CREC)
        {
            Name (_HID, "GOOG0004")  // _HID: Hardware ID
            Name (_UID, One)  // _UID: Unique ID
            Name (_DDN, "EC Command Device")  // _DDN: DOS Device Name
            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                Return (0x0F)
            }
        }
    }
}

