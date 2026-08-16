# ESP32 Smart Touchless Water Dispenser

Hands-free water dispenser controlled by an ultrasonic gesture sensor, with a 16x2 LCD status display and a Wi-Fi web portal for calibration, statistics, and remote control.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-green.svg)](#)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-00979D.svg)](#)

---

## Features

- **Touchless gestures** via HC-SR04 ultrasonic sensor
- **Mode A**: quick wave for continuous manual pour (up to 2.0 L)
- **Mode B**: hold hand to select volume by distance (closer = less, farther = more)
- **1 second no-stop lock** after pump starts (prevents false stop from residual hand)
- **16x2 LCD** live status with EMI-hardened redraw after relay switching
- **Wi-Fi web portal** for live distance, stats, dispense, calibration, lock, reset
- **Flow-rate calibration** (timed 1 L fill) stored in NVS
- **Distance calibration** for Mode B min/max volume positions
- **Can level tracking** (default 18.9 L bottle water can)
- **Lifetime statistics** (total volume, pour count, average pour, cans used)
- **System lock** from web (admin disable)
- **Safety**: 3 minute pump timeout, ignore micro-dispenses under 50 ml, low-water warning

---

## Demo / how it works

| Gesture | Result |
|--------|--------|
| Quick wave (stable 150 ms+, removed before 1 s) | Mode A manual pour |
| Second wave during pour (after 1 s) | Stop pour early |
| Hold hand 1 s+ | Enter Mode B volume select |
| Move hand closer / farther in Mode B | Live volume 0.1 to 2.0 L in 0.1 L steps |
| Remove hand and stay away 3 s | Lock selected volume |
| Wait 1 s after lock, then swipe | Start dispensing locked volume |

### Live Action & Interface Preview

| Web Portal Interface (`WebUI Demo.mp4`) | Hands-Free Gesture Demo (`Project Live Demo.mp4`) |
| :---: | :---: |
| <video src="WebUI%20Demo.mp4" controls width="100%"></video> | <video src="Project%20Live%20Demo.mp4" controls width="100%"></video> |
| *Live web portal calibration, volume control & stats* | *Touchless gesture control, LCD feedback & physical dispensing* |

---

```text
IDLE
  | wave
  v
GESTURE_DETECT ---- quick remove ----> MANUAL_POUR ----> COMPLETE ----> IDLE
  | hold 1s                              ^ stop wave / max 2L
  v
SELECT_VOLUME (live distance map)
  | hand gone 3s
  v
VOLUME_LOCKED -- wait 1s --> swipe --> DISPENSING --> COMPLETE --> IDLE
```

---

## Hardware

### Bill of materials

| Item | Qty | Notes |
|------|-----|-------|
| ESP32 DevKit (30+ pin) | 1 | Wi-Fi required |
| HC-SR04 ultrasonic sensor | 1 | 5V friendly; use level shift on ECHO if needed |
| 16x2 LCD HD44780 | 1 | 4-bit mode |
| N-channel / relay module | 1 | **Active LOW** coil drive assumed |
| Water pump (12V/5V DC) | 1 | Matched to your supply and tubing |
| Flyback diode (1N4007) | 1 | Across relay coil if bare relay |
| 10k trim pot | 1 | LCD contrast |
| Jumper wires, breadboard or PCB | - | Prefer short LCD leads |
| Power supply | 1 | Separate rails recommended for pump vs logic |

### Pin map

| Function | ESP32 GPIO | Notes |
|----------|------------|--------|
| Ultrasonic TRIG | 23 | Output |
| Ultrasonic ECHO | 22 | Input (level-shift from 5V if required) |
| LCD RS | 19 | |
| LCD EN | 21 | |
| LCD D4 | 18 | |
| LCD D5 | 17 | |
| LCD D6 | 16 | |
| LCD D7 | 15 | |
| LCD backlight | 25 | PWM via LEDC |
| Relay / pump | 26 | **Active LOW** (LOW = pump ON) |

> Change pins at the top of `water_dispenser.ino` if your wiring differs.

### Wiring notes

1. **Common ground** between ESP32, sensor, LCD, and relay module.
2. **Do not power a high-current pump from the ESP32 5V pin.** Use a separate supply; share GND only.
3. Put a **flyback diode** across mechanical relay coils (cathode to coil +).
4. HC-SR04 ECHO is 5V. Many ESP32 boards tolerate it; for safety use a resistor divider (e.g. 2.2k/3.3k) or a level shifter.
5. LCD contrast pot: one end 5V, other GND, wiper to VO.
6. If the LCD shows random characters when the pump starts, see [Troubleshooting](#troubleshooting).

See [docs/WIRING.md](docs/WIRING.md) for a full connection table and power diagram description.

---

## Software

### Requirements

- Arduino IDE 2.x **or** PlatformIO
- ESP32 board support (**Arduino-ESP32 core 3.x** recommended for `ledcAttach`)
- Libraries:
  - `LiquidCrystal` (built-in)
  - `Preferences` (built-in)
  - `WiFi` (built-in)
  - [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
  - [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) (ESP32)

### Arduino IDE setup

1. Install **esp32** by Espressif via Boards Manager.
2. Install **ESPAsyncWebServer** and **AsyncTCP** (Library Manager or ZIP from GitHub).
3. Open `water_dispenser.ino`.
4. Set board to your ESP32 Dev Module.
5. Edit Wi-Fi credentials:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

6. Upload. Open Serial Monitor at **115200** baud.
7. Note the printed URL: `WiFi OK -> http://x.x.x.x`

### PlatformIO (optional)

```bash
# from project root with platformio.ini present
pio run -t upload
pio device monitor -b 115200
```

See [docs/SETUP.md](docs/SETUP.md) for detailed install steps.

---

## First-time calibration

Do this once after install (or after hardware changes).

### 1. Flow rate (seconds per litre)

1. Open the web portal.
2. Place an empty **1.0 L** marked container under the spout.
3. Click **Start Flow Test** (pump ON; LCD shows `FLOW CAL 1L` and live time).
4. When the container reaches exactly 1.0 L, click **Stop (1L Done)**.
5. Firmware saves `secondsPerLiter` to flash.

Minimum accepted duration: 3 s. Maximum: 300 s.

### 2. Distance (Mode B)

1. Hold hand steady at the **closer** position (minimum volume 0.1 L) for 2 s, click **Cal Min**.
2. Hold hand steady at the **farther** position (maximum volume 2.0 L) for 2 s, click **Cal Max**.
3. Mapping always enforces: **closer = less volume, farther = more volume**.

### 3. New can

When you install a full bottle, click **New Can Installed (18.9L)** so remaining volume resets.

---

## Web portal

Browse to `http://<esp32-ip>/` on the same Wi-Fi network.

| Section | Purpose |
|---------|---------|
| Hand distance | Live cm reading (valid gesture band 6 to 30 cm) |
| Status | State machine + remaining can + selected volume + uptime |
| Statistics | Total dispensed, count, average pour, cans used, flow rate, can % bar, cal distances |
| Dispense | Slider / presets, remote lock-and-swipe dispense, manual ON/OFF |
| Calibration | Flow test + distance min/max |
| Maintenance | New can, lock/unlock, factory reset |

API reference: [docs/API.md](docs/API.md)

---

## Configuration constants

Edit these in `water_dispenser.ino` as needed:

| Constant | Default | Meaning |
|----------|---------|---------|
| `MIN_DIST_CM` | 6.0 | Hand must be at least this close |
| `MAX_DIST_CM` | 30.0 | Hand farther than this = absent |
| `MIN_VOLUME_L` | 0.1 | Smallest selectable pour |
| `MAX_VOLUME_L` | 2.0 | Largest pour / Mode A cap |
| `CAN_CAPACITY_L` | 18.9 | Full can size (L) |
| `LOW_WATER_THRESH` | 1.0 | LCD low-water warning (L) |
| `HOLD_TIME_MS` | 1000 | Hold to enter Mode B |
| `VOLUME_ABSENT_LOCK_MS` | 3000 | Hand-away time to lock Mode B volume |
| `VOLUME_SWIPE_DELAY_MS` | 1000 | Wait after lock before swipe accepted |
| `LOCK_TIMEOUT_MS` | 10000 | Cancel locked volume if no swipe |
| `SENSOR_IGNORE_MS` | 1000 | No hand-stop after pump start |
| `PUMP_TIMEOUT_MS` | 180000 | Hard pump cutoff (3 min) |
| `MIN_DETECTION_MS` | 150 | Min stable presence for Mode A |
| `MIN_DISPENSED_COUNT` | 0.05 | Ignore stats under 50 ml |

---

## Project structure

```text
esp32_water_dispenser/
├── README.md                 # This file
├── LICENSE                   # MIT
├── water_dispenser.ino       # Main firmware
├── platformio.ini            # Optional PlatformIO config
├── .gitignore
└── docs/
    ├── SETUP.md              # Install and flash guide
    ├── WIRING.md             # Hardware connections
    ├── API.md                # HTTP endpoints and JSON
    └── TROUBLESHOOTING.md    # Common issues
```

---

## Safety

- This project controls a water pump and mains-adjacent hardware. Use at your own risk.
- Never leave the system unattended during first tests.
- Use a fused supply and proper waterproofing around liquid and electronics.
- The 3 minute software timeout is a backup, not a substitute for a proper float switch or hardware interlock if you need one.
- Keep mains wiring away from the ESP32 and LCD.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Random LCD characters when pump starts | Relay/motor EMI | Flyback diode, separate pump PSU, short LCD wires; firmware already re-inits LCD after relay |
| Wi-Fi fails | Wrong SSID/password / 5 GHz only AP | Use 2.4 GHz Wi-Fi; check Serial log |
| Always Mode A / never Mode B | Hold shorter than 1 s | Hold steadily over sensor for full second |
| Mode B volume wrong | Bad distance cal | Recalibrate min (close) and max (far) |
| Volumes always short/long | Bad flow cal | Redo 1 L timed calibration carefully |
| Pump never runs | Active-high relay module | Invert logic in `setPump()` or use active-low module |
| False triggers | Wall / sink reflections | Raise `MIN_DETECTION_MS`, adjust sensor aim, shield sides |

More detail: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)

---

## License

MIT License. See [LICENSE](LICENSE).

---

## Contributing

Issues and pull requests are welcome. Please:

1. Describe hardware (ESP32 board, relay type, pump voltage).
2. Include Serial logs when reporting bugs.
3. Keep pin defaults unchanged unless documented.

---

## Acknowledgments

- Espressif ESP32 Arduino core
- me-no-dev ESPAsyncWebServer / AsyncTCP
- Classic HD44780 parallel LCD interface
