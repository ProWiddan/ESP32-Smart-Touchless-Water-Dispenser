# Setup Guide

Step-by-step instructions to build, flash, and commission the ESP32 Smart Touchless Water Dispenser.

---

## 1. Gather hardware

See the BOM in the main [README](../README.md#bill-of-materials) and wire according to [WIRING.md](WIRING.md).

Before applying power:

- [ ] Common GND connected
- [ ] Pump on its own supply (not ESP 5V pin)
- [ ] Relay logic matches firmware (active LOW by default)
- [ ] LCD contrast pot adjusted so text is visible
- [ ] No shorts on breadboard power rails

---

## 2. Install Arduino IDE toolchain

### 2.1 Arduino IDE

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. **File → Preferences → Additional boards manager URLs**, add:

```text
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

3. **Tools → Board → Boards Manager** → search `esp32` → install **esp32 by Espressif Systems** (3.x).
4. **Tools → Board** → select your module (e.g. **ESP32 Dev Module**).
5. **Tools → Port** → select the serial port of the board.
6. Recommended tools settings:
   - Upload Speed: `921600` (or `115200` if uploads fail)
   - CPU Frequency: `240MHz`
   - Flash Frequency: `80MHz`
   - Partition Scheme: `Default 4MB with spiffs` (or any scheme with enough app space)

### 2.2 Libraries

Install via **Library Manager** or clone into `Documents/Arduino/libraries`:

| Library | Source |
|---------|--------|
| ESP Async WebServer | https://github.com/me-no-dev/ESPAsyncWebServer |
| AsyncTCP | https://github.com/me-no-dev/AsyncTCP |

Built-in (no install): `LiquidCrystal`, `Preferences`, `WiFi`.

> If Library Manager cannot find AsyncTCP / ESPAsyncWebServer, install from GitHub ZIP: **Sketch → Include Library → Add .ZIP Library**.

---

## 3. Configure firmware

1. Open `water_dispenser.ino`.
2. Set Wi-Fi (2.4 GHz network required):

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

3. Confirm pin defines match your wiring (top of file).
4. Optional: tune timing constants (`HOLD_TIME_MS`, `SENSOR_IGNORE_MS`, etc.).

**Do not commit real passwords.** The repo ships with placeholders.

---

## 4. Upload

1. Connect ESP32 USB.
2. Hold **BOOT** if your board needs manual download mode.
3. Click **Upload**.
4. Open **Serial Monitor** at **115200** baud.
5. Expected boot log (example):

```text
=== ESP32 Touchless Water Dispenser ===
Connecting WiFi......
WiFi OK -> http://192.168.1.42
HTTP server started
Loaded sec/L=12.00 minD=8.0 maxD=25.0 remain=18.90 locked=0 totalVol=0.00 cnt=0
```

6. LCD should show `Wave Hand Over` / `Sensor to Start` (or `SYSTEM LOCKED` if previously locked).

---

## 5. PlatformIO alternative

```bash
cd esp32_water_dispenser
pio pkg install
pio run -t upload
pio device monitor -b 115200
```

Edit `platformio.ini` if your board ID or port differs.

For Arduino IDE style single `.ino` layout, keep the sketch at project root or under `src/main.cpp` (copy contents) depending on your preference. This repo keeps a single `water_dispenser.ino` for Arduino IDE users; with PlatformIO you may rename/copy to `src/main.cpp`.

---

## 6. Commissioning checklist

1. Open `http://<ip>/` from a phone or PC on the same LAN.
2. Confirm **Hand Distance** updates when you move a hand over the sensor.
3. Run **Flow calibration** with a marked 1 L container.
4. Run **Distance Cal Min** and **Cal Max**.
5. Test Mode A (quick wave) and Mode B (hold, adjust height, leave 3 s, wait 1 s, swipe).
6. Click **New Can Installed** after mounting a full bottle.
7. Verify pump stops at target volume and that a wave cannot stop pour during the first second.

---

## 7. Updating firmware later

- Settings in NVS (`Preferences` namespace `dispenser`) survive sketch uploads.
- **Reset All Settings** on the web portal clears NVS to defaults.
- Changing `CAN_CAPACITY_L` in code does not rewrite remaining can until New Can or Reset.

---

## 8. Network tips

- ESP32 uses **station mode** only (joins your router).
- Many ESP32 modules support **2.4 GHz only**.
- Static DHCP lease on the router makes the portal IP stable.
- No authentication is implemented on the HTTP API. Do not expose the device to the public internet without adding auth or a VPN.
