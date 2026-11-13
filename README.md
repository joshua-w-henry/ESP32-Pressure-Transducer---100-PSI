# Water Supply — ESP32-S3 Pressure and Dual Flow Monitoring

**Project plan:** [Water Supply Engineering Backlog](BACKLOG.md) — priorities, calibration tasks, and completed milestones.

Measurement-only well/filtration monitor for an ESP32-S3-WROOM-1-N16R8 on an OSOYOO breakout. Firmware **v0.3.1** retains upstream/downstream pressure and dual GREDIA G1 Hall-effect flowmeters, and adds **adaptive pressure reporting** with per-interval raw pressure minima/maxima. The Rusco differential sensor remains retired; password-protected ArduinoOTA is unchanged.

## Hardware

- ESP32-S3-WROOM-1-N16R8, OSOYOO ESP32-S3 breakout
- Two 5 V, 0–100 PSI analog pressure transducers (output **assumed** 0.5–4.5 V; verify by multimeter before connecting)
- Each sensor output: **10 kΩ series resistor** to GPIO4 (upstream) / GPIO5 (downstream); from the respective GPIO **10 kΩ to GND**, plus **0.1 µF ceramic to GND**
- Both sensor grounds must connect to ESP32 GND
- Sensor supplies: regulated **5 V**, verified with a multimeter
- Optional: 0.1 µF across each sensor's 5 V/GND; 100 µF electrolytic across the common 5 V sensor rail (**observe polarity and voltage rating**)

This gives `V_ADC = V_sensor / 2`: a 0.5–4.5 V sensor becomes 0.25–2.25 V at the ESP32 ADC. Never feed a 5 V signal directly into GPIO. Verify the actual wire colors. The OSOYOO breakout's external-power terminal rails may **not** be powered from the ESP32's USB connector; verify before wiring sensors. Never connect its external DC input to USB 5 V unless the board documentation specifically permits it.

**Important:** Firmware v0.3.0 retains a **provisional**, empirically derived two-point calibration based on a mechanical reference gauge. The gauge and the transducers have not yet been independently validated. Pressure values must **not** be used for pump protection or control; the ESP32 is **not** connected to pump-switch controls.

## Flash and configure

1. Install VS Code + PlatformIO.
2. Copy `include/secrets.example.h` to `include/secrets.h` and fill in Wi-Fi (Keeper of the Hounds), MQTT broker address/credentials, and a unique strong OTA password. The real secrets file is ignored by Git.
3. On the ESP32-S3 board, use the native USB-C connector for serial (the right-hand socket when the antenna is at the top), and connect it to your PC. If flashing fails, consult the board's BOOT/RESET procedure.
4. Run `pio run -e usb -t upload`, followed by `pio device monitor -b 115200`.
5. Watch the console for a DHCP address, the reported voltages and MQTT connection. Reserve that IP on **Keeper of the Hounds** after DHCP assigns it.

## OTA after first USB flash

The device advertises `esp32-well-room.local` (mDNS; depends on network support). The `ota` PlatformIO environment reads the **local, uncommitted** `OTA_PASSWORD` from `include/secrets.h` and passes it to the uploader automatically.

```powershell
pio run -e ota -t upload --upload-port 192.168.86.X
```

For the first OTA update that changes the hostname, use the **current IP address** explicitly: the old firmware still advertises `well-pressure.local`. Subsequent updates can use `esp32-well-room.local` when mDNS resolves. For this deployment, use the assigned IP `192.168.86.31` (reserved on Keeper of the Hounds). No password is committed to GitHub. OTA requires the PC and device to be reachable on the LAN and typically uses TCP/UDP port 3232. OTA is unavailable if the ESP32 is offline; keep USB recovery access.

## MQTT

- State: `water/well/pressure/state` (JSON, retained; every 5 s)
- Availability: `water/well/pressure/availability` (`online` / `offline`, retained; offline via broker last will)
- Discovery: `homeassistant/sensor/well_pressure_*/config` (retained)

With MQTT discovery enabled in Home Assistant, the existing **five** pressure/voltage/Wi-Fi entities remain under **ESP32 Well Room**. Six new entities represent the two flowmeters: GPM, total gallons, and cumulative raw pulse count for each. At the first MQTT connection after upgrading to v0.2.0, firmware publishes an empty *retained* config to `homeassistant/sensor/well_pressure_filter_dp/config` to retire the misleading Rusco differential sensor. Historical HA recorder records are not intentionally deleted. Existing dashboards may need their obsolete card removed manually.

A hardware failure may produce an in-range voltage; the crude input-range fault check is **not** a safety interlock. Telemetry is for observation only.

## Adaptive hydraulic acquisition (v0.3.1)

ESP32 pressure ADC sampling **remains 40 Hz**, and pulse interrupts remain unchanged. Only the MQTT publication interval changes:

| Inferred state | MQTT reporting | Trigger/notes |
|---|---|---|
| Idle | Every 5 s | No recent detected activity |
| Initial hydraulic activity | Every 0.5 s for 12 s | First detected rise/fall in both pressure channels, or measured flow |
| Sustained hydraulic activity | Every 1 s | Inferred activity is recent |
| Settling | Every 1 s for up to 30 s after last detected activity | Returns to idle after 30 s without a new trigger |

**Activity is not definitive pump-on status:** both filtered pressure channels must move in the same direction by at least 0.16 PSI over the 3-second detection window, **or** either flowmeter must report more than 0.15 GPM. Actual faucet draws and tank pressure decreases can trigger active mode; motor status must be corroborated using electrical power.

New state payload fields (retained state topic `water/well/pressure/state`): `hydraulic_active` boolean, `upstream_min_psi`, `upstream_max_psi`, `downstream_min_psi`, `downstream_max_psi`, `pressure_window_ms`, `upstream_samples`, `downstream_samples`. Interval extrema are computed from the **unsmoothed voltage-calibrated pressure estimates** for each 40 Hz sample, and can reveal occasional spikes or noise as well as actual water-pressure transients. The existing pressure values are still exponentially filtered, and **pressure calibration coefficients have not changed**. Home Assistant discovers four additional interval min/max PSI entities; raw and filtered results are not interchangeable.

The end-to-end latency to enter active mode is up to about three seconds for pressure-only events, but instantaneous flow from Flowmeter 1 can trigger earlier. The Pi 3 camera automatic MQTT coordination is **not implemented in this release**; for a near-term pump-cycle calibration run, use a temporary capture burst invoking `well-gauge-auto.service` every ~10 s while preserving the images, and restore the normal systemd timer afterward. Run this without competing capture loops.

**First build/deployment:** Compile using `pio run -e ota` before flashing over OTA. After the update, verify that both flow totals/pulse counts survived the restart, that pressure sensors remain valid, and that new HA min/max sensors appear. No pump control is performed.

## Dual GREDIA G1 Hall flowmeters (v0.3.0)

### Wiring

- **Flow meter 1:** 5 V red, GND black, signal yellow through 10 kΩ series to **GPIO6**, with 15 kΩ from GPIO6 to GND.
- **Flow meter 2:** same arrangement to **GPIO7**.
- Both flowmeters share the ESP32 GND. GPIO inputs **must not** receive 5 V directly. The divider provides nominal 3.0 V at a 5 V HIGH (2.82 V at a 4.7 V HIGH).
- **No 0.1 µF capacitor on these pulse inputs.** Use capacitors across the supply if supply decoupling is needed, not on the pulse signal.
- These are *GPIO interrupt* digital inputs, not additional ADC channels. The firmware counts **rising** edges.

### Coefficients and telemetry

The nominal specification `frequency (Hz) = 5.5 × flow (L/min)` corresponds to **330 pulses/liter**, **3.0303 mL/pulse**, or approximately **1249.19 pulses/US gallon**. Each meter has an independent coefficient in `src/main.cpp`:

```cpp
constexpr float FLOW_1_PULSES_PER_LITER = 330.0f;
constexpr float FLOW_2_PULSES_PER_LITER = 330.0f;
```

- The firmware uses pulse delta over elapsed time every **1 second** to calculate GPM, without smoothing/dropping the actual counted pulses.
- All 11 Home Assistant entities publish on the existing MQTT state topic **every 5 seconds**.
- New Home Assistant entities: `Flow Meter 1 Rate`, `Flow Meter 2 Rate` (`gal/min`, device class `volume_flow_rate`); `Flow Meter 1 Total`, `Flow Meter 2 Total` (`gal`, class `water`, state class `total_increasing`); `Flow Meter 1 Pulses`, `Flow Meter 2 Pulses` (`pulses`, state class `total_increasing`).
- Raw MQTT keys: `flow1_gpm`, `flow2_gpm`, `flow1_total_gal`, `flow2_total_gal`, `flow1_pulses`, `flow2_pulses`, `flow_calibration_id`.
- `flow_calibration_id` is `gredia_G1_nominal_330ppl_v1`; the pressure `calibration_id` stays unchanged.

**Persistence:** Cumulative *raw pulse counts*, not gallons, are checkpointed to ESP32 NVS (`well-flow` namespace; `m1pulses`/`m2pulses` keys). The firmware writes ~3 seconds after the last flow pulse, at least every 15 minutes during continuous flow, and immediately before an OTA reboot. Unplanned power loss before a checkpoint can lose the newest pulses; erasing NVS resets history. Store total volume externally in HA/RTR for additional durability. A disconnected meter reporting zero cannot be distinguished from no flow using only these pulse inputs.

**Calibration:** These 330 pulses/L values are nominal. For each meter, measure water volume and accumulated pulse-count difference; calculate `pulses_per_liter = pulse_delta / measured_liters`. Verify at several flow rates. The two meters may be in series or on separate branches; do not sum their totals without knowing their actual placement.

At 1 GPM we expect approximately 20.8 Hz; at 8 GPM, approximately 166.6 Hz. Adaptive reporting and pump-state integration are intentionally deferred to a future revision.

## Pressure calibration: v0.2.0 coefficients retained (provisional, 2026-11-10/11)

The earlier v0.1.0 firmware assumed `P_nominal = 25 × (V_sensor - 0.5)`, with `V_sensor` derived from the calibrated ADC millivolts and the 2:1 resistor divider. At two static, no-flow operating points, the mechanical gauge and Home Assistant indicated:

| Reference gauge | Upstream v0.1 PSI | Downstream v0.1 PSI |
| ---: | ---: | ---: |
| 39.50 PSI | 25.20 PSI | 21.97 PSI |
| 51.50 PSI | 39.59 PSI | 36.38 PSI |

These points were observed on separate dates, from visually read gauge data and Home Assistant readings rounded to hundredths. Their absolute accuracy is unverified. They are **interim** corrections only.

The equivalent v0.2.0 correction to the old nominal pressure values is:

```text
Upstream calibrated PSI   = 0.83391244 × Upstream nominal PSI   + 18.48540653
Downstream calibrated PSI = 0.83275503 × Downstream nominal PSI + 21.20437196
```

The firmware implements these as independent voltage/pressure anchor pairs for `Calibration` in `src/main.cpp`:

- Upstream: `(1.5080 V, 39.5 PSI)` and `(2.0836 V, 51.5 PSI)`
- Downstream: `(1.3788 V, 39.5 PSI)` and `(1.9552 V, 51.5 PSI)`

The voltage anchor values are **calculated** from the old nominal PSI readings, **not** directly measured with a multimeter. The firmware keeps publishing `upstream_v`, `downstream_v` (sensor output estimated from ADC and divider), and `upstream_adc_mv` / `downstream_adc_mv` in MQTT state, allowing later analysis and reprocessing. Each payload also contains `"calibration_id": "static_20261110-11_2pt_v1"` for provenance. Historic Home Assistant pressures from before this firmware upgrade remain on the old nominal scale.

The Rusco differential pressure is intentionally **not** published in v0.2.0: the earlier no-flow 3.2 PSI discrepancy was mostly channel mismatch. Once we establish validated absolute calibration and flow references, we can restore a meaningful differential reading. It should approach zero at no-flow, accounting for any height or hydraulic differences.

Continue gathering simultaneous gauge and electronic readings over a wider pressure span, ideally as timestamped camera-based references. Confirm supply voltage, sensor linearity and noise before treating these as accurate readings.

The diagnostic serial output is once per second, samples occur at 40 Hz with exponential smoothing, and telemetry is measurement-only. Pump state should be corroborated with electric power data.


## Notes

- On UNICORN, use `git pull` in the project directory; verify that ignored local `include/secrets.h` still has the exact SSID `Keeper of the Hounds`. Build with `pio run -e ota` before `pio run -e ota -t upload --upload-port 192.168.86.31`.
- Flowmeter code has **not** been hardware-built or OTA-tested here. Confirm PlatformIO compiles and validates pulse counts before relying on totals.

- No hardcoded device IP: DHCP by design. The router reservation can be added later.
- All program logic is local. MQTT requires only the local broker.
- This is a first-run firmware draft; verify wiring and compile/test on your actual board before unattended installation.
