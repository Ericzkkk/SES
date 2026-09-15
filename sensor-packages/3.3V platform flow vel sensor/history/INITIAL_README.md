> Historical development notes. Settings, line numbers and local-file references may be superseded. Use the [current package README](../README.md) and current sketch as authoritative. The old ORIGINAL_TO_MDS.diff was excluded because it contained a script error, not a valid diff.

# BoSL MDS-P2510 velocity + depth uploader

> FIRST SCAN: setup now prints velocity/depth, validity and readable error
> status before network activity. See FIRST_SCAN_CHANGES.md. Serial Monitor:
> 9600 baud. No extra measurement or power-on time is added by this display.

> LATEST: see WEB_CONTROL_CHANGES.md. A15/debug-pin handling has been removed.
> LOG_TO_WEB alone enables/disables networking; it is currently true.
> Current user startup wait is 1 second. Earlier debug-pin and default-setting
> descriptions below are historical.

> STARTUP UPDATE: see STARTUP_CHANGES.md. The first measurement is now in
> setup, followed by optional network-time sync/startup upload when LOG_TO_WEB
> is true (unless A15 local-only mode is active). Both rails are off during
> network activity. Earlier setup descriptions and line numbers are historical.

> POWER UPDATE: see POWER_CHANGES.md for the current VPP_SW/EN_SWVPP sensor
> supply and EN_SWB converter control. Current user settings are a 10-second
> startup wait and six-minute scan interval. The earlier continuous-power
> instructions and line numbers below describe the initial integration only.

Open **BoSL_MDS_P2510_Uploader.ino** in Arduino IDE, keeping all four companion .h/.cpp files in this folder. Select **BoSL Board 0.5.x**. This is a separate modified copy; the Desktop original is untouched.

## Before deployment

1. Review SITE_DIR and SITE_ID (preserved: testing / Eric_Testing), APN and MCCMNC. Use a distinct data series or a documented changeover to avoid mixing old turbidity and new velocity.
2. At sketch line 25, change `LOG_TO_WEB false` to `LOG_TO_WEB true` to enable cellular uploads. Default remains false for local SD testing, matching your original.
3. Leave A15 ungrounded for normal logging. Grounding A15 enables local-only readings: no SD logging or uploads.
4. Keep the working converter on EN_SWB / Serial2 and the MDS continuously supplied with regulated 12 V via VBAT/MT3608. The old depth/turbidity sensors and reset leads are not used.

## Upload mapping (legacy server format)

| Existing HTTP key | New content | Unit / missing value |
|---|---|---|
| Depth | MDS water depth/level | mm; sensor metres multiplied by 1000 |
| Turbw | MDS flow velocity | m/s, four decimals |
| Temp | No sensor measurement | -99.99 |
| EC | No sensor measurement | -99.99 |
| Turbwo | No sensor measurement | -99.99 |
| Batt, CSQ | Existing modem diagnostics | unchanged |

Example sensor data: depth 0.032318 m and velocity 0.1234 m/s becomes:

`&Temp=-99.99&Depth=32.318&EC=-99.99&Turbw=0.1234&Turbwo=-99.99`

The server may still call the velocity column Turbw/turbidity. Interpret/relabel it as velocity; do not apply turbidity calibration or subtract Turbwo. Depth is already sensor-reported water level, not an old raw pressure reading. Endpoint/schema compatibility beyond the retained query names has not been live-tested.

## Sampling and validity

- Serial2 at 9600/8N1, address 1; automatic direction, no DE/RE.
- One conservative 20-second startup wait in setup; sensor/converter remain powered through logger sleep. Neither old D13/D14 sensor resets nor ASCII A/V/S commands are used.
- Default scan interval: 1 minute; averages: 6 minutes; variable upload based on valid depth/velocity change or maximum 12 scan-minutes. First valid averaging window is eligible. Long modem operations can extend wall-clock intervals, as in the source.
- Per-field valid counts exclude CRC errors/timeouts/nonfinite values. A true zero remains valid. If both averages are invalid, no upload occurs; the SD still records the failed window. If only one is invalid, its HTTP value is -99.99.
- SD files: flowraw.csv and flowavg.csv. Depth is metres and velocity m/s in both, with separate validity/count columns. The distinct filenames prevent appending the new schema to old log.txt/web.txt files.
- Operational modem/SD console messages remain; there are no sensor TX/RX hex dumps. Debug-pin mode provides concise values/INVALID.

## Review changes

See **CHANGES.md** for a summary and all 52 exact changed-line blocks. **ORIGINAL_TO_MDS.diff** contains every added/removed line. Code sections are marked MDS-01 through MDS-24.

## Verification

Compiled with BoSL AVR core 1.2.2, MCP7940 1.2.0 and bundled SD (MegaCore) 1.0.7, using board identifier bosl-avr-b:avr:bosl0v5. Both LOG_TO_WEB=false and LOG_TO_WEB=true configurations were compiled. Web-enabled build: 44,370 bytes flash / 2,495 bytes static RAM. Static RAM excludes stack and dynamic String allocations.

No board was flashed, no sensor was hardware-tested in this turn and no HTTP request was sent. Verify the saved server row (including unit scaling and missing markers), not just an HTTP 200 response, before deployment. The original file's SHA-256 is recorded in CHANGES.md.
