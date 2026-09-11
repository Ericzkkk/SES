# Turbidity + depth history

## 1.0.1 — published 2026-09-11

First published package snapshot. Includes explicit LTE-M/NB-IoT selection, verified registration/bearer/HTTP handling, bounded AT transactions and the CPSI parser fix. Modem power-off after each attempt is retained. See [full change table](BoSL_Turb_Depth_SeperateReset_HKU_LPWA/CHANGES.md) and [1.0.1 notes](BoSL_Turb_Depth_SeperateReset_HKU_LPWA/RELEASE_NOTES_1.0.1.md).

Client success reported 2026-09-11; exact final configuration and cycle logs not supplied. This is not blanket certification.

## 1.0.0 — unpublished baseline, 2026-09-09

Initial revised code compiled; first upload succeeded, but a later cycle revealed a false operator mismatch caused by an unsolicited +40 timezone field. Publication was paused.

## Withdrawn experiment

Retaining modem power/registration between cycles was briefly implemented then removed at the user's request to preserve field battery operation. Not a released power policy.

## Future entries

Record version, timestamp/timezone, before/after changes, compatibility/migration, board/modem/library versions, actual tests and remaining limits. Increment versions; do not silently replace an earlier commit.
