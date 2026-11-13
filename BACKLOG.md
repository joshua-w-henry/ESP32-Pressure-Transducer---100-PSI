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
**Status:** BLOCKED BY WS-002 (or manual readings)
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

## Calibration and maintenance log

| Date | Event | Notes |
| --- | --- | --- |
| 2026-11-10/11 | Pressure v0.2.0 provisional calibration | Two observed static anchors from old mechanical gauge; gauge later found sticky |
| 2026-11-11 | Pi webcam detected stuck gauge and jump after tapping | Prior gauge readings **not suitable as a trusted standard** |
| 2026-11-12 | New glycerin-filled mechanical gauge installed | Camera has **not** yet been repositioned/recalibrated for replacement gauge |
| 2026-11-12 | Flow firmware v0.3.0 observed online | Flow 1 pulses/GPM/gallons live; Flow 2 absent/zero; nominal 330 pulses/L |

## Immediate next session

1. **WS-001:** Run the five-gallon meter-1 calibration. Capture pre/post total pulse counts, measured volume and approximate flow.
2. **WS-002:** Reposition the webcam and test OpenCV on the glycerin-filled gauge, then gather a full pump-cycle photo/angle dataset.
3. **WS-003:** Use new gauge readings to reassess digital pressure coefficients *before* changing firmware again.

*Keep plumbing installation decisions with the field engineer. Do not modify stable sensor wiring without a specific diagnosed reason.*
