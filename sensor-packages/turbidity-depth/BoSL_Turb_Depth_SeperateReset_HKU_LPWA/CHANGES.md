# BoSL production LTE-M / NB-IoT revision

## Current publication: 1.0.1 — 2026-09-11

See [1.0.1 release notes](RELEASE_NOTES_1.0.1.md) for the CPSI parser fix and validation scope. Modem power-off after each upload attempt is retained; the temporary keep-powered change was withdrawn. Client success was reported on 2026-09-11 without final configuration or upload logs. The table below records the original-to-baseline changes; the 1.0.1 notes add the subsequent parser correction.

## User settings

Open `BoSL_Turb_Depth_SeperateReset_HKU_LPWA.ino` in Arduino IDE and select BoSL Board 0.5.x. Serial baud remains 9600.

For the M2M One SIM tested on Telstra Australia:

```cpp
#define APN "telstra.iot"
#define MCCMNC "50501"
#define CELLULAR_MODE 2
```

`CELLULAR_MODE`: **1 = LTE-M only; 2 = NB-IoT only**. Only the chosen technology is attempted. There is no automatic technology fallback, no extra mode flags and no manual band setting required. Change these definitions and re-upload when changing the deployment/SIM. APN and network access must be confirmed by the SIM provider.

A non-empty MCCMNC deliberately restricts registration to that operator, using the chosen technology. Use `#define MCCMNC ""` to permit automatic operator selection among networks accessible to the SIM. This differs from the original preferred-operator-then-other-operators behaviour. The sketch accepts home and roaming registration.

The included settings are for Australia, not Hong Kong. Set the actual Hong Kong provider's APN and network code before deployment. Existing `SITE_DIR`, `SITE_ID`, measurement intervals and sensor settings still need the normal project-specific configuration; the three settings above are the cellular settings only.

## Complete functional change table

| Area | Before | After |
| --- | --- | --- |
| Delivered file | Desktop `BoSL_Turb_Depth_SeperateReset_HKU.ino` | Separate `BoSL_Turb_Depth_SeperateReset_HKU_LPWA.ino`; Desktop original unchanged. Header identifies the revision. |
| Default APN | `sensor.net` | `telstra.iot`, matching the successfully tested M2M One subscription. |
| Default operator | `45403` | `50501` for Telstra Australia; editable 5/6-digit string or empty for automatic selection. |
| Radio selection | `ALLOW_NBIOT=false` selected LTE-M; `true` selected both technologies, with separate GSM/home-registration flags | One `CELLULAR_MODE` setting: 1 LTE-M, 2 NB-IoT. Invalid modes fail compilation. Both home and roaming registration accepted; GSM disabled. |
| Automatic fallback | Preferred/cached operator, configured operator, automatic selection, then discovered operators | Exactly the selected radio mode; non-empty MCCMNC is an explicit operator restriction. No alternate RAT or operator fallback unless MCCMNC is empty. |
| Configuration validation | No APN/network-code validation | APN must be non-empty, at most 63 characters, with letters/digits/dots/hyphens. MCCMNC must be 5/6 digits or empty. Invalid input fails clearly. |
| Configuration sequence | RF enabled before mode and APN changes | SIM readiness checked, deregistration requested, RF disabled, RAT/bands/APN configured and read back, then RF enabled and SIM readiness checked again. |
| Band masks | Could inherit a previous diagnostic's single-band restriction | Applies and verifies the full SIM7000G LTE-M and NB-IoT band lists used with B08/B10. No country-specific band lock. Unsupported/rejected configuration fails instead of being ignored. |
| Cell locks | Not verified in production registration | Requires both cell-lock queries to report no lock. Unexpected locks stop the attempt; they are not silently cleared. |
| Registration command | `COPS=4` preferred operator without explicit access technology | Quoted numeric operator with `COPS=1,2,"MCCMNC",7/9`, or `COPS=0` when MCCMNC is empty. |
| Registration status | `CREG?`, matching only strings with reporting mode 0 | `CEREG?`, parsing reporting mode and status separately; accepts status 1 or 5, reports status 3 and CEER without assuming its cause. Sets reporting mode after RF is enabled. |
| Successful network verification | Registration flag alone | Checks serving RAT with CPSI, verifies supplied operator, and waits for `CGATT: 1`. Prints network/APN evidence to Serial. |
| Registration timing | Empirical attempt count and short clock-scaled waits | Elapsed-time polling: up to 180 s for LTE-M or 300 s for NB-IoT after the operator-selection command. COPS commands have separate 180 s limits; setup/attachment adds time. Timeouts remain inconclusive. |
| Network discovery | Potential 30-minute `COPS=?`, tuple parsing and RSSI-ranked operator list | Removed from production connection path, including obsolete parser/data structures/debug examples. Discovery remains a separate diagnostic sketch. |
| HTTP bearer | `openbearer()` returned void; short unverified SAPBR commands; CSTT also configured | Boolean success/failure; verifies packet attachment, closes any non-closed SAPBR bearer, sets the chosen APN, opens with an 85 s limit, verifies state 1 and a nonzero IP. CSTT removed from this HTTP-only path. |
| Bearer failure | Upload proceeded regardless | Upload is skipped and failure counted when bearer setup fails. SAPBR close uses a 65 s limit. |
| AT buffer handling | Unchecked 8-bit buffer index, potential overwrite/wrap and missing termination | Bounded response and line buffers with explicit null termination; oversized response is rejected rather than trusted. Expected-response pointer is const. |
| AT response boundaries | Substring matching and discarded receive buffer | Complete-line terminal OK/ERROR/CME/CMS handling; incoming unsolicited lines are parsed rather than blindly flushed before commands. |
| AT retry/timeout handling | Default five retries could overlap a still-running command | Default single attempt; only startup AT/ATE0 negotiation retries on silence. Unresolved commands block further AT transactions until physical reset/power-cycle bookkeeping clears the state. |
| Modem waiting | Clock prescaling through xDelay could disturb active UART reception | Network/HTTP waits keep the UART at its normal clock and consume incoming data. Original physical power/reset pulse timings are unchanged. |
| HTTP completion | `HTTPSTATUS?` polling could discard the asynchronous HTTPACTION result | Arms HTTP result capture before sending HTTPACTION; waits for its actual completion event for up to 120 s, including immediately arriving or fragmented results. |
| Upload success | Could return true without confirming HTTP 200; later retry result ignored | Returns true only after the GET completion reports HTTP 200; prints status and byte count. This is transport/application-status confirmation, not proof the server saved data correctly. |
| Unknown HTTP result | Could retry while the earlier request was unresolved | No immediate retry after missing command/completion response; marks reset required. Avoids overlapping actions and reduces duplicate-write risk. Other explicit failures retain the existing outer retry limit. |
| HTTP/cleanup sequence | Could issue cleanup/retry commands while an earlier operation remained pending | HTTPTERM, SAPBR close and CFUN cleanup are gated by transaction state; physical power-off cleanup remains. HTTP session termination attempted after completed transactions. |
| RSSI parsing | `%d` wrote through a `uint8_t*`; failure assigned the pointer rather than its value | Parses into an int, validates 0–31 or 99, then safely assigns a byte. Failure returns signal 99. |
| Network-time parsing | sscanf used overlapping input/output storage for the `+CCLK` token | Parses the literal prefix without writing into its input buffer; updates expected assignment count from 8 to 7. RTC conversion policy unchanged. |
| Supporting query timeouts | CCLK/CLTS/CBC/CPSI information queries used 1 s | 5 s for these queries; operator extraction now checks AT success before parsing. |
| Failure-reset arithmetic | `% 3 * NET_REG_FAILS_LIMIT_RATE` had unintended precedence | `% (3UL * NET_REG_FAILS_LIMIT_RATE)` expresses the intended interval. |
| Dependencies | MCP7940 required by the original but missing locally | Installed MCP7940 1.2.0 for compilation. No new runtime library added to the sketch except standard `ctype.h` for input validation. |

## Preserved behaviour and boundaries

- Sensor commands, sensor pins, calibration/conversion and averaging routines are unchanged.
- RTC setup, SD file formats, site identifiers, upload intervals and normal physical modem power/reset timing are retained.
- The production HTTP GET URL construction and all sensor fields are unchanged. HTTP, not HTTPS, remains in use.
- Existing software resets, power-off handling and upload scheduling remain blocking. A difficult registration can still take several minutes and delay sensor sampling. Full-band acquisition is not guaranteed within the chosen budgets.
- No automatic retry can guarantee exactly-once writes to the existing GET endpoint. A server-side unique record ID/deduplication mechanism would be needed for that guarantee.
- No modem firmware was changed. SIM authorisation, regional coverage and server reachability are still prerequisites; this revision does not make unsupported SIM/network combinations work.

## Verification

Both selected-mode configurations are compiled for `BoSL Boards AVR B:avr:bosl0v5`, with MCP7940 1.2.0 and SD (MegaCore) 1.0.7. Source regression checks cover unchanged sensor/SD routines, unchanged payload construction and unchanged physical power timings (excluding added transaction-state clearing).

The earlier separate COM63 test demonstrated Telstra NB-IoT B28 registration and HTTP 200 with `telstra.iot`, using SAPBR and the legacy HTTP commands. The assistant did not upload or hardware-test the full production revision and did not open either COM62 or COM63 during the revision task. On 2026-09-09, the user subsequently confirmed that the revised code was working. No new logs or test details were supplied with that confirmation, so it does not independently establish testing of both modes, Hong Kong networks, or every failure/recovery path.

Reference: [SIMCom SIM7000 AT command manual V1.06](https://www.tinytronics.nl/product_files/004312_SIM7000_Series_AT_Command_Manual_V1.06.pdf).
