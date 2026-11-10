# ESP32 Pressure Transducer — 100 PSI

Measurement-only well/filtration monitor for an ESP32-S3-WROOM-1-N16R8 on an OSOYOO ESP32-S3 I/O breakout. Publishes upstream pressure, downstream pressure, Rusco filter differential pressure and diagnostic voltages to MQTT/Home Assistant. Includes password-protected ArduinoOTA.

## Hardware

- ESP32-S3-WROOM-1-N16R8, OSOYOO ESP32-S3 breakout
- Two 5 V, 0–100 PSI analog pressure transducers (output **assumed** 0.5–4.5 V; verify by multimeter before connecting)
- Each sensor output: **10 kΩ series resistor** to GPIO4 (upstream) / GPIO5 (downstream); from the respective GPIO **10 kΩ to GND**, plus **0.1 µF ceramic to GND**
- Both sensor grounds must connect to ESP32 GND
- Sensor supplies: regulated **5 V**, verified with a multimeter
- Optional: 0.1 µF across each sensor's 5 V/GND; 100 µF electrolytic across the common 5 V sensor rail (**observe polarity and voltage rating**)

This gives `V_ADC = V_sensor / 2`: a 0.5–4.5 V sensor becomes 0.25–2.25 V at the ESP32 ADC. Never feed a 5 V signal directly into GPIO. Verify the actual wire colors. The OSOYOO breakout's external-power terminal rails may **not** be powered from the ESP32's USB connector; verify before wiring sensors. Never connect its external DC input to USB 5 V unless the board documentation specifically permits it.

**Important:** The pressure values initially use nominal 0.5–4.5 V / 0–100 PSI calibration. They are not validated until compared with the mechanical gauge. The ESP32 is **not** connected to pump-switch controls.

## Flash and configure

1. Install VS Code + PlatformIO.
2. Copy `include/secrets.example.h` to `include/secrets.h` and fill in Wi-Fi (Keeper of the Hounds), MQTT broker address/credentials, and a unique strong OTA password. The real secrets file is ignored by Git.
3. On the ESP32-S3 board, use the native USB-C connector for serial (the right-hand socket when the antenna is at the top), and connect it to your PC. If flashing fails, consult the board's BOOT/RESET procedure.
4. Run `pio run -e usb -t upload`, followed by `pio device monitor -b 115200`.
5. Watch the console for a DHCP address, the reported voltages and MQTT connection. Reserve that IP on **Keeper Of the Hounds** after DHCP assigns it.

## OTA after first USB flash

The device advertises `esp32-well-room.local` (mDNS; depends on network support). The `ota` PlatformIO environment reads the **local, uncommitted** `OTA_PASSWORD` from `include/secrets.h` and passes it to the uploader automatically.

```powershell
pio run -e ota -t upload --upload-port 192.168.86.X
```

For the first OTA update that changes the hostname, use the **current IP address** explicitly: the old firmware still advertises `well-pressure.local`. Subsequent updates can use `esp32-well-room.local` when mDNS resolves. Use the actual DHCP-reserved IP in place of X. No password is committed to GitHub. OTA requires the PC and device to be reachable on the LAN and typically uses TCP/UDP port 3232. OTA is unavailable if the ESP32 is offline; keep USB recovery access.

## MQTT

- State: `water/well/pressure/state` (JSON, retained; every 5 s)
- Availability: `water/well/pressure/availability` (`online` / `offline`, retained; offline via broker last will)
- Discovery: `homeassistant/sensor/well_pressure_*/config` (retained)

With MQTT discovery enabled in Home Assistant, entities for upstream pressure, downstream pressure, differential pressure, both raw transducer voltage readings, and Wi-Fi RSSI should appear under **ESP32 Well Room**.

A hardware failure may produce an in-range voltage; the crude input-range fault check is **not** a safety interlock. Telemetry is for observation only.

## Calibration

In `src/main.cpp`, set independent `upstream` and `downstream` voltage-versus-pressure calibration points after recording electrical output and mechanical pressure gauge readings at at least two pressures, ideally near the 40 and 60 PSI switch points. In a no-flow condition both sensors should read similar pressure; during flow, upstream minus downstream measures the drop across the Rusco.

The diagnostic serial output is once per second, with samples at 40 Hz and exponential digital smoothing. Don't interpret every pressure rise as a confirmed pump-on indication; pump state needs validation.

## Notes

- No hardcoded device IP: DHCP by design. The router reservation can be added later.
- All program logic is local. MQTT requires only the local broker.
- This is a first-run firmware draft; verify wiring and compile/test on your actual board before unattended installation.
