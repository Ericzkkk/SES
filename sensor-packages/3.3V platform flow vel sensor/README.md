# 3.3V platform flow vel sensor

BoSL 0.5.x logger integration for the MDS-P2510 RS-485 velocity/depth sensor.
This name refers to the logger/serial-interface platform: **the sensor is powered at 12 V, not 3.3 V**.

## Open and build

Open [BoSL_MDS_P2510_Uploader.ino](BoSL_MDS_P2510_Uploader/BoSL_MDS_P2510_Uploader.ino), keeping the four companion .h/.cpp files in its matching sketch folder. Select BoSL Board 0.5.x (bosl-avr-b:avr:bosl0v5). Dependencies used: BoSL AVR core 1.2.2, MCP7940 1.2.0, bundled SD (MegaCore) 1.0.7 / Wire / SPI.

## Current snapshot settings

| Setting | Current value |
|---|---|
| Sensor UART | Serial2, 9600 baud, 8N1, address 1 |
| Modem UART | Serial1 |
| Sensor startup allowance | 20 seconds per power cycle |
| Scan / averaging interval | 2 minutes / 2 minutes |
| LOG_TO_WEB | true |
| CONSTANT_LOGGING | true |
| Serial Monitor | 9600 baud; first scan and every loop scan printed |
| SD files | flowraw.csv, flowavg.csv |

These are the user's current test settings, not the earlier six-minute configuration. Review site/APN/operator settings at the top before deploying; the current sketch retains testing / Eric_Testing / simbase / 50501. Uploading is enabled on boot. No live upload was performed to publish this snapshot.

## Wiring and power

- VPP_SW -> MT3608 input positive; MT3608 regulated 12-V output -> sensor positive.
- Common ground for BoSL, sensor, boost converter and RS-485 converter.
- 3V3_SWB -> auto-direction RS-485 converter supply; UART2 TX/RX -> converter TTL interface, A/B -> sensor A/B.
- EN_SWVPP controls sensor/MT3608 input power; EN_SWB controls converter power. Do not connect the MT3608 input to an enable GPIO.
- Both rails turn on for the startup allowance and two Modbus reads, then turn off before SD/network work, including when reads fail.
- First reading occurs in setup; setup attempts modem network-time synchronization and initial upload when LOG_TO_WEB=true. A15/debug pin has no effect.
- Long blocking modem registration and watchdog sleep affect actual wall-clock timing. Capacitors can delay the physical 12-V decay after software shutdown.

## Measurements and HTTP fields

Current source uploads **Depth in metres** and **Velocity in m/s**, plus Batt and CSQ, using WriteMe_v2.php. This is the user's latest mapping, replacing the earlier Turbw/mm mapping. The PHP endpoint must accept these field names and units; server storage has not been verified.

Each field requires a complete CRC-checked response with matching address/function/length and a finite decoded float. Depth reads PDU 0x0000; velocity reads 0x0002, two registers each, ABCD IEEE-754 float. No physical-range/calibration/immersion validation is implied.

Zero is valid. Invalid samples do not enter averages. Missing SD values are blank with validity/count columns; a partially invalid HTTP sample uses -99.99 for the missing field. If both fields are invalid, HTTP data transmission is skipped. Network-time sync may still run in setup. The current constant-logging setting makes each valid averaging window eligible for upload.

## Validation and remaining issues

The user previously obtained valid readings after replacing an RS-485 converter. Intermittent CRC/communication failures were subsequently reported with power cycling; the cause remains unresolved. The current parser does not resynchronize past unexpected leading bytes. The 20-second startup allowance is configurable, not an established manufacturer startup specification.

Arduino compilation is checked for this publication, but this is **not a claim of validated unattended operation**. Verify cold starts, rail voltages, consecutive valid reads, saved server fields/units and failed-read behavior before field use. Modem time-sync and HTTP success are separate. No volumetric flow calculation is included.

## Change history

[Historical notes](history/) preserve the integration and power/startup/debug/display changes. Their old settings and line numbers can be superseded. Firmware files in this snapshot are unchanged from the current local source. Local computer paths were removed from published notes; an invalid generated diff, unrelated projects, raw field logs, PDFs and build outputs were excluded.
