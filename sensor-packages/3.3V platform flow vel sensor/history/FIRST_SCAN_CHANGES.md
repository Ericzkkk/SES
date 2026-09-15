> Historical development notes. Settings, line numbers and local-file references may be superseded. Use the [current package README](../README.md) and current sketch as authoritative. The old ORIGINAL_TO_MDS.diff was excluded because it contained a script error, not a valid diff.

# Setup first-scan display

| Current sketch lines | Change |
|---|---|
| 845-877 | SCAN-01: add printSetupReading() for value, validity, readable error status and Modbus exception code. |
| 913-926 | SCAN-02: print the first setup velocity/depth sample and effective LOG_TO_WEB setting, before network activity. Warn when both fields are invalid and HTTP data upload will be skipped. |

Printed once at startup, using the actual BoSLFlow::Sample; no extra sensor
poll is performed. Both power rails are already disabled when printing starts.
Depth is metres, velocity is m/s, with six decimal places. Invalid values are
printed as INVALID, not zero. Zero with valid=1/status=OK is a valid response.
No changes to sensor timing, upload gating, periodic reads or field mapping.

Use Serial Monitor at 9600 baud and reset the board after flashing. Earlier
change-document line numbers are historical after this insertion.
