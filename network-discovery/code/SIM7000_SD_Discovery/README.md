# BoSL SIM7000G battery and SD discovery v4

Open SIM7000_SD_Discovery.ino. Select BoSL Board 0.5.x and upload once.
No APN or operator needs to be entered. The default scans the candidate global
LTE-M and NB-IoT band lists, not a preselected Hong Kong provider.

## Customer procedure

1. With power disconnected, insert the SIM and a working FAT16/FAT32 SD card.
2. Attach the cellular antenna and a charged battery.
3. Leave the board powered while it scans. No USB or computer is required.
4. Allow roughly two hours for a full first pass; repeated failures can extend
   this. Time elapsed alone does not prove completion.
5. Disconnect battery before removing the SD card. Return all three files for
   the same run number.

Each power-up reserves a new filename set, for example:

- R0001.LOG: full commands, responses, elapsed scan counters and summaries.
- R0001.CSV: scan and operator rows, saved after each band.
- R0001.TXT: readable checkpoint summaries; the last block is the latest one.

Existing files are not overwritten. If no card is usable or files cannot be
created, scanning does not start. Optional Serial Monitor at 9600 baud reports
errors and progress. There is no completion LED in this version.

A final summary ends with FINAL SUMMARY END and Run state FINISHED. Check
Assessment, Fatal/cleanup detail, SD write status and Modem power off verified: FINISHED means
the run ended, not that every scan succeeded. Missing final markers mean the
run was interrupted or logging failed; completed checkpoints remain useful.

Writes are buffered and flushed after commands, waiting updates and band
results. Sudden power loss can still lose the latest buffer or damage a FAT
filesystem. Completed checkpoints improve recovery but are not a guarantee
against card corruption. No calendar clock is required; run numbers and elapsed
seconds identify the test. Supply the actual location/date and SIM provider
when returning files.

## Interpreting results

COMPLETE means the modem returned OK. A timeout after the three-minute scan
budget is INCONCLUSIVE and may need a longer follow-up. No listed operator is
not proof of absent coverage. Global first-pass scans are not exhaustive RF
measurements. The band column identifies the verified configured scan band.

Operator status: 0 unknown, 1 available, 2 current, 3 forbidden for this SIM.
AcT: 7 LTE-M, 9 NB-IoT. A visible operator is a candidate to discuss with SIM
providers; this code does not test subscription entitlement or data service.

v4 separates total tuples, RAT-matching candidate reports and RAT mismatches.
Only a reported AcT matching the requested scan technology is a candidate.
For example, AcT 0 in an LTE-M or NB-IoT scan is RAT_MISMATCH, not evidence of
LTE-M/NB-IoT coverage. The original response is retained for diagnosis.
Matching tuples from scans without a final OK are labelled PARTIAL_CANDIDATE.
Counts are reports, not unique operators; status 3 still means forbidden for
this SIM. Matching technology does not independently verify the physical band.

The summary includes aggregate error/timeout counts and an assessment that
flags incomplete tests. Modem error text is saved per scan (up to 47 characters;
the full response is in LOG). CSV v4 adds candidates, mismatches, classification
and error columns; the old found column is renamed tuples. Tools reading the
old CSV schema must be updated. All CSV records have 15 columns.

The screenshot/CSV operator summary holds up to 40 records; names are shortened
to 24 characters. Any overflow or parse issue is flagged; the full LOG retains
the original streamed responses. The CSV records result status alongside each
operator so partial timed-out results are distinguishable.

The sketch verifies SIM READY, deregisters with COPS=2, then checks mode 2
before every scan. It leaves the saved APN untouched. Cleanup disables RF,
applies the documented full-band baseline, and requests CPOWD=1 shutdown.
Shutdown is confirmed only if the modem status pin goes low. The MCU remains
powered but stops scanning; this is not an optimised deep-sleep implementation.

## Australian bench validation

Set DISCOVERY_AU_VALIDATION to 1 near the top before compiling to run only
LTE-M B28, NB-IoT B5 and NB-IoT B28 (nine minutes of scan budgets plus setup/recovery).
Restore it to 0 for the full customer scan.

The previous v3 R0001 run verified readable SD logs and reported modem shutdown.
That does not establish battery-only operation or validate v4 on hardware.
Both v4 configurations compile for BoSL Boards AVR B:avr:bosl0v5 with the
installed SD (MegaCore) library; v4 subsequently produced the completed Hong Kong R0005 run described below.
The 180-second first-pass scan limit is unchanged. In particular, an NB-IoT
B28 timeout remains unknown, not proof of absent coverage. No automatic
registration, APN change or firmware workaround has been added.

## Hong Kong follow-up — reviewed 2026-09-11

R0005 from the customer archive completed with SD writes and shutdown verified. Firmware: 1529B04SIM7000G. Of 27 scans, 1 returned OK, 9 timed out and 17 returned errors. NB-IoT with band 8 configured returned CSL 45400, 3 45403, SmarTone HK 45406 and China Mobile HK 45412, all AcT 9/status 0. All 14 LTE-M scans returned operation-not-allowed errors; absence of LTE-M coverage is not established.

These are discovery candidates, not registration or data-service proof. APN was untouched. See the private repository FINDINGS.md for the complete interpretation. Client success was later reported without final connection logs or configuration.
