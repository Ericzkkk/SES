> Historical development notes. Settings, line numbers and local-file references may be superseded. Use the [current package README](../README.md) and current sketch as authoritative. The old ORIGINAL_TO_MDS.diff was excluded because it contained a script error, not a valid diff.

# Current power-control update

> Subsequent setup changes are documented in STARTUP_CHANGES.md. The first
> reading now occurs in setup; the sketch line numbers below are historical.

This update is based on the user's current sketch, preserving its 10-second
startup setting, six-minute scan interval, upload mapping and other edits.
It supersedes the continuous-power notes and old line numbers in README.md,
CHANGES.md and ORIGINAL_TO_MDS.diff.

## Required connections

- VPP_SW -> MT3608 IN+; MT3608 OUT+ -> sensor supply positive.
- Common GND for MT3608, sensor, converter and BoSL.
- 3V3_SWB -> RS485 converter supply; UART2 and A/B remain unchanged.
- Do not connect MT3608 input to an enable GPIO or leave it on always-on VBAT.
- EN_SWVPP controls VPP_SW; EN_SWB controls 3V3_SWB.

## Changed lines in this update

| File | Current lines | Change |
|---|---|---|
| BoSLFlow.cpp | 3 | Require EN_SWVPP in the board pin definitions. |
| BoSLFlow.cpp | 17-19 | Enable VPP_SW for the MT3608/sensor at the start of each measurement cycle. |
| BoSLFlow.cpp | 44-46 | Disable VPP_SW immediately after UART shutdown and EN_SWB off. |
| BoSLFlow.h | 24-29 | Document ownership of both power rails. |
| BoSL_MDS_P2510_Uploader.ino | 3, 6-7, 76 | Correct wiring and rail-ownership comments only. |

Unchanged current settings and call sites:

- Sketch line 20: SENSOR_STARTUP_MS = 10000UL.
- Sketch line 27: LOOP_INTERVAL_MINUTES = 6.
- Sketch line 845: both rails off during setup.
- Sketch lines 993-996: rails on -> wait -> read velocity and depth -> rails off.
  Shutdown happens even when a read returns a timeout or invalid frame, before
  formatting, SD writes or network activity.

The existing loop targets six-minute start-to-start cycles; long blocking modem
operations can still delay the next measurement. The first cycle starts after
setup, not six minutes after boot. Existing debug-pin mode uses the same interval.
Removing input power is immediate in software; the 12-V output can take time to
decay because of capacitance.

## Verification

Compiled for bosl-avr-b:avr:bosl0v5 with LOG_TO_WEB=true: 44,414 bytes flash,
2,475 bytes static RAM. No hardware flash or electrical test was performed.
Check 12 V at the sensor during the read window and its decay after shutdown;
confirm both readings remain valid after repeated cold starts. The retained
10-second startup interval is the user's setting, not a verified sensor limit.
