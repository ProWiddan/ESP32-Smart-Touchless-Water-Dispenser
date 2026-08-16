# Wiring Guide

Hardware connections for the ESP32 Smart Touchless Water Dispenser.

---

## Pin summary

| Signal | ESP32 GPIO | Direction | Device pin |
|--------|------------|-----------|------------|
| TRIG | 23 | OUT | HC-SR04 TRIG |
| ECHO | 22 | IN | HC-SR04 ECHO (level shift recommended) |
| LCD RS | 19 | OUT | LCD RS |
| LCD EN | 21 | OUT | LCD E |
| LCD D4 | 18 | OUT | LCD D4 |
| LCD D5 | 17 | OUT | LCD D5 |
| LCD D6 | 16 | OUT | LCD D6 |
| LCD D7 | 15 | OUT | LCD D7 |
| Backlight PWM | 25 | OUT | LCD LED+ via transistor/resistor, or module BL |
| Relay | 26 | OUT | Relay IN (**active LOW**) |

LCD R/W must be tied to **GND** (write-only).

---

## Power architecture (recommended)

```text
                    +12V (or pump-rated supply)
                           |
                      [FUSE]
                           |
              +------------+------------+
              |                         |
           PUMP +                    RELAY COM
              |                         |
           PUMP - ---+             RELAY NO ---- to pump -
                     |                  |
                    GND <------+   RELAY coil to module
                               |
  USB 5V or 5V PSU ----+-------+---- ESP32 5V/VIN (per board)
                       |
                      LCD VCC, HC-SR04 VCC, relay module VCC
                       |
                      GND (common with pump GND)
```

**Rules**

1. Pump current never flows through the ESP32 regulator.
2. All grounds are common.
3. Relay module VCC matches its logic rating (often 5V; some are 3.3V).
4. If using a bare transistor + relay coil: add **1N4007** diode across the coil (stripe/cathode toward +V).

---

## HC-SR04 ultrasonic

| HC-SR04 | Connect to |
|---------|------------|
| VCC | 5V |
| GND | GND |
| TRIG | GPIO 23 |
| ECHO | GPIO 22 via divider (optional but safer) |

### Optional ECHO level divider

```text
ECHO (5V) ---- 2.2k ----+---- GPIO 22
                        |
                       3.3k
                        |
                       GND
```

Mount the sensor facing the hand area, roughly 6 to 30 cm operating range. Avoid aiming at shiny sinks or walls that cause constant reflections inside the valid band.

---

## 16x2 LCD (HD44780 parallel 4-bit)

| LCD pin | Connect to |
|---------|------------|
| 1 VSS | GND |
| 2 VDD | 5V |
| 3 VO | Contrast pot wiper |
| 4 RS | GPIO 19 |
| 5 R/W | GND |
| 6 E | GPIO 21 |
| 7-10 D0-D3 | NC (4-bit mode) |
| 11 D4 | GPIO 18 |
| 12 D5 | GPIO 17 |
| 13 D6 | GPIO 16 |
| 14 D7 | GPIO 15 |
| 15 A (LED+) | 5V via 220 ohm **or** GPIO 25 driver |
| 16 K (LED-) | GND |

Contrast pot: 10k between 5V and GND, wiper to VO.

### Backlight on GPIO 25

Firmware uses LEDC PWM on GPIO 25. Drive the backlight with an NPN/N-MOSFET low-side switch; do not sink high LED current into the ESP32 pin directly if the LCD draws more than a few mA without a series resistor.

Idle brightness is low (`BACKLIGHT_IDLE = 40`), active is full (`255`).

---

## Relay and pump

Firmware **active LOW**:

```cpp
digitalWrite(RELAY_PIN, on ? LOW : HIGH);
```

| If your module is... | Action |
|----------------------|--------|
| Active LOW (common) | Wire IN to GPIO 26, no code change |
| Active HIGH | Invert in `setPump()` or use a LOW-trigger module |
| SSR | Confirm logic level and load type (DC pump vs AC) |

### Pump plumbing tips

- Use food-safe tubing for drinking water.
- Keep electronics above any leak path.
- Add a drip tray during bring-up.
- Consider a float switch in series with the pump supply for hard low-water cutout (optional hardware).

---

## EMI / LCD garbage when pump starts

Motor and relay coils couple noise into the LCD parallel bus.

**Hardware mitigations (best results)**

1. Flyback diode on coil.
2. Separate pump PSU + short power leads.
3. 100 nF ceramic + 47 to 100 uF electrolytic on LCD VCC near the header.
4. Twisted or short LCD data wires; avoid routing next to pump leads.
5. Snubber or RC across motor if needed.

**Software mitigations (already in firmware)**

- Toggle relay only on state change.
- Quiet window after switch.
- `lcd.begin()` re-init after pump edge.
- Full 16-character line overwrite (no `clear()` every frame).

---

## Mechanical layout suggestions

- Sensor above or beside the spout, clear hand path.
- LCD at eye level on the enclosure front.
- ESP32 and relay in a dry compartment.
- Strain-relief all cables.

---

## Changing pins

Edit the block at the top of `water_dispenser.ino`:

```cpp
const int TRIG_PIN      = 23;
const int ECHO_PIN      = 22;
const int LCD_RS        = 19;
const int LCD_EN        = 21;
const int LCD_D4        = 18;
const int LCD_D5        = 17;
const int LCD_D6        = 16;
const int LCD_D7        = 15;
const int LCD_BACKLIGHT = 25;
const int RELAY_PIN     = 26;
```

Avoid strapping pins used for boot (0, 2, 12, 15 can be sensitive on some boards). GPIO 15 is used here as LCD D7; if your board fails to boot, move D7 to another free GPIO.
