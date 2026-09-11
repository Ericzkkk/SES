# Network discovery — start here

Updated 2026-09-11. This is the BoSL SIM7000G LTE-M/NB-IoT network-discovery workflow from this task. It is separate from both the supervisor's earlier Signal Finding project and the turbidity/depth sensor package. Current discovery sketch: **v4**. Folder/documentation layout: **1.0.2**. Public maintained home: SES/network-discovery; private raw evidence stays in network-finder. No firmware logic changed in this reorganisation. The earlier public Signal_Checking_V1.ino is not part of this project and is left untouched.

## Code inventory

| Role | Files | Status |
| --- | --- | --- |
| Customer radio discovery | [SIM7000_SD_Discovery](code/SIM7000_SD_Discovery/README.md) / [sketch](code/SIM7000_SD_Discovery/SIM7000_SD_Discovery.ino) | Current v4; use this first for an unknown deployment network. |
| Targeted connection test | [SIM7000_M2MOne_NB_Test](code/SIM7000_M2MOne_NB_Test/SIM7000_M2MOne_NB_Test.ino) | Optional engineering test configured for the Australian M2M One/Telstra combination, not a generic Hong Kong default. |
| Serial capture/query helpers | [capture.ps1](code/SIM7000_M2MOne_NB_Test/capture.ps1), [query.ps1](code/SIM7000_M2MOne_NB_Test/query.ps1) | Supply actual port explicitly; COM63 was only the historical test port. |
| Earlier iterations | [private history](https://github.com/Ericzkkk/network-finder/tree/main/network-discovery/history) | Archived, not the current customer instructions. |
| Test evidence | [private evidence archive](https://github.com/Ericzkkk/network-finder/tree/main/network-discovery) | Private evidence; preserve failed/incomplete runs too. |

## Repeatable procedure

1. Identify board/modem firmware; connect the correct antenna and follow the original BoSL power/reset sequence. Confirm AT communication before interpreting any coverage result.
2. With power disconnected, insert a ready SIM and FAT16/FAT32 SD card. Discovery does not predefine an APN or operator; it still requires SIM READY in this implementation.
3. Open the current discovery sketch in its matching Arduino folder. Select BoSL 0.5.x. Keep DISCOVERY_AU_VALIDATION=0 for a full customer scan; 1 is only the short Australian bench profile. Upload the sketch, then use battery power if required.
4. Allow roughly two hours, potentially longer on repeated faults. There is no completion LED. Optional Serial Monitor is 9600 baud. Read the sketch README for full power/SD precautions.
5. Return matching Rxxxx.LOG, Rxxxx.TXT and Rxxxx.CSV files. Read the final TXT block, not an early checkpoint. FINISHED means the procedure ended, not all scans succeeded. Check SD status, fatal detail and verified shutdown.
6. Match reported access technology to requested scan mode. AcT 7 identifies LTE-M on this modem, 9 NB-IoT; other RAT reports are not LPWA candidates. Status 0 is unknown, not permission to register. Errors and timeouts are inconclusive. Configured band is not independent serving-band evidence.
7. Ask the SIM provider which candidate PLMN is authorised for this exact SIM plan, country and RAT, including roaming, and obtain the APN. A generic NB-IoT claim is insufficient.
8. Test registration, serving RAT/PLMN, packet attachment, bearer/IP and HTTP separately. The optional Australian sketch is a reference test, not an automatically adapting Hong Kong connector. Configure an appropriate test before running it elsewhere.
9. Save code commit, actual date/location, firmware, SIM provider/plan, chosen RAT/APN/PLMN, logs and outcome using [RUN_RECORD_TEMPLATE.md](RUN_RECORD_TEMPLATE.md). Keep sensitive identifiers private.
10. Once network access is established, upload the relevant sensor firmware separately. Turbidity/depth production settings and changes belong in [SES](https://github.com/Ericzkkk/SES/tree/main/sensor-packages/turbidity-depth), not this workflow.

## What we have done so far

| Stage | Outcome / lesson |
| --- | --- |
| Initial modem startup | Used the BoSL power/reset sequence; no AT response was a communication issue, not proof of absent coverage. |
| Discovery revisions | Added separate RAT/band scans, SD logs/checkpoints, bounded scan budgets, timeout recovery and final summary. v4 separates matching candidates from anomalous RAT reports. |
| Australian discovery | Telstra LTE-M with B28 configured and Vodafone NB-IoT with B5 configured were candidates. A Telstra NB B28 timeout did not rule out later connection. |
| Australian registration | Correct COM63 board with NB-enabled M2M One SIM, APN telstra.iot, registered to Telstra NB-IoT B28. Earlier COM62 tests used the wrong board. |
| HTTP check | Legacy HTTP worked after SAPBR setup; CNACT alone was insufficient for that tested command path. Interactive example.com GET returned HTTP 200/559 bytes. |
| Hong Kong R0005 | B04 firmware; 27 scans: 1 OK, 9 timeouts, 17 errors. NB-IoT B8-configured scan returned four candidates after 173 seconds. All LTE-M scans errored; LTE-M absence not established. |
| Client feedback | User reported success on 2026-09-11. Final SIM/APN/operator, flashed version and consecutive-cycle logs were not supplied. |

Hong Kong candidates: CSL **45400**, 3 **45403**, SmarTone HK **45406**, China Mobile HK **45412**. All reported AcT 9/status 0. These are discovery observations, not universal connectivity guarantees.

See [FINDINGS.md](FINDINGS.md) for full evidence and limitations. The optional targeted test leaves the modem powered on and does not restore the original configuration; it is not unattended field firmware. Historical instructions can be superseded; use this page and the current v4 README for a new run.
