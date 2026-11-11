# ESP32 Pressure Transducer — 100 PSI

Measurement-only well/filtration monitor for an ESP32-S3-WROOM-1-N16R8 on an OSOYOO ESP32-S3 I/O breakout. Publishes provisionally calibrated upstream and downstream pressures plus raw diagnostic voltages to MQTT/Home Assistant. The old Rusco differential entity is deliberately retired until we validate flow-dependent pressure drop. Includes password-protected ArduinoOTA.

## Hardware

- ESP32-S3-WROOM-1-N16R8, OSOYOO ESP32-S3 breakout
- Two 5 V, 0–100 PSI analog pressure transducers (output **assumed** 0.5–4.5 V; verify by multimeter before connecting)
- Each sensor output: **10 kΩ series resistor** to GPIO4 (upstream) / GPIO5 (downstream); from the respective GPIO **10 kΩ to GND**, plus **0.1 µF ceramic to GND**
- Both sensor grounds must connect to ESP32 GND
- Sensor supplies: regulated **5 V**, verified with a multimeter
- Optional: 0.1 µF across each sensor's 5 V/GND; 100 µF electrolytic across the common 5 V sensor rail (**observe polarity and voltage rating**)

This gives `V_ADC = V_sensor / 2`: a 0.5–4.5 V sensor becomes 0.25–2.25 V at the ESP32 ADC. Never feed a 5 V signal directly into GPIO. Verify the actual wire colors. The OSOYOO breakout's external-power terminal rails may **not** be powered from the ESP32's USB connector; verify before wiring sensors. Never connect its external DC input to USB 5 V unless the board documentation specifically permits it.

**Important:** Firmware v0.2.0 uses a **provisional**, empirically derived two-point calibration based on a mechanical reference gauge. The gauge and the transducers have not yet been independently validated. Pressure values must **not** be used for pump protection or control; the ESP32 is **not** connected to pump-switch controls.

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

With MQTT discovery enabled in Home Assistant, **five** entities—upstream pressure, downstream pressure, both sensor voltages, and Wi-Fi RSSI—appear under **ESP32 Well Room**. At the first MQTT connection after upgrading to v0.2.0, firmware publishes an empty *retained* config to `homeassistant/sensor/well_pressure_filter_dp/config` to retire the misleading Rusco differential sensor. Historical HA recorder records are not intentionally deleted. Existing dashboards may need their obsolete card removed manually.

A hardware failure may produce an in-range voltage; the crude input-range fault check is **not** a safety interlock. Telemetry is for observation only.

## Calibration: v0.2.0 (provisional, 2026-11-10/11)

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

- No hardcoded device IP: DHCP by design. The router reservation can be added later.
- All program logic is local. MQTT requires only the local broker.
- This is a first-run firmware draft; verify wiring and compile/test on your actual board before unattended installation.
