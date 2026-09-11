# Turbidity + depth 1.0.1

Logic edit: 2026-09-09. Documentation edit: 2026-09-11T14:12:27+10:00 (Australia/Sydney).
First published package version; the proposed 1.0.0 was not released.

| Area | Before | After |
| --- | --- | --- |
| Operator parser | First plus sign in response could be +40 in an unsolicited time message | Locate +CPSI: specifically; validate MCC/MNC digits, delimiter and output capacity; clear output on failure. |
| Successful cleanup | RF disabled and modem powered off | Preserved for field battery operation. |
| Failed cleanup | AT cleanup gated on unresolved transaction; physical shutdown attempted | Preserved. |
| Next upload | Power on, configure/register, open bearer and upload | Preserved; registration is not retained across physical shutdown. |

The temporary keep-powered experiment was withdrawn at the user's request before publication. No PSM/eDRX mode is configured. The MCU sleeps between sensor readings; the modem uses PWRKEY shutdown, not battery disconnection.

Cellular inputs remain APN, MCCMNC and CELLULAR_MODE (1 LTE-M, 2 NB-IoT). Site/sensor settings, payload and physical reset/power pulse timings are preserved. See CHANGES.md for the complete original-to-revision table.

## Evidence and limits

Both radio configurations compile for BoSL 0.5.x. Eleven host-C scanf cases pass, including the supplied +40/CPSI collision; this is not AVR UART or modem simulation.

On 2026-09-11 the user reported "It works for our client" after the Hong Kong investigation. No final SIM/operator/APN, flashed firmware version, upload log or cycle count was supplied. Record this as user-reported client success, not independent validation of every production path or both RATs.

For each deployment, retain at least three consecutive upload logs and check server records, shutdown status and measured battery current. Registration can block for minutes. HTTP 200 is not proof of server persistence or exactly-once delivery.
