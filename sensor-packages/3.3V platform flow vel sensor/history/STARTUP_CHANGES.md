> Historical development notes. Settings, line numbers and local-file references may be superseded. Use the [current package README](../README.md) and current sketch as authoritative. The old ORIGINAL_TO_MDS.diff was excluded because it contained a script error, not a valid diff.

# Setup measurement and optional network-time sync

Current sketch: BoSL_MDS_P2510_Uploader.ino. Changes marked BOOT-01 to BOOT-04.

| Current lines | Change |
|---|---|
| 814-815 | Report network-time failure and retain the existing RTC time. |
| 847, 865 | Update initialization comments: rails start off; first read is in setup. |
| 875 | Correct the fallback message: absence of an SD timestamp does not actually set compile time. |
| 878-880 | Take the first sensor reading in setup; readFlowSensor shuts down both rails afterward. |
| 882-901 | When LOG_TO_WEB is true and A15 is not grounded, call the existing modem upload routine, including network-time retrieval and RTC update. Save the startup reading to SD afterward. Advance the upload baseline only if HTTP upload succeeds. |
| 904-906 | Wait the remainder of the six-minute cycle before entering loop, avoiding a duplicate immediate measurement. |

Order: initialize RTC -> power on -> retained 10-second warmup -> read
velocity/depth -> power off -> optional modem connection/time sync/startup
upload -> SD record -> wait until the next cycle.

LOG_TO_WEB=false means no startup network activity. A15 grounded retains the
existing local-only override even when LOG_TO_WEB=true. Time comes from the
modem's network clock via pullNetTime(), not from the HTTP server's clock.
Time-sync success and HTTP upload success are independent: the RTC can be
updated even when upload fails; a failed time sync retains the RTC value.

With no valid sensor fields, Transmit() refuses to send fabricated data, but
the network-time attempt can still occur. SD timestamps are logging time after
the network attempt, not reconstructed acquisition timestamps. Long network
registration can exceed six minutes; the blocking modem design is unchanged.
The first setup sample is separate from subsequent averaging windows.

Earlier change tables are historical; these are current line numbers for
this update. No board was flashed or HTTP traffic sent during preparation.
