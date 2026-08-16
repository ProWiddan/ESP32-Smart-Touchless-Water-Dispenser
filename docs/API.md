# HTTP API Reference

The firmware runs an asynchronous HTTP server on **port 80**.

Base URL: `http://<esp32-ip>/`

There is **no authentication**. Use only on a trusted LAN or behind a reverse proxy / VPN with access control.

All endpoints below use **HTTP GET** unless noted.

---

## Pages

### `GET /`

Returns the HTML web portal (status, stats, dispense, calibration, maintenance).

**Response:** `200 text/html`

---

## Status

### `GET /status`

Live JSON snapshot for UI polling (portal refreshes about every 400 ms).

**Response:** `200 application/json`

Example:

```json
{
  "state": "Idle",
  "remain": 18.90,
  "selected": 0.5,
  "dispensed": 0.000,
  "locked": false,
  "secPerL": 12.00,
  "lpm": 5.00,
  "distance": 14.2,
  "totalVol": 3.40,
  "totalCnt": 7,
  "avgVol": 0.49,
  "cansUsed": 1,
  "canPct": 100.0,
  "minDist": 8.0,
  "maxDist": 25.0,
  "flowRunning": false,
  "flowElapsed": 0.0,
  "uptime": 3600000
}
```

| Field | Type | Description |
|-------|------|-------------|
| `state` | string | Human-readable FSM state |
| `remain` | number | Litres left in current can |
| `selected` | number | Target volume (L) |
| `dispensed` | number | Volume poured in current/last cycle (L) |
| `locked` | bool | System lock flag |
| `secPerL` | number | Calibrated seconds per litre |
| `lpm` | number | Litres per minute (`60 / secPerL`) |
| `distance` | number | Latest ultrasonic reading (cm); `999` style far = no echo |
| `totalVol` | number | Lifetime dispensed (L) |
| `totalCnt` | number | Lifetime pour count |
| `avgVol` | number | `totalVol / totalCnt` (0 if none) |
| `cansUsed` | number | Estimated can index from total volume |
| `canPct` | number | Remaining can percent 0 to 100 |
| `minDist` | number | Calibrated nearer distance (cm) |
| `maxDist` | number | Calibrated farther distance (cm) |
| `flowRunning` | bool | Flow calibration pump active |
| `flowElapsed` | number | Seconds since flow test start |
| `uptime` | number | `millis()` uptime (ms) |

### State strings

| `state` value | Meaning |
|---------------|---------|
| `Idle` | Waiting for gesture |
| `Manual Pour` | Mode A running |
| `Selecting Volume` | Mode B adjusting |
| `Locked Swipe` | Volume locked; waiting for swipe |
| `Dispensing` | Mode B pour running |
| `Complete` | Brief done screen |
| `SYSTEM LOCKED` | Dispense disabled |
| `Flow Calibration` | Timed 1 L flow test |
| `Distance Calibration` | Distance cal in progress |
| `Low Water` | Remaining below threshold (idle paths) |

---

## Dispense control

### `GET /setvol?v=<litres>`

Set selected volume without starting a pour.

| Query | Required | Range |
|-------|----------|--------|
| `v` | yes | 0.1 to 2.0 |

**Response:** `200 text/plain` → `OK`

---

### `GET /dispense?v=<litres>`

Lock a volume and enter swipe-to-confirm state (same as Mode B lock path). Waits 1 s, then a hand swipe starts the pour.

| Query | Required | Default |
|-------|----------|---------|
| `v` | no | current `selectedVolume` |

**Responses**

- `200 text/plain` confirmation message
- `403 text/plain` `Locked` if system lock is on

---

### `GET /manual?on=<0|1>`

| Query | Meaning |
|-------|---------|
| `on=1` | Start Mode A style manual pour (up to 2.0 L) |
| `on=0` | Stop pump / complete cycle |

**Responses:** `200 OK` or `403 Locked`

---

## Calibration

### `GET /flowstart`

Starts flow-rate calibration:

- Enters calibration mode (gestures ignored)
- Pump ON
- LCD shows elapsed seconds

**Responses**

- `200` start message
- `200` `Already running` if already active
- `403` if system locked

---

### `GET /flowstop`

Stops flow test and saves calibration if duration is valid.

**Logic**

- Elapsed = seconds to fill **1.0 L**
- Valid window: **3 s to 300 s**
- On success: `secondsPerLiter = elapsed`, saved to NVS

**Responses**

- `200` e.g. `OK 12.40 s per L (4.84 L/min)`
- `400` if not running / too short / too long

---

### `GET /calmin`

Samples distance for ~2 s. Saves average as **min volume position** (0.1 L / closer).

**Responses**

- `200` with calibrated cm
- `400` if unstable (too few valid samples)

---

### `GET /calmax`

Same as calmin for **max volume position** (2.0 L / farther).

---

## Maintenance

### `GET /newcan`

Sets `remainingCan = 18.9` (or `CAN_CAPACITY_L`) and saves.

**Response:** `200` confirmation

---

### `GET /lock`

Toggles system lock.

- Locked: pump forced off, state `STATE_SYSTEM_LOCKED`
- Unlocked: returns to idle

**Response:** `200` `SYSTEM LOCKED` or `UNLOCKED`

---

### `GET /reset`

Clears Preferences namespace and restores defaults:

| Setting | Default |
|---------|---------|
| secondsPerLiter | 12.0 |
| minCalDist | 8.0 cm |
| maxCalDist | 25.0 cm |
| remainingCan | 18.9 L |
| systemLocked | false |
| totalDispensedAllTime | 0 |
| totalDispenseCount | 0 |

**Response:** `200` confirmation

---

## curl examples

```bash
# status
curl http://192.168.1.42/status

# set 1.0 L and arm swipe dispense
curl "http://192.168.1.42/setvol?v=1.0"
curl "http://192.168.1.42/dispense?v=1.0"

# manual
curl "http://192.168.1.42/manual?on=1"
curl "http://192.168.1.42/manual?on=0"

# flow cal
curl http://192.168.1.42/flowstart
# ... wait until 1 L filled ...
curl http://192.168.1.42/flowstop

# distance cal
curl http://192.168.1.42/calmin
curl http://192.168.1.42/calmax

# maintenance
curl http://192.168.1.42/newcan
curl http://192.168.1.42/lock
curl http://192.168.1.42/reset
```

---

## Persistence (NVS)

Namespace: `dispenser`

| Key | Type | Content |
|-----|------|---------|
| `secPerL` | float | Flow calibration |
| `minDist` | float | Mode B near distance |
| `maxDist` | float | Mode B far distance |
| `remain` | float | Can remaining litres |
| `locked` | bool | System lock |
| `totalVol` | float | Lifetime volume |
| `totalCnt` | int | Lifetime count |

Saved after successful pours (above 50 ml), calibrations, lock toggle, new can, and related events.
