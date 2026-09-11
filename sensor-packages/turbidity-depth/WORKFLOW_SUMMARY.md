# From network discovery to field firmware

Updated 2026-09-11. Package: turbidity + depth, v1.0.1.

| Step | What we did / learned | Code or action |
| --- | --- | --- |
| 1. Establish modem communication | Follow the BoSL physical power/reset sequence and correct UART. Early AT silence was not proof of missing coverage. | Board-specific startup retained. |
| 2. Discover operators | Scan LTE-M and NB-IoT separately, verify settings, save checkpoints and full responses to SD; leave APN untouched. | Separate SIM7000_SD_Discovery v4, maintained privately. |
| 3. Classify results | Match access technology to scan RAT. Errors/timeouts are inconclusive; anomalous AcT 0 reports are not LPWA candidates. | Review final TXT summary plus raw LOG. |
| 4. Australian connection test | Correct board COM63 with an NB-enabled M2M One SIM and telstra.iot registered on Telstra NB-IoT B28. Wrong-board COM62 failures were not evidence against that SIM. | Optional targeted SIM7000_M2MOne_NB_Test. |
| 5. Verify HTTP | CNACT alone did not open the legacy HTTP SAPBR bearer. Configuring/opening SAPBR enabled an example.com HTTP 200 response. | Production checks bearer state/IP and actual HTTPACTION completion. |
| 6. Revise production | Explicit APN/MCCMNC/mode, full band masks, CEREG/RAT/operator/attachment checks, bounded AT parsing and reset-on-unresolved transactions. | Current production sketch in this package. |
| 7. Fix repeat-cycle parser | +PSUTTZ-style time information containing +40 preceded CPSI; first-plus parsing caused a false mismatch despite registration. | v1.0.1 searches +CPSI: specifically and validates the PLMN. |
| 8. Preserve battery policy | A temporary keep-powered approach was withdrawn. | Modem powers off after each upload attempt; reconnects next time. |
| 9. Hong Kong discovery | R0005 found four NB-IoT candidates with B8 configured. LTE-M errors did not establish absence of coverage. | Obtain SIM-provider confirmation of NB-IoT access and APN before connection testing. |
| 10. Client feedback | User reported client success on 2026-09-11. Final SIM/operator/APN, firmware and upload-cycle evidence were not supplied. | Retain deployment-specific verification records. |

## Which sketch is required?

For normal sensing/upload, use only [the production sketch](BoSL_Turb_Depth_SeperateReset_HKU_LPWA/BoSL_Turb_Depth_SeperateReset_HKU_LPWA.ino). The SD discovery sketch is a temporary troubleshooting replacement, not an additional sketch to install alongside production. Upload production again after diagnostics and set the real cellular/site parameters.

The targeted Australian test is optional engineering equipment, not a required Hong Kong deployment step. Historical diagnostic variants are not the recommended customer tool.

## Hong Kong candidates from R0005

| Reported operator | MCCMNC | Reported RAT | Configured band |
| --- | --- | --- | --- |
| CSL | 45400 | NB-IoT | 8 |
| 3 | 45403 | NB-IoT | 8 |
| SmarTone HK | 45406 | NB-IoT | 8 |
| China Mobile HK | 45412 | NB-IoT | 8 |

All reported status 0 (unknown). The successful scan took 173 seconds. Discovery does not establish subscription permission, actual serving band, registration or HTTP. The run finished with 1 successful scan, 9 timeouts and 17 errors across 27 scans. Firmware B04 differed from the Australian B08/B10 units; that difference alone does not prove the error cause.

## Deployment checklist

1. Confirm SIM entitlement for the country, specific operator and selected RAT; obtain APN.
2. Configure APN, MCCMNC and CELLULAR_MODE; set site identifiers and sensor/upload intervals.
3. Install correct board/libraries, connect sensors/antenna/SD, and upload production.
4. Capture consecutive cycles: registration, attachment, bearer, HTTP status and verified shutdown.
5. Check actual server records and battery current. Keep sensitive field logs private.

See [setup](README.md), [change history](CHANGELOG.md) and [complete before/after table](BoSL_Turb_Depth_SeperateReset_HKU_LPWA/CHANGES.md).
