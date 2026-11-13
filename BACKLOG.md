# Water Supply — Engineering Backlog

**Established:** 2026-11-12  
**Scope:** Well-room pressure, flow, camera reference, MQTT/Home Assistant historian and later water-supply subsystems.  
**Working principle:** Preserve original pulse counts / ADC readings and calibration versions; improve empirically from real-world measurements. All telemetry is observational, **not** a pump safety/control interlock.

## Current checkpoint

- [x] Dual 100-PSI pressure transducers installed on either side of Rusco filter; ESP32-S3 GPIO4/GPIO5, RC conditioning, OTA and MQTT to Home Assistant
- [x] Pressure firmware v0.2.0 provisionally calibrated from two historical manual readings; v0.3.0 retains those coefficients (still **not** independently validated)
- [x] Raspberry Pi 3 webcam, OpenCV needle detector, roughly 70-second systemd timer, CSV angle logging and periodic archived images
- [x] Diagnosed sticky old mechanical gauge using synchronized Pi photographs and HA pressure; **replaced with new glycerin-filled gauge on 2026-11-12**
- [x] GREDIA G1 Hall flow meter **1** installed and reporting pulse count, GPM and total US gallons via v0.3.0; GPIO6
- [x] Flow meter **2** firmware/HA discovery ready on GPIO7; hardware not yet installed
- [x] Cumulative raw pulse counts stored periodically in ESP32 NVS; flow totals currently use **nominal 330 pulses/liter**
- [x] Retained MQTT discovery and HA Recorder pressure/flow history working; no current Rusco differential entity
- [x] Re-seating transducer harness connectors reduced downstream signal variation; **leave wiring unchanged** unless new evidence justifies work
- [x] v0.3.0 OTA deployed and seen in Home Assistant; six flow entities discovered (meter 2 correctly zero while disconnected)
- [ ] Record/reference build SHA for v0.3.0 deployment and verify NVS persistence with a controlled restart

## P0 — Next: make measurements trustworthy

### WS-001 — Calibrate flow meter 1 by a measured five-gallon draw
**Status:** TODO · **Owner:** Field test + data analysis  
- Record *meter 1 raw cumulative pulse count* and timestamp at start; deliver a measured **five US gallons** through the installed meter; record end pulse count/timestamp. Avoid mixing another draw into the test.
- Use a verified container/scale if possible; note uncertainty of the five-gallon reference and whether the volume was measured or read from markings.
- Compute **pulses per liter = Δpulses / (5 × 3.785411784)**. Nominal expectation: **~6,246 pulses over 5.000 US gal**.
- Repeat once to check repeatability; ideally later at a second steady flow rate. Keep meter-specific coefficients (`FLOW_1_PULSES_PER_LITER`).
- Compare actual flow against indicated GPM and verify rate returns to zero when stopped.
- **Done when:** Observed pulse-count change, reference volume, test flow conditions, new coefficient and estimated uncertainty recorded; update `FLOW_CALIBRATION_ID` and validate a separate run.

### WS-002 — Re-aim and recalibrate Pi camera for the new glycerin-filled gauge
**Status:** TODO · **Owner:** Camera setup + data analysis
- Take and archive a photo of new gauge face and its accuracy grade/model; confirm 0/100 PSI scale endpoints, center, orientation, viewing angle, and legibility.
- Reposition or re-aim webcam and regenerate `~/water-supply/gauge/config.json` **ROI, circle and rotation**; verify OpenCV identifies the *thin indicating needle* rather than counterweight or dial tick marks.
- Obtain synchronized new-gauge visual readings, OpenCV angles, ESP32 pressures and timestamps near pump cut-in, mid-cycle and cut-out (ideally more points).
- Fit angle → PSI for the **new** dial; do not reuse old-gauge angle calibration. Reject low-confidence camera detections and use images for spot checks.
- Retain **separate calibration IDs** for the replacement gauge/camera and ESP32 pressure channels; archive old runs without rewriting history.
- **Done when:** Camera provides repeatable PSI readings across normal working range, validated against visible gauge graduations at 3+ pressures.

### WS-003 — Refit/validate electronic pressure calibration against new gauge
**Status:** PROVISIONAL REFIT COMMITTED v0.3.2; OTA / INDEPENDENT FIELD VALIDATION PENDING
- Align synchronized upstream/downstream voltages with new-gauge readings under no-flow conditions at multiple pressures, with uncertainty notes.
- Fit each channel independently, assess residuals and hysteresis, and recheck around 40-ish PSI cut-in and 65-ish PSI cut-out. Do not assume nominal 40/60 switch settings are exact.
- Use unrounded raw ADC mV (and sensor V) when possible. Keep the v0.2.0 pressure coefficients as identifiable historical baseline, not truth.
- **Done when:** Independent validation run and calibration revision documented; no implausible static pressure difference.

## P1 — Improve coverage and automatic acquisition

### WS-004 — Install and individually calibrate flow meter 2
**Status:** WAITING FOR FIELD INSTALL
- Connect to GPIO7 with a 3.3V-safe digital interface; verify pulse count and GPM, then carry out its own measured-volume test.
- Name/describe the meter's actual physical location; don't assume its total gallons can be added to meter 1.

### WS-005 — Adaptive pump-cycle logging (ESP32)
**Status:** IMPLEMENTED IN CODE v0.3.1; BUILD / OTA / FIELD VALIDATION PENDING
- Keep pressure ADC sampling at 40 Hz; idle 5s MQTT; ~1s during active cycle and ~0.5s near start/stop, with sensible hysteresis.
- Publish min/max/mean and sample count per period to preserve short transients. Record the evidence used for inferred pump activity; cross-check against power telemetry.
- Prefer separate raw/filtered values when useful; don't smooth away pulse edges. Validate historian load/retention impacts.

### WS-006 — Adaptive camera sampling + cross-instrument alignment (Pi)
**Status:** PLANNED; 10-second manual burst capture procedure available for near-term calibration cycles
- Ingest pump-activity state over local MQTT; e.g. ~60–70s baseline and ~5–10s while active, subject to Pi 3 capture/detection performance.
- Avoid contention between webcam web UI and systemd job. Archive representative images at meaningful state changes.
- Centralize timestamps, calibration IDs, and data exports for sensor/camera comparisons.

### WS-007 — Quantify stability and data integrity
**Status:** PLANNED
- Log signal-noise metrics per sensor, reconnect status, missed readings, and pulse counter behavior.
- Validate cumulative pulse totals survive OTA/restarts, and document potential unsaved pulses on unplanned power loss.
- Add a simple CSV export for a requested time window spanning Pi logs and Home Assistant Recorder.

### WS-013 — Home Assistant water-consumption dashboard: cumulative, daily, weekly, monthly
**Status:** TODO · **Priority:** P1
- Use the installed flowmeter's **monotonically increasing total gallons** MQTT sensor (currently Flow Meter 1 Total) as the single source of truth for household water-use tracking; don't sum two meters unless their actual locations require it.
- Create four HA views/entities: **cumulative (all-time)** total gallons plus **daily**, **weekly**, and **monthly** water use. HA `utility_meter` helpers or equivalent can derive calendar-period totals from the existing `total_increasing` sensor; use local time for period boundaries.
- Build a compact **Water Supply** Home Assistant dashboard section with present GPM, cumulative gallons, today's gallons, this week's gallons, and this month's gallons. Include useful historical trend charts later.
- Check utility-meter behavior on ESP32 restart/NVS recovery, reconnect, counter discontinuities, and future meter calibration changes, so period totals aren't distorted.
- **Done when:** The four totals show sane values, daily/weekly/monthly reset at their expected calendar boundaries, and volumes reconcile with raw pulse counts and the five-gallon reference test.

## P2 — Later extensions

- **WS-008:** Characterize actual pump cut-in/cut-out, pressure-storage drawdown, gallons per cycle, pump electrical consumption, and flow-versus-pressure curves.
- **WS-009:** Implement leak detection / unexplained flow alerts and filter assessment only after flow/pressure calibration. Do **not** reintroduce Rusco differential until the static offset is validated.
- **WS-010:** Local Wi-Fi credential recovery option (secure, disabled by default if possible) to avoid USB rescue after SSID mistakes.
- **WS-011:** Water quality measurements and solar-assisted water treatment/storage, based on actual chemistry testing; separate scope from instrumentation.
- **WS-012:** Irrigation use/per-zone water metering, using measured rather than assumed flow.

### WS-014 — Characterize real pump-start pressure collapse and pressure-switch operation
**Status:** BACKLOG · **Priority:** P2 (investigate after calibration)
- On November 13, 2026, both electronic pressure sensors independently recorded a **real hydraulic pressure dip** with near-simultaneous minima at **08:02:08 MST**: upstream **20.71 PSI**, downstream **20.88 PSI** (provisional pressure calibration). Pressure recovered to ~38 PSI at the next 5-second observation. User observed the drop as real, not a voltage/ADC artifact.
- Separate *hydraulic response* from *electrical pump-switch action*. Correlate future 40-Hz-derived minimum/maximum pressure, frequent MQTT reports, flowmeter counts, the mechanical gauge camera, and an independently confirmed motor-start indicator (pump electrical load data) with synchronized timestamps.
- Record actual cut-in/cut-out, possible switch contact bounce/response delay, pump start latency, tank behavior, and any startup pressure overshoot or undershoot. Do not infer switch malfunction solely from this transient.
- A later November 13 observed cycle appeared to reach **nearly zero indicated pressure before pump activation**. See **WS-015** for the distinct mechanical/plumbing investigation after telemetry validation, subject to safety exceptions.
- Note that adaptive v0.3.1 telemetry only first appeared at ~08:04:15 MST on this run, **after** the 08:02:08 transient; the earlier samples are ~5 seconds apart, leaving the true transient minimum and duration unresolved.
- Revisit after electronic pressures are calibrated to the replacement glycerin-filled mechanical gauge; historical minimum of 20.7 PSI is **not yet a verified absolute pressure**.
- **Done when:** At least two pump start/stop cycles are synchronized to reliable motor-state telemetry and pressure/flow observations, with documented pressure-switch behavior and uncertainty.

## Confirmed well control hardware (2026-11-13 photos)

The following details were read from **actual user photographs** of the installed equipment, not inferred from lookalike catalog photos:

| Component | Identification | Observations |
|---|---|---|
| Pressure switch | **Square D / Pumptrol Class 9013 FSG2, Series B** | Cover stamped **ON 30 / OFF 50 PSI**; 230 V 1-phase 1½ HP on cover; factory setting does **not** establish current adjustment |
| Submersible pump control box | **Pentair PENTEK SMC-IR1021-01** | Label: **1 HP, 230 V**, date code **033B19M**; 0.75 kW, 8.2 A nominal, max SF 9.8 A, automatic thermal protection *in motor* |
| Start relay | **U17-1592** (label visible in box) | Pentair current manual lists U17-1592 for 230-V CSIR controls |
| Start capacitor | Large black can, exact current component part not fully confirmed | Pentair 2023 documentation lists **U17-2065-R, 105 µF, 330 V** as the nominal 1-HP CSIR replacement; confirm on actual capacitor before sourcing |
| Submersible pump/motor | **Actual installed model unknown** | Control box says “use with motor(s) P43B0010A2 only,” which is compatibility guidance, **not** confirmation of installed pump or motor model |
| Pressure tank | **Amtrol 86-gallon**, manufactured 1988 per July 2023 inspection | Tank model `WX-252` carried in project context; verify physical tag when convenient. 2023 report showed 27 PSI air charge at nominal 30/50 operation |

**Manufacturer sources:** 
- Schneider Electric Class 9013F/G maintenance/troubleshooting bulletin 9013DB0701: https://s3.amazonaws.com/s3.supplyhouse.com/product_files/Square-D-9013GHG6J65X-Install-Instructions.pdf
- Schneider Electric 9013FSG pressure switch adjustments: https://www.se.com/us/en/faqs/FA121330/
- Pentair PENTEK Electronics Manual PN793 (2023), §10.4, p.90: https://www.pentair.com/content/dam/extranet/web/nam/pentek/manuals/pn793-electronics-iom.pdf

**Diagnostic interpretation as of November 13:** Standard FSG2 switches are expected to operate at roughly 15–30 PSI differential; user observed a new instance of severe delay toward near-zero pressure before a pump restart, despite roughly 62 PSI cutoff. Schneider specifically lists obstruction of the switch sensing passage / diaphragm inlet as a cause of failure to sense tank pressure; electrical switch contacts, control-box start circuit, motor thermal protection and actual hydraulic recovery remain alternatives. Inspect for passage deposits and pressure communication **only with power isolated and pressure safely released**, or by qualified service. Do not change settings to compensate for an unverified failure. Distinguish switch closure, pump motor electrical start, and pressure recovery from telemetry. Severe recurrent pressure loss warrants prompt inspection, not repeated induced cycles.

### 2026-11-13 10:35:20 pump-collapse review (Pi + HA)
**High-priority mechanical investigation evidence — inspect before deliberately repeating this event.**

- Observed water draw began ~10:30 MST, with **Flow Meter 1** running around 2.5–3 GPM. Pressure fell slowly through about 28 PSI around 10:34:45, then plunged rapidly ~10:35:00–10:35:21.
- At **10:35:20.954 MST**, Home Assistant v0.3.2 recorded **upstream 5.53 PSI / downstream 7.99 PSI**, with raw interval minima **4.71 / 7.11 PSI** respectively. The corresponding sensor voltages fell smoothly on **both** channels, not just one.
- The **actual archived Pi photograph at ~10:35:21** independently shows the new glycerin-filled needle **near zero PSI**. The needle detector returned **−123.811°**. This is an angular wrap representation; using `−123.811 + 360 = 236.189°` with the **provisional, low-range-extrapolated** camera conversion estimates ~**1.3 PSI**. Do **not** take 1.3 PSI as an accurate calibrated value: the camera conversion was anchored only at 36.0 and 61.75 PSI. The visual *near zero* conclusion is robust.
- The electronic pressures recovered rapidly starting approximately **10:35:21–10:35:23**. Flow Meter 1 rate dropped to **0.144 GPM** at 10:35:21 and briefly peaked at **7.253 GPM** at 10:35:23.95 before settling. This coincident timing indicates a major hydraulic transition; without dedicated motor-current/power telemetry in this export, it does **not** establish the exact switch-closure or motor-start instant.
- Pressure rose continuously afterward and reached roughly **63–64 PSI near 10:40:20**, then stabilized near **62 PSI**. This is consistent with a pump run lasting roughly five minutes after pressure recovery; motor start/stop times require electrical corroboration.
- The new-gauge camera agrees well with the v0.3.2 upstream pressure measurement across the **~26–64 PSI measured range** (40 time-matched valid photo/HA pairs; median upstream residual about −0.19 PSI and mean absolute residual ~0.48 PSI, despite camera angle noise). This supports field calibration in that span, **not** accuracy claims near 0 PSI.
- **New software backlog:** Correct the Pi angle normalization/wrap logic and apply plausibility checks that consider cyclic angles. The sample at −123.811° should not be categorized as a random detector failure merely because it is negative.
- **Hardware follow-up:** User reports the Square D 9013 FSG2 Series B switch uses a short horizontal sensing standoff; sediment/mineral restriction is plausible. Verify switch passage, switch motion/contacts, and control-box motor-start behavior. With a severe near-zero recurrence, promptly inspect rather than intentionally inducing more near-zero episodes. Switch/contact enclosure is 230 V equipment: isolate, verify deenergized, and depressurize before disassembly; qualified pump/electrical service is appropriate.

### WS-015 — Mechanical / plumbing investigation of delayed pump cut-in
**Status:** OPEN · **Priority:** P0 / prompt inspection: November 13 near-zero event now independently confirmed by the Pi camera and both ESP32 channels

- **New November 13, 2026 observation:** On a subsequent manually initiated pump cycle, the user observed the pressure fall **nearly to zero before the pump triggered**. This is an observation pending timestamp alignment; do not assign a numeric minimum or assume a particular failed component until the Pi camera and HA pressure/electrical/flow logs are reviewed.
- **Dependency:** First validate the new glycerin-filled mechanical gauge/camera and independently calibrated upstream/downstream pressure readings across another pump cycle. Collect synchronized pump motor power (or other reliable electrical state) to distinguish **pressure-switch closure, motor energization, and hydraulic pressure recovery**.
- **Mechanical/hydraulic follow-up:** With verified telemetry, investigate actual switch cut-in/cut-out and repeatability, sensing port/tube obstruction or lag, switch adjustment/condition, pressure-tank precharge and drawdown, pressure losses, check valves or unusual flow restrictions, and actual pump startup/priming behavior as warranted by evidence. Keep troubleshooting observational first; follow isolation and electrical safety requirements if physical work becomes necessary.
- **Important:** A genuine near-zero pressure delay is potentially consequential. **Do not intentionally repeat severe pressure-loss cycles merely to collect data**; escalate inspection sooner if it recurs or causes loss of service or abnormal pump behavior. Telemetry work should not postpone a needed safety inspection.
- **Done when:** Confirmed, repeatable relationship between pressure, switch action, motor start, and subsequent pressure recovery; root cause and corrective action documented, with before/after data.

### 2026-11-13 — v0.3.2 static field validation (HA + Pi camera)
- **Data windows:** HA 08:00–10:22:53 MST; Pi 08:06–10:18:43 MST. 129 Pi rows, 126 detected, 3 HTTP 503 camera failures; 49 detections during the settled 09:20–10:18 period.
- **Firmware calibration step:** 08:45:17 MST, upstream 43.51→34.52 PSI (−8.99 PSI), downstream 43.62→36.01 PSI (−7.61 PSI), with no corresponding sensor-voltage step. This is the v0.3.2 software correction and should not be interpreted as a pressure event.
- **Quiet zero-flow comparison (09:20–10:18):** HA upstream median **34.84 PSI** (n=1719, SD 0.083); downstream median **36.28 PSI** (n=1702, SD 0.113); synchronized median downstream−upstream **+1.44 PSI** (n=1683, SD 0.089). Pi angle median 149.323° from 49 detections, giving **~34.96 PSI** using the provisional camera conversion (SD ~0.43 PSI); user's visual reading near this period was **~34.5 PSI**.
- **Interpretation:** Upstream agrees with camera at this low-pressure operating point; downstream has a persistent ~1.4 PSI positive relative bias here. This does **not** establish calibration at high pressure: next pumped cycle after v0.3.2 is needed before changing slopes/offsets.
- **Raw downstream anomaly:** Single voltage entity value **0.000 V** at 08:51:05 MST and `downstream_interval_min` **13.93 PSI** at 08:51:10, despite stable filtered pressure. Treat as isolated measurement/data-path glitch until verified—not a verified hydraulic collapse. This is distinct from the real startup pressure dip observed at 08:02 earlier.
- **Flow history:** Meter 1 pulse counter increased 128,496→177,976 between 08:00 and 08:39:30, corresponding to nominal **39.61 US gal** through the sensor over that export segment (still using uncalibrated 330 pulses/L). HA flow-entity history ended when values stopped changing; no implication of firmware dropout.
- **Next:** Keep current firmware collecting; repeat across pressure rise/cutoff with new gauge and synchronized timestamps. For optical data, reject sudden implausible angle jumps and retain photos; do not fit pressure calibration to one quiet low-pressure segment alone.

## Calibration and maintenance log

| Date | Event | Notes |
| --- | --- | --- |
| 2026-11-10/11 | Pressure v0.2.0 provisional calibration | Two observed static anchors from old mechanical gauge; gauge later found sticky |
| 2026-11-11 | Pi webcam detected stuck gauge and jump after tapping | Prior gauge readings **not suitable as a trusted standard** |
| 2026-11-12 | New glycerin-filled mechanical gauge installed | Camera has **not** yet been repositioned/recalibrated for replacement gauge |
| 2026-11-12 | Flow firmware v0.3.0 observed online | Flow 1 pulses/GPM/gallons live; Flow 2 absent/zero; nominal 330 pulses/L |
| 2026-11-13 | New gauge Pi burst and HA matched | ~20 accepted camera angles from pressure rise and post-cycle hold; 36.0 / 61.75 PSI manual references; occasional faulty optical readings rejected |
| 2026-11-13 | Firmware v0.3.2 provisional pressure correction committed | Up: 1.243033×prior PSI−19.307025; downstream: 1.204790×prior PSI−16.281993; **field validation and OTA still pending** |

## Immediate next session

1. **WS-001:** Run the five-gallon meter-1 calibration. Capture pre/post total pulse counts, measured volume and approximate flow.
2. **WS-002:** Reposition the webcam and test OpenCV on the glycerin-filled gauge, then gather a full pump-cycle photo/angle dataset.
3. **WS-003:** Use new gauge readings to reassess digital pressure coefficients *before* changing firmware again.

*Keep plumbing installation decisions with the field engineer. Do not modify stable sensor wiring without a specific diagnosed reason.*
