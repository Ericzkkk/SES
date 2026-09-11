# Network Discovery findings

Updated 2026-09-11. This investigation is separate from the supervisor's earlier Signal Finding project and from production turbidity/depth firmware.

## Conclusions and evidence

- Correct BoSL power/reset and UART startup are prerequisites. AT silence is not evidence of absent network coverage.
- Current SD discovery v4 scans LTE-M and NB-IoT separately without predefining APN/operator. A ready SIM is required by this implementation.
- A reported operator is a candidate, not proof that the inserted SIM can register. Errors and 180-second scan timeouts are inconclusive.
- Australian scans reported Telstra LTE-M with B28 configured and Vodafone NB-IoT with B5 configured. An NB B28 scan timeout did not prevent a later successful Telstra NB-IoT connection.
- The Australian targeted test on the correct COM63 board registered using an NB-enabled M2M One SIM and APN telstra.iot, on Telstra NB-IoT B28. Wrong-board COM62 failures do not establish failure of that SIM.
- An interactive HTTP test returned 200/559 bytes after SAPBR setup. CNACT alone had not opened that legacy HTTP bearer. The HTTP result was recorded in the conversation, not the saved registration log.
- Hong Kong R0005 finished with SD writes and modem shutdown verified; firmware 1529B04SIM7000G. There were 27 scans: 1 OK, 9 timeouts, 17 errors. All 14 LTE-M scans errored, so LTE-M availability remains inconclusive.
- NB-IoT with B8 configured returned CSL 45400, 3 45403, SmarTone HK 45406 and China Mobile HK 45412 after 173 seconds. All reported AcT 9/status 0. These are not independent serving-band or SIM-access proofs.
- The user reported client success on 2026-09-11; final SIM/APN/operator, firmware version and cycle logs were not supplied.

## Next deployment

Run discovery, review final results, obtain provider confirmation for the exact SIM plan/country/PLMN/RAT and APN, then verify registration, attachment, bearer and HTTP independently. Only then deploy the relevant sensor package. Preserve actual test date, code commit and raw logs.

[Private raw evidence and historical iterations](https://github.com/Ericzkkk/network-finder/tree/main/network-discovery) require repository access. They are deliberately not copied into public SES.

Production parser fixes and battery policy are documented separately in [turbidity-depth](../sensor-packages/turbidity-depth/README.md); they are not modifications to the supervisor's Signal Finding project.
