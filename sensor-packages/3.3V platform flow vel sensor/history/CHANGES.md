> Historical development notes. Settings, line numbers and local-file references may be superseded. Use the [current package README](../README.md) and current sketch as authoritative. The old ORIGINAL_TO_MDS.diff was excluded because it contained a script error, not a valid diff.

# MDS-P2510 uploader: change table

> Historical initial-integration comparison. Subsequent user edits and power
> changes mean the line numbers below are not current. See POWER_CHANGES.md
> for the current power-control changes; ORIGINAL_TO_MDS.diff is historical too.

Original file was not edited:

`BoSL_Turb_Depth_SeperateReset_NSW.ino`

Original SHA-256: `6cb563c1f05a97347863c9cd31dba460f8964651f0d6c3f96789210ccf5f513b`.

Modified sketch: `BoSL_MDS_P2510_Uploader.ino`. Line numbers below refer to the delivered files, before subsequent edits. Search for `MDS-01` through `MDS-24` in the sketch. Original ranges in the summary are orientation aids; the exact exhaustive mapping is below.

## Summary

| Original lines | Modified starting line | Change |
|---|---:|---|
| 1–22 | 2 | Replace wiring notes: MDS-P2510 + auto-direction converter; remove old turbidity/depth reset wiring. |
| 30–33 | 19 | Use flowraw.csv / flowavg.csv; reuse Turbw for velocity, Depth for depth; expose units and startup settings; keep LOG_TO_WEB=false. |
| 49–52 | 42 | Replace EC/temp/turbidity triggers with depth 0.010 m and velocity 0.010 m/s thresholds. |
| 71 | 62 | Include the clean BoSLFlow driver. |
| 86–109 | 79 | Remove old sensor pin/UART aliases; Serial2 belongs to MDS; Serial1 remains modem. |
| 113–121 | 94 | Use unsigned millisecond constant and validate averaging intervals. |
| 177 | 153 | Give DATETIME::init an explicit void return type. |
| 202–211 | 173 | Replace old sensor values/buffers with velocity, depth and independent validity flags. |
| 222–233 | 191 | Replace shared averages with per-field sums/counts and successful-upload baseline. |
| 595–602 | 574 | Remove obsolete reset-pin routine. |
| 630; 648; 687–697 | 660 | Change SD headers/rows to SI measurements, flags and counts; correct CSV newline placement. |
| 872–918 | 865 | Initialize Serial2/converter once; wait conservatively 20 seconds; remove old ASCII reads and rail shutdowns. |
| 942–1062 | 893 | Replace inverted change comparison; handle near-zero values, validity transitions and first upload; remove obsolete tests. |
| 1074–1079 | 924 | A15-ground local-only mode now prints velocity/depth, with INVALID on failure. |
| 1088–1100 | 939 | Replace the two old sensor polling blocks with readFlowSensor(). |
| 1115–1126 | 953 | Average only valid samples; count elapsed scans before checking maximum upload interval. |
| 1128–1168 | 966 | Log every average window; only advance baseline after confirmed HTTP success (or local-only decision); reset per-window counters. |
| 1184–1353 | 995 | Replace old ASCII sensor parsers/conversion functions with Modbus reads, safe averages and numeric formatting. |
| 1388–1451 | 1052 | Remove unused legacy _test harness. |
| 1803–1832 | 1429 | Retain legacy URL endpoint/query slots; Depth=mm, Turbw=m/s; unsupported/invalid fields=-99.99; skip all-invalid uploads. |
| 1859–1865 | 1479 | Preserve late HTTP 200 result and return it, instead of unconditional success. |
| 1501–1519 | 1112 | Avoid pointer arithmetic on null and parse operator integers into int variables before narrowing. |
| 1710–1723 | 1321 | Fix RSSI sscanf pointer size and initialize unknown RSSI to 99. |
| 1768–1777 | 1384 | Bound modem response writes; fail safely when the buffer fills. |
| 1066; 1171–1172 | 916 | Use atomic millis() reads; unsigned duration subtraction handles rollover. |

## Exact changed-line mapping

All 52 changed blocks, calculated directly from the original and final sketch. Unlisted lines are unchanged. For full removed/added contents, open `ORIGINAL_TO_MDS.diff` (`-` original; `+` modified). The previews below show only the first two lines of each changed block, not the entire block.

| # | Operation | Original lines | Modified lines | Original preview | Modified preview |
|---:|---|---|---|---|---|
| 1 | replace | 1–21 | 1–7 | // File: BoSL_Turb_Depth_SeperateReset.ino /  | // File: BoSL_MDS_P2510_Uploader.ino / // MDS-01: replaces the original turbidity and depth/EC sensors. |
| 2 | replace | 31–33 | 17–26 | #define SD_LOG_FILE_ALL_DATA "log.txt"  // file to save raw sensor readings (max 8.3 characters) / #define SD_LOG_FILE_WEB_DATA  "web.txt" // file to save averaged sensor readings  | #define SD_LOG_FILE_ALL_DATA "flowraw.csv"  // file to save raw sensor readings (max 8.3 characters) / #define SD_LOG_FILE_WEB_DATA  "flowavg.csv" // file to save averaged sensor r |
| 3 | replace | 49–52 | 42–44 | #define PRESS_DIFF_THRESHOLD 10.0 // absolute threshold for depth readings (mm) / #define EC_DIFF_THRESHOLD 10.0     // absolute threshold for EC readings (mS/cm) | // MDS-03: thresholds use internal SI units; velocity threshold is configurable. / #define DEPTH_DIFF_THRESHOLD_M 0.010f |
| 4 | insert | insert after 69 | 62–63 | — | // MDS-04: clean Modbus driver; no TX/RX dumps. / #include "BoSLFlow.h" |
| 5 | delete | 80–80 | removed; next line 74 | #define IMPOSSIBLE_SENSOR_VALUE -99.99 | — |
| 6 | replace | 86–102 | 79–80 | // *** Pins *** / // For debugging | // MDS-05: Serial2 and EN_SWB belong exclusively to BoSLFlow. / #define DEBUG_PIN A15 |
| 7 | delete | 104–105 | removed; next line 82 | #define DepECScan Serial2 / #define TurbScan Serial3 | — |
| 8 | replace | 108–108 | 84–84 | #define LOOP_INTERVAL_MILLIS (LOOP_INTERVAL_MINUTES * 60000) // sensor scan | #define LOOP_INTERVAL_MILLIS (LOOP_INTERVAL_MINUTES * 60000UL) // sensor scan |
| 9 | insert | insert after 117 | 94–99 | — | // MDS-06: reject invalid averaging intervals. / static_assert(LOOP_INTERVAL_MINUTES > 0, "Scan interval must be positive"); |
| 10 | replace | 171–171 | 153–153 |   init(int16_t yr, int16_t mt, int16_t dy, int16_t hr, int16_t mn, int16_t sc) { |   void init(int16_t yr, int16_t mt, int16_t dy, int16_t hr, int16_t mn, int16_t sc) { |
| 11 | replace | 191–199 | 173–180 | // sensor variables / String DEPTH_EC[3];  // 1-depth reading, 2-Temp reading, 3-EC reading | // MDS-07: SI readings with independent validity; never substitute zero on failure. / BoSLFlow::Sample flowSample; |
| 12 | replace | 210–220 | 191–201 | double pressSum = 0; / double ECSum = 0; | // MDS-08: average valid samples only, separately for each measurement. / double velocitySum = 0; |
| 13 | replace | 593–601 | 574–574 | // RESET sensors / void resetPin(int8_t pin) { | // MDS-09: old sensor reset routine removed; MDS stays powered. |
| 14 | replace | 630–630 | 603–603 |       myFile.println(F("Time,Index,EC,Temp,Press,Turbidity,Turbiditywoled,Signal Quality")); |       myFile.println(F("Time,Index,Velocity_m_s,Depth_m,VelocityValid,DepthValid,VelocityCount,DepthCount,SignalQuality")); |
| 15 | replace | 648–648 | 621–621 |     myFile.println(F("Time,Index,EC,Temp,Press,Turbidity,Turbiditywoled,Signal Quality")); |     myFile.println(F("Time,Index,Velocity_m_s,Depth_m,VelocityValid,DepthValid,VelocityCount,DepthCount,SignalQuality")); |
| 16 | replace | 687–697 | 660–673 |     myFile.print(ECVal); /     myFile.print(F(",")); |     // MDS-10: blank invalid values; raw counts are 0/1, averaged counts are per field. /     if (velocityValid) myFile.print(velocityVal, 4); |
| 17 | delete | 872–875 | removed; next line 848 |   // _test(); /   // while(1); | — |
| 18 | replace | 893–899 | 865–865 |   pinMode(TURB_VPP, OUTPUT); /   digitalWrite(TURB_VPP, HIGH); |   // MDS-11: initialize once, allow startup, never switch EN_SWB off. |
| 19 | replace | 901–918 | 867–869 |  /   // read sensors |   BoSLFlow::begin(SENSOR_STARTUP_MS); /   while (!BoSLFlow::ready()) delay(10); |
| 20 | replace | 942–943 | 893–899 | double max(double a, double b){ /   return a > b ? a : b; | // MDS-12: trigger on absolute OR relative change, as the original config describes. / bool readingChanged(double value, double previous, double absoluteThreshold) { |
| 21 | replace | 946–1061 | 902–911 | double min(double a, double b){ /   return a &lt; b ? a : b; | int shouldLogToWeb(void) { /   if (!velocityValid && !depthValid) return false; |
| 22 | replace | 1066–1066 | 916–916 |   unsigned long millisTemp = timer0_millis; |   unsigned long millisTemp = millis(); // MDS-24: atomic read on the 8-bit MCU |
| 23 | insert | insert after 1073 | 924–924 | — |   // MDS-13: retain the existing local-only mode, now showing the MDS readings. |
| 24 | replace | 1075–1078 | 926–931 |     digitalWrite(TURB_VPP, HIGH); /  |     readFlowSensor(); /     Serial.print(F("Velocity_m_s=")); |
| 25 | delete | 1081–1082 | removed; next line 934 |  /   bool logToWebDone = false; | — |
| 26 | replace | 1088–1100 | 939–940 |   // read turbidity sensor /   digitalWrite(TURB_VPP, HIGH); |   // MDS-14: one read-only Modbus pair; no reset, ASCII commands or power cycling. /   readFlowSensor(); |
| 27 | delete | 1109–1110 | removed; next line 949 |       turbConvertChar(); /       depthConvertChar(); | — |
| 28 | replace | 1115–1115 | 953–954 |   // count scan times and add up the valid sensor readings |   // MDS-15: per-field counts exclude timeouts, CRC failures and nonfinite values. /   if (noLoggingLoopCounter &lt; 0xFFFFFFFDUL) noLoggingLoopCounter++; |
| 29 | replace | 1117–1121 | 956–957 |   pressSum += pressVal; /   ECSum += ECVal; |   if (velocityValid) { velocitySum += velocityVal; velocityAverageCount++; } /   if (depthValid) { depthSum += depthVal; depthAverageCount++; } |
| 30 | delete | 1123–1123 | removed; next line 959 |   // calculate average and decide if to log to web | — |
| 31 | delete | 1125–1125 | removed; next line 960 |     // calculate average | — |
| 32 | insert | insert after 1126 | 961–963 | — |     if (!isSDSetupDone) isSDSetupDone = SDSetup(); /     // Keep every averaging window on SD, including all-invalid windows. |
| 33 | replace | 1129–1134 | 966–973 |       // convert average values to char string /       turbConvertChar(); |       // MDS-16: failed HTTP attempts must not advance the comparison baseline. /       if (!LOG_TO_WEB \|\| logToWeb()) { |
| 34 | delete | 1136–1152 | removed; next line 975 |       // backup the data to SD card /       if (isSDSetupDone) { | — |
| 35 | delete | 1154–1155 | removed; next line 976 |  /     // clear sums and counter no matter if log to web or not | — |
| 36 | replace | 1157–1167 | 977–978 |     pressSum = 0; /     ECSum = 0; |     velocitySum = depthSum = 0; /     velocityAverageCount = depthAverageCount = 0; |
| 37 | replace | 1172–1172 | 983–983 |       timer0_millis - millisTemp; // TODO: 50days overflow to 0 |       millis() - millisTemp; // unsigned subtraction handles millis rollover |
| 38 | replace | 1184–1329 | 995–1003 | void turbread() { /   Serial.println(F("Scanning tubidity sensor...")); | // MDS-17: replace both original sensor protocols with validated Modbus readings. / void readFlowSensor() { |
| 39 | replace | 1333–1344 | 1007–1013 |   Serial.print(F("repeat count: ")); /   Serial.println(scanCounterForHttp); |   velocityValidCount = velocityAverageCount; /   depthValidCount = depthAverageCount; |
| 40 | replace | 1346–1352 | 1015–1019 |   ECVal = (double)ECSum / (double)scanCounterForHttp; /   tempVal = (double)tempSum / (double)scanCounterForHttp; | // 48 bytes cover even a finite AVR float's largest fixed-point representation. / String flowNumber(double value, uint8_t decimals) { |
| 41 | replace | 1385–1441 | 1052–1052 | void _test(void){ /   int ret; | // MDS-18: unused legacy test harness removed; modem operation stays intact. |
| 42 | replace | 1501–1501 | 1112–1113 |     end   = strstr(start, "),") + 1;//end of an operator listing |     // MDS-21: check the pointer before arithmetic; parse into correctly sized ints. /     end = strstr(start, "),"); |
| 43 | insert | insert after 1502 | 1115–1115 | — |     end++; |
| 44 | replace | 1508–1509 | 1121–1125 |     uint8_t matches = sscanf(start, "(%d,%*[^,],%*[^,],\"%6[^\"]\",%d)", &o->status, &o->mccmnc, &o->netact); /     if(matches == 3){ |     int status = 0, netact = 0; /     uint8_t matches = sscanf(start, "(%d,%*[^,],%*[^,],\"%6[^\"]\",%d)", &status, o->mccmnc, &netact); |
| 45 | insert | insert after 1704 | 1321–1324 | — |   // MDS-22: %d requires int*, not uint8_t*; default unknown on every failure. /   if (rssi_p == NULL) return false; |
| 46 | replace | 1710–1711 | 1330–1332 |   ret = sscanf(start, "+CSQ: %d,%*d", rssi_p); /   if(ret == 1){ |   ret = sscanf(start, "+CSQ: %d,%*d", &parsedRssi); /   if(ret == 1 && ((parsedRssi >= 0 && parsedRssi &lt;= 31) \|\| parsedRssi == 99)){ |
| 47 | delete | 1714–1714 | removed; next line 1335 |   rssi_p = 99; | — |
| 48 | insert | insert after 1763 | 1384–1388 | — |         // MDS-23: never overwrite RAM or lose the string terminator on long replies. /         if (i >= CHARBUFF - 1) { |
| 49 | insert | insert after 1803 | 1429–1429 | — |   if (!velocityValid && !depthValid) return false; // MDS-19: no fabricated data |
| 50 | replace | 1812–1822 | 1438–1445 |   dataStr += "&Temp="; /   dataStr += Temp; // temperature of the low cost sensor |   // MDS-19: keep all legacy query keys; -99.99 means missing, never a reading. /   dataStr += "&Temp=-99.99"; |
| 51 | replace | 1851–1851 | 1474–1474 |     sendATcmd(F("AT+HTTPSTATUS?"), "+HTTPACTION: 0,200", 1000, 15); // TODO: what is the best timeout? |     ret = sendATcmd(F("AT+HTTPSTATUS?"), "+HTTPACTION: 0,200", 1000, 15); // TODO: what is the best timeout? |
| 52 | replace | 1856–1856 | 1479–1479 |   return true; |   return ret; // MDS-20: report failure unless an HTTP 200 was actually observed |

## Added companion files

BoSLFlow.h, BoSLFlow.cpp, MdsP2510.h and MdsP2510.cpp are added files, not changes to the original sketch. They are copied from the clean sensor package previously prepared: Serial2, address 1, 9600/8N1, CRC-checked function 03 reads of two registers at 0x0000 (depth) and 0x0002 (velocity), ABCD float decoding, 500-ms reply timeout, continuous EN_SWB power. No Modbus diagnostic printing.

## Preserved behavior and limits

- SITE_DIR, SITE_ID, APN, MCCMNC, RAT settings, scan/average intervals, SIM reset policy, RTC and deep-sleep structure are retained.
- Existing operational modem/SD Serial messages remain, and A15-ground local-only mode remains; only the sensor audit/hex output and obsolete test paths are absent.
- LOG_TO_WEB remains false as in the original. Set it true when ready to upload. Nothing was flashed or transmitted during preparation.
- Invalid samples never enter averages. All-invalid windows are SD-recorded but not uploaded. Partially valid windows use -99.99 in the invalid legacy field; the server must treat this as missing.
- Missing raw SD values are blank with validity flags; flowavg.csv contains EVERY averaging window, not only windows selected for upload. Setup also writes the initial sample with 0/1 counts, preserving the original initial-record behavior.
- Existing historical data under the same SITE_ID may contain turbidity in Turbw. Do not mix it analytically with new velocity values; use a new site/data series or mark the changeover and relabel charts. Firmware does not change server code or column labels.
- The server's storage/calibration behavior has not been inspected. If it subtracts Turbwo from Turbw, transforms turbidity, or applies an old pressure calibration to Depth, those server-side transformations must be disabled for this data.
- No volumetric discharge or channel-depth offset is calculated.
- Failed uploads do not advance the comparison baseline, but this version does not replay old SD windows; the existing live-sampling upload model is retained.
- Existing network searches can still block sampling for many minutes; this change does not redesign modem scheduling.
