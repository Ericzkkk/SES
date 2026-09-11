# Turbidity + depth sensor package

Version: **1.0.1**. Documentation edited **2026-09-11T14:12:27+10:00 (Australia/Sydney)**. See [workflow summary](WORKFLOW_SUMMARY.md).

Hardware: BoSL Board 0.5.x with SIM7000G, turbidity and depth sensors using separate reset lines. Open `BoSL_Turb_Depth_SeperateReset_HKU_LPWA/BoSL_Turb_Depth_SeperateReset_HKU_LPWA.ino` in Arduino IDE; keep the sketch's folder and filename matching.

## Configure before uploading

```cpp
#define APN "telstra.iot"  // Example: tested M2M One / Telstra Australia subscription
#define MCCMNC "50501"    // Exact operator; "" permits automatic operator selection
#define CELLULAR_MODE 2   // 1 = LTE-M only; 2 = NB-IoT only
```

There is no automatic LTE-M/NB-IoT fallback. A non-empty MCCMNC restricts the operator. Both home and roaming registration are accepted, subject to SIM entitlement. Band masks are restored to the full supported lists; production does not run lengthy COPS network discovery.

These are Australian example settings, not Hong Kong settings. Confirm the SIM provider's APN, operator and chosen radio technology before deployment. Also set SITE_DIR, SITE_ID, sensor configuration and sampling/upload intervals. The saved working sketch currently uses testing/Eric_Testing, 1-minute sampling and 6-minute averaging; these are not customer deployment defaults.

Use the wiring notes in the sketch. Connecting the debug pin A15 to GND skips registration and HTTP uploads. Serial baud: 9600.

## Build

Arduino IDE > 2.3.2; board FQBN `BoSL Boards AVR B:avr:bosl0v5` (board package 1.2.2). Verified with MCP7940 1.2.0, SD (MegaCore) 1.0.7, Wire 1.1 and SPI 1.0.

Both selected modes compile for BoSL 0.5.x and 11 host-C parser regressions pass. A separate Australian test confirmed Telstra NB-IoT B28 and HTTP 200. The user reported client success on 2026-09-11, but did not provide the final SIM/APN/operator, firmware version or cycle logs; do not infer independent validation of both RATs or all failure paths.

## Changes and limitations

See [the complete before/after table](BoSL_Turb_Depth_SeperateReset_HKU_LPWA/CHANGES.md) and [release history](CHANGELOG.md).

Registration/bearer/HTTP results are verified; unresolved AT commands require reset before further commands. Sensor calculations, SD formats, payload construction and physical modem power timing are preserved. Difficult registration can still block sampling for minutes. The existing upload uses unencrypted HTTP and HTTP 200 is not proof that the server stored the measurement. No claim of exactly-once delivery is made.

For later releases, update this package's VERSION, sketch header and changelog, then create a new `turbidity-depth-vX.Y.Z` tag. Do not copy a new version over a published tag.

## Field power behaviour

After every upload attempt, the code attempts RF shutdown and modem power-off using the existing status-pin/retry logic. The modem is not kept registered and no PSM/eDRX is configured. At the next upload it powers on and registers again. The MCU sleeps between sensor readings. Battery standby current and reliable shutdown should be measured on the actual board.
