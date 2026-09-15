> Historical development notes. Settings, line numbers and local-file references may be superseded. Use the [current package README](../README.md) and current sketch as authoritative. The old ORIGINAL_TO_MDS.diff was excluded because it contained a script error, not a valid diff.

# Display every sensor scan

| Sketch lines | Change |
|---|---|
| 845 | Update comment: the existing result-printing helper is shared by setup and loop. |
| 986-993 | SCAN-03: print velocity (m/s), depth (m), validity and readable status after every loop measurement. |

The existing first-scan display in setup remains. Output uses the actual raw
sample and occurs after both sensor power rails have been disabled, before
SD logging/averaging/upload decisions. INVALID remains distinct from zero.
No extra reads, raw-byte dumps or changes to timing/upload rules.

Preserved current settings: 20-second warmup, two-minute scan interval,
LOG_TO_WEB=true. Open Serial Monitor at 9600 baud after flashing.
Older change-document line numbers are historical.
