# Troubleshooting

Common issues and fixes for the ESP32 Smart Touchless Water Dispenser.

---

## Power and boot

### Board will not flash

- Hold **BOOT** (and sometimes press **EN**) during upload.
- Lower upload speed to 115200.
- Try another USB cable / port (data-capable cable).
- Install or update CP210x / CH340 USB-UART drivers.

### Brownout / random resets when pump starts

- Pump is loading the same weak USB supply.
- Move pump to a separate PSU; common GND only.
- Add bulk capacitance on the logic 5V rail.
- Check for loose ground wires.

---

## Wi-Fi and web portal

### `WiFi failed` on Serial

- SSID/password typo in sketch.
- Access point is **5 GHz only** (ESP32 needs 2.4 GHz).
- Router client isolation / MAC filter.
- Too far from AP; test closer.

### Portal loads but status stuck on Loading

- `/status` failing: open browser devtools Network tab.
- Mixed content not applicable on plain HTTP LAN.
- Confirm you are on the same subnet/VLAN.

### Cannot reach device after router reboot

- DHCP address changed. Check Serial for new IP or set a DHCP reservation.

### Security concern

- HTTP API has **no password**. Treat as a lab/LAN device.
- Do not port-forward to the internet without adding authentication.

---

## Ultrasonic / gestures

### No hand detection

- Wiring TRIG/ECHO swapped.
- Sensor VCC not powered.
- Hand outside 6 to 30 cm band (`MIN_DIST_CM` / `MAX_DIST_CM`).
- Serial / web distance stuck near 999 = no valid echo.

### Constant false hand present

- Hard surface reflecting inside the valid range.
- Re-aim sensor; add side foam shroud; raise minimum distance.

### Mode A triggers too easily

- Increase `MIN_DETECTION_MS` (default 150).

### Cannot enter Mode B

- Hold a full second without leaving the beam (`HOLD_TIME_MS = 1000`).
- LCD should leave idle prompt when hold succeeds.

### Mode B locks while hand still present

- Firmware only locks after hand is **absent** for 3 s. If it locks early, check noisy `handPresent` toggling (distance jitter around band edges). Stabilize mounting and averaging.

### Volume map feels inverted or wrong

- Recalibrate **Cal Min** at closer position and **Cal Max** at farther.
- Firmware forces closer = less even if values were swapped, but a tiny calibrated span (&lt;1 cm) falls back to full 6 to 30 cm range.

---

## Pump and relay

### Pump never turns on

- Active-high module with active-low firmware: invert `setPump()`.
- Relay IN not on GPIO 26.
- Pump PSU off or fuse blown.
- System locked (web / LCD `SYSTEM LOCKED`).

### Pump stuck on

- Software crash mid-pour (rare): power cycle.
- `PUMP_TIMEOUT_MS` should force stop after 3 minutes.
- Check for welded relay contacts on failing hardware.

### Pour volume wrong

- Redo flow calibration carefully with a true 1.0 L mark.
- Pump voltage sag changes flow vs calibration time.
- Air in line / weak prime.

### Stops immediately after start

- Confirm you flashed the version with `SENSOR_IGNORE_MS = 1000`.
- Hand still counted as stop edge: leave hand out after starting Mode A until you intend to stop (after 1 s).

---

## LCD

### Blank LCD

- Contrast pot.
- VCC/GND reverse.
- R/W not grounded.
- Backlight wiring.

### Random letters when motor starts

Classic EMI. Firmware re-inits LCD after each pump edge, but hardware still matters:

1. Flyback diode on relay coil.
2. Separate pump supply.
3. Decouple LCD VCC (100 nF + electrolytic).
4. Shorten LCD ribbon / wires.
5. Keep motor leads away from LCD data lines.

### Garbled only after long runtime

- Loose Dupont connectors.
- Overheating regulator.
- Try `reinitLCD` path is already called on pump edges; power-cycle to confirm hardware vs firmware.

---

## Calibration

### Flow cal rejected as too short / too long

- Under 3 s: container already partly full or pump extremely fast (use larger timed volume method / slower pump, or adjust code limits).
- Over 300 s: pump weak, clogged tube, or forgot to stop.

### Flow LCD showed distance (old builds)

- Current firmware shows `FLOW CAL 1L` and `Time: X.Xs` during flow test. Update sketch if you still see distance-only calibration text during flow.

### Distance cal unstable

- Hold hand still for full 2 s.
- Avoid multi-path reflections.
- Need more than 10 valid samples in the window.

---

## Statistics and NVS

### Totals reset after flash

- Normally NVS survives upload. Full erase / `esptool erase_flash` clears it.
- Web **Reset All Settings** also clears totals.

### Can remaining wrong

- Click **New Can Installed** after replacing bottle.
- Micro pours under 50 ml are ignored on purpose.

---

## Build errors

### `ledcAttach was not declared`

- Need Arduino-ESP32 **3.x**. On 2.x use legacy `ledcSetup` / `ledcAttachPin` API.

### `ESPAsyncWebServer.h: No such file`

- Install ESPAsyncWebServer and AsyncTCP libraries.

### Parallel build / multiple definition with PlatformIO

- Ensure only one copy of the sketch is compiled (`src/main.cpp` **or** root ino, not both).

---

## Getting help

When opening a GitHub issue, include:

1. ESP32 board model
2. Relay module type (active high/low) and pump voltage
3. Arduino core / PlatformIO versions
4. Serial boot log
5. `/status` JSON snapshot
6. What you expected vs what happened
