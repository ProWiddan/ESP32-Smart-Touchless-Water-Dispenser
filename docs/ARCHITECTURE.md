# Architecture

Software design overview for maintainers and contributors.

---

## High-level blocks

```text
+-------------+     +----------------+     +-----------+
| HC-SR04     | --> | Gesture / FSM  | --> | Relay/Pump|
+-------------+     |  (loop)        |     +-----------+
                    |                |
+-------------+     |                |     +-----------+
| Web clients | <-> | Async HTTP     |     | 16x2 LCD  |
+-------------+     | + Preferences  | --> +-----------+
                    +----------------+
```

- **Sense**: averaged ultrasonic distance every ~60 ms
- **Decide**: finite state machine in `loop()`
- **Actuate**: active-low relay via `setPump()`
- **Display**: throttled LCD updates with EMI-safe redraw
- **Configure / observe**: ESPAsyncWebServer + NVS Preferences

---

## Finite state machine

| State | Pump | Entry | Exit |
|-------|------|-------|------|
| `STATE_IDLE` | off | boot / complete / timeout | hand edge -> gesture detect |
| `STATE_GESTURE_DETECT` | off | hand appeared | quick leave -> Mode A; hold 1s -> Mode B; cancel -> idle |
| `STATE_MANUAL_POUR` | on | Mode A start | stop wave (after 1s lock) / 2L cap / timeout |
| `STATE_SELECT_VOLUME` | off | Mode B | hand absent 3s -> locked; hand present keeps mapping |
| `STATE_VOLUME_LOCKED` | off | volume lock | wait 1s then swipe -> dispensing; 10s timeout -> idle |
| `STATE_DISPENSING` | on | swipe / target | target reached / stop wave after 1s / timeout |
| `STATE_COMPLETE` | off | stop | 2.5 s -> idle |
| `STATE_SYSTEM_LOCKED` | off | web lock | unlock |
| `STATE_CALIBRATING` | flow:on / dist:off | web cal | cal endpoints finish |

### Mode A timing

1. Hand present edge while idle -> `GESTURE_DETECT`
2. If removed before `HOLD_TIME_MS` and presence lasted `MIN_DETECTION_MS` -> start pour
3. `startDispensing` sets `sensorIgnoreUntilMs = now + 1000`
4. Stop edges ignored until ignore window ends
5. Volume estimate: `elapsed_s / secondsPerLiter`

### Mode B timing

1. Hold through `HOLD_TIME_MS` -> `SELECT_VOLUME`
2. While `handPresent`: `selectedVolume = mapDistanceToVolume(distance)`
3. On leave: start absence timer; return of hand cancels timer
4. Absence >= 3000 ms -> `VOLUME_LOCKED`
5. First 1000 ms of lock: ignore swipe start
6. After that: hand present edge starts `DISPENSING`
7. Lock overall timeout 10000 ms without swipe -> idle

### Distance to volume

```text
dNear = min(minCalDist, maxCalDist)
dFar  = max(minCalDist, maxCalDist)
t     = clamp( map(dist, dNear, dFar, 0, 1) )
vol   = 0.1 + t * (2.0 - 0.1)
vol   = round_to_0.1(vol)
```

If calibrated span &lt; 1 cm, fallback range is 6 to 30 cm.

---

## Concurrency model

- Single-threaded Arduino `loop()` owns the FSM and GPIO.
- AsyncTCP / ESPAsyncWebServer callbacks run in the async context and mutate shared globals (`currentState`, calibration flags, etc.).
- This is acceptable for a soft real-time appliance but is not hard-synchronized. Avoid long blocking work inside HTTP handlers except the intentional 2 s sampling loops in distance calibration (they block the async task briefly).

---

## LCD EMI strategy

Relay and motor noise can corrupt HD44780 state.

`setPump(bool)`:

1. No-op if already in requested state
2. Set quiet deadline (`LCD_QUIET_MS`)
3. Write relay pin
4. `delay(15)` settle
5. `reinitLCD()` (`begin`, `clear`, `display`)
6. Force next UI refresh

`updateLCD()`:

- Skips while quiet window active
- Writes two fixed 16-char rows (space padded)
- Does not call `clear()` every frame

---

## Persistence

`Preferences` namespace `dispenser` stores calibration and lifetime counters. `saveSettings()` is called after meaningful events (completed pour above threshold, cal, lock, new can).

---

## Web UI

HTML/CSS/JS is embedded as a raw string literal in `handleWebRequests()` for a single-file firmware deploy. The browser polls `/status` and issues GET actions for controls.

Tradeoff: simple OTA-less flashing UX vs large sketch string. For growth, move static files to SPIFFS/LittleFS.

---

## Extension ideas

- Add HTTP basic auth or token
- MQTT / Home Assistant discovery
- Float switch GPIO interlock
- OTA updates (`ArduinoOTA`)
- Multi-language LCD strings
- Non-blocking distance calibration (stateful sampler)
