> Historical development notes. Settings, line numbers and local-file references may be superseded. Use the [current package README](../README.md) and current sketch as authoritative. The old ORIGINAL_TO_MDS.diff was excluded because it contained a script error, not a valid diff.

# Web-enable control update

LOG_TO_WEB is now the only enable/disable switch for startup networking and
scheduled web uploads. A15 is not configured or read by this sketch.

| Previous sketch lines | Current lines | Change |
|---|---|---|
| 8, 22 | 8, 22 | Document LOG_TO_WEB as the only web-enable switch; preserve true. |
| 77, 863 | Removed | Remove DEBUG_PIN definition and input/pullup configuration. |
| 882-901 | 880-896 | Remove startup debug-pin condition. LOG_TO_WEB=true calls logToWeb for network time and initial upload; SD setup/logging is independent. |
| 935-949 | 930 | Remove debug-pin branch and its early return from loop. |

Preserved: first measurement in setup, user's 1-second startup setting,
six-minute scan interval, both power rails off after measurement, current
upload field mapping, error checks and variable-upload scheduling.

LOG_TO_WEB=false disables networking but retains sensor reads and SD logging.
LOG_TO_WEB=true enables startup time synchronization/upload and later uploads
according to shouldLogToWeb(). Invalid readings, modem/network failures and
HTTP failures can still prevent a successful upload; no validity safeguards
were removed. This change does not force an upload every six minutes when
CONSTANT_LOGGING=false.

This supersedes the A15/debug-mode descriptions and line numbers in older
change documents. No firmware was flashed or live HTTP request sent.
