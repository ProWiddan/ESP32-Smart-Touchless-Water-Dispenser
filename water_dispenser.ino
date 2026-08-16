/*
 * ESP32 Smart Touchless Water Dispenser
 *
 * Touchless ultrasonic gestures, LCD status, Wi-Fi web portal,
 * flow + distance calibration, can tracking, and NVS settings.
 *
 * Mode A: quick wave -> manual pour (stop after 1 s lockout with second wave)
 * Mode B: hold 1 s -> distance volume (closer=less, farther=more)
 *         hand present = adjust only; hand gone 3 s = lock;
 *         wait 1 s; swipe to pour
 *
 * Before flash: set WIFI_SSID / WIFI_PASSWORD below.
 * Docs: README.md and docs/
 *
 * License: MIT
 */

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <LiquidCrystal.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

// ====================== PIN DEFINITIONS (EXACT) ======================
const int TRIG_PIN      = 23;
const int ECHO_PIN      = 22;
const int LCD_RS        = 19;
const int LCD_EN        = 21;
const int LCD_D4        = 18;
const int LCD_D5        = 17;
const int LCD_D6        = 16;
const int LCD_D7        = 15;
const int LCD_BACKLIGHT = 25;   // PWM
const int RELAY_PIN     = 26;   // ACTIVE LOW

// ====================== CONSTANTS ======================
const float MIN_DIST_CM        = 6.0;
const float MAX_DIST_CM        = 30.0;
const float MAX_VOLUME_L       = 2.0;
const float MIN_VOLUME_L       = 0.1f;
const float CAN_CAPACITY_L     = 18.9;
const float LOW_WATER_THRESH   = 1.0;

const unsigned long HOLD_TIME_MS           = 1000;   // hold to enter Mode B
const unsigned long VOLUME_ABSENT_LOCK_MS  = 3000;   // hand gone this long -> lock volume
const unsigned long VOLUME_SWIPE_DELAY_MS  = 1000;   // after lock, wait before accepting swipe
const unsigned long LOCK_TIMEOUT_MS        = 10000;  // cancel locked volume if no swipe
const unsigned long LCD_UPDATE_MS         = 300;
const unsigned long PUMP_TIMEOUT_MS        = 180000UL; // 3 min
const unsigned long SENSOR_IGNORE_MS       = 1000;   // after dispense start: no hand-stop for 1 s

// -- Anti-false-trigger settings --
const unsigned long MIN_DETECTION_MS    = 150;  // hand must be stable this long before Mode A
const float         MIN_DISPENSED_COUNT = 0.05; // ignore dispenses < 50ml

const int   AVG_SAMPLES        = 4;
const int   BACKLIGHT_IDLE     = 40;
const int   BACKLIGHT_ACTIVE   = 255;

// ====================== WIFI (CHANGE THESE) ======================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ====================== OBJECTS ======================
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
Preferences prefs;
AsyncWebServer server(80);

// ====================== STATE MACHINE ======================
enum SystemState {
  STATE_IDLE,
  STATE_GESTURE_DETECT,
  STATE_MANUAL_POUR,
  STATE_SELECT_VOLUME,
  STATE_VOLUME_LOCKED,
  STATE_DISPENSING,
  STATE_COMPLETE,
  STATE_SYSTEM_LOCKED,
  STATE_CALIBRATING
};

SystemState currentState = STATE_IDLE;

// ====================== RUNTIME ======================
float currentDistance = 0.0;
bool  handPresent     = false;
bool  handDetected    = false;   // edge-detect (toggle style)

// Detection stability (anti-wall / brief reflection)
unsigned long handFirstSeenMs = 0;
bool  handFirstSeenValid = false;

// Timing
unsigned long gestureStartMs       = 0;
unsigned long stateEnterMs         = 0;
unsigned long lastLcdUpdateMs      = 0;
unsigned long dispenseStartMs      = 0;
unsigned long lastSensorMs         = 0;
unsigned long sensorIgnoreUntilMs  = 0;  // ignore hand stop until this time
unsigned long handAbsentStartMs    = 0;  // when hand left during Mode B select
bool          handAbsentValid      = false;

// Volume & flow
float secondsPerLiter = 12.0;
float selectedVolume  = 0.5;
float dispensedVolume = 0.0;
float remainingCan    = CAN_CAPACITY_L;

// Distance calibration:
//   minCalDist = hand distance for MIN volume (0.1 L)  - typically closer
//   maxCalDist = hand distance for MAX volume (2.0 L)  - typically farther
// User rule: closer -> less volume, farther -> more volume
float minCalDist = 8.0;    // default closer  -> 0.1 L
float maxCalDist = 25.0;   // default farther -> 2.0 L

bool  systemLocked    = false;

// Statistics
float totalDispensedAllTime = 0.0;
int   totalDispenseCount   = 0;
unsigned long systemUptimeMs = 0;

// Calibration mode (ignores gestures)
bool calibrationMode = false;

// Flow test
bool  flowTestRunning = false;
unsigned long flowTestStartMs = 0;

// Pump / LCD EMI protection
bool  pumpIsOn = false;
unsigned long pumpChangedMs = 0;
unsigned long lcdQuietUntilMs = 0;   // skip LCD writes after relay switch
const unsigned long LCD_QUIET_MS = 80;

// ====================== FORWARD DECL ======================
float mapFloat(float x, float inMin, float inMax, float outMin, float outMax);
float readDistanceAveraged();
void  updateHandPresence();
void  setPump(bool on);
void  setBacklight(int brightness);
void  reinitLCD();
void  lcdWriteLine(int row, const char* text);
void  lcdWriteLineF(int row, const char* fmt, ...);
void  updateLCD();
void  saveSettings();
void  loadSettings();
void  startDispensing(float targetVol);
void  stopDispensing(bool completed);
void  handleWebRequests();
String getStatusJSON();
float mapDistanceToVolume(float dist);
bool  sensorStopAllowed();

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== ESP32 Touchless Water Dispenser ===");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // pump OFF (active low)
  pumpIsOn = false;

  // New ESP32 core 3.x LEDC API
  ledcAttach(LCD_BACKLIGHT, 5000, 8);
  setBacklight(BACKLIGHT_IDLE);

  reinitLCD();
  lcdWriteLine(0, "Booting...");
  lcdWriteLine(1, "");

  loadSettings();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK -> http://" + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi failed - web portal unavailable");
  }

  handleWebRequests();
  server.begin();
  Serial.println("HTTP server started");

  systemUptimeMs = millis();
  currentState = systemLocked ? STATE_SYSTEM_LOCKED : STATE_IDLE;
  stateEnterMs = millis();
  updateLCD();
}

// ====================== MAIN LOOP ======================
void loop() {
  systemUptimeMs = millis();

  // Always read sensor (LCD / web / Mode B mapping).
  // Hand-stop during pour is gated separately via sensorStopAllowed().
  if (millis() - lastSensorMs >= 60) {
    lastSensorMs = millis();
    currentDistance = readDistanceAveraged();
    updateHandPresence();
  }

  // Safety timeout on pump
  if ((currentState == STATE_MANUAL_POUR || currentState == STATE_DISPENSING) &&
      (millis() - dispenseStartMs > PUMP_TIMEOUT_MS)) {
    stopDispensing(false);
    currentState = STATE_IDLE;
  }

  // Skip gesture-based states while calibrating (do not kill pump during flow test)
  if (calibrationMode && currentState != STATE_CALIBRATING) {
    currentState = STATE_CALIBRATING;
    if (!flowTestRunning) setPump(false);
  }

  // Finite State Machine
  switch (currentState) {

    // ---------- IDLE ----------
    case STATE_IDLE:
      setPump(false);
      setBacklight(BACKLIGHT_IDLE);

      if (systemLocked) {
        currentState = STATE_SYSTEM_LOCKED;
        break;
      }
      if (calibrationMode) {
        break;
      }

      // Edge detect - hand just appeared
      if (handPresent && !handDetected) {
        handDetected       = true;
        handFirstSeenMs    = millis();
        handFirstSeenValid = true;
        gestureStartMs     = millis();
        currentState       = STATE_GESTURE_DETECT;
        stateEnterMs       = millis();
      }
      if (!handPresent) {
        handDetected = false;
        handFirstSeenValid = false;
      }
      break;

    // ---------- GESTURE TIMING ----------
    case STATE_GESTURE_DETECT:
      setBacklight(BACKLIGHT_ACTIVE);

      if (!handPresent) {
        unsigned long held = millis() - gestureStartMs;
        if (held < HOLD_TIME_MS) {
          // Mode A only if hand was stable for MIN_DETECTION_MS
          unsigned long presentMs = millis() - handFirstSeenMs;
          if (handFirstSeenValid && presentMs >= MIN_DETECTION_MS) {
            startDispensing(MAX_VOLUME_L);
            currentState = STATE_MANUAL_POUR;
            handDetected = true;   // wait for next edge to stop
          } else {
            // Too brief (wall / reflection) -> idle silently
            currentState = STATE_IDLE;
            handDetected = false;
            handFirstSeenValid = false;
          }
        } else {
          currentState = STATE_IDLE;
        }
        break;
      }

      if (millis() - gestureStartMs >= HOLD_TIME_MS) {
        // Mode B - hold -> Select Volume (distance-based)
        selectedVolume    = mapDistanceToVolume(currentDistance);
        handAbsentValid   = false;
        currentState      = STATE_SELECT_VOLUME;
        stateEnterMs      = millis();
        handDetected      = true;
      }
      break;

    // ---------- MODE A: MANUAL POUR ----------
    case STATE_MANUAL_POUR:
      setBacklight(BACKLIGHT_ACTIVE);
      setPump(true);

      dispensedVolume = (millis() - dispenseStartMs) / 1000.0f / secondsPerLiter;
      if (dispensedVolume > MAX_VOLUME_L) dispensedVolume = MAX_VOLUME_L;

      // Only allow hand-stop after 1 s post-start ignore window
      if (sensorStopAllowed()) {
        // Second wave = stop (edge)
        if (handPresent && !handDetected) {
          handDetected = true;
          stopDispensing(true);
          currentState = STATE_COMPLETE;
          stateEnterMs = millis();
        }
        if (!handPresent) {
          handDetected = false;
        }
      } else {
        // During ignore: track hand leaving so a wave after 1s is a clean edge
        if (!handPresent) {
          handDetected = false;
        }
      }

      if (dispensedVolume >= MAX_VOLUME_L) {
        stopDispensing(true);
        currentState = STATE_COMPLETE;
        stateEnterMs = millis();
      }
      break;

    // ---------- MODE B: SELECT VOLUME (distance-based) ----------
    // While hand is present: live map distance -> volume, NEVER lock.
    // Hand gone continuously for 3 s -> lock last volume -> VOLUME_LOCKED.
    case STATE_SELECT_VOLUME: {
      setBacklight(BACKLIGHT_ACTIVE);
      setPump(false);

      if (handPresent) {
        // Still adjusting - keep mapping, reset absence timer
        selectedVolume  = mapDistanceToVolume(currentDistance);
        handAbsentValid = false;
        handDetected    = true;
      } else {
        // Hand left - start / continue 3 s absence countdown
        if (!handAbsentValid) {
          handAbsentStartMs = millis();
          handAbsentValid   = true;
          handDetected      = false;
          Serial.printf("Mode B: hand left, locking in 3s if still gone (vol=%.1f L)\n",
                        selectedVolume);
        }
        if (millis() - handAbsentStartMs >= VOLUME_ABSENT_LOCK_MS) {
          selectedVolume = constrain(selectedVolume, MIN_VOLUME_L, MAX_VOLUME_L);
          currentState   = STATE_VOLUME_LOCKED;
          stateEnterMs   = millis();
          handDetected   = false;
          handAbsentValid = false;
          Serial.printf("Volume LOCKED: %.1f L (hand absent 3s)\n", selectedVolume);
        }
      }
      break;
    }

    // ---------- VOLUME LOCKED ----------
    // First 1 s: wait (no swipe accepted) so user can prepare cup.
    // After 1 s: accept hand swipe to start dispense.
    // Overall timeout cancels if no swipe.
    case STATE_VOLUME_LOCKED:
      setBacklight(BACKLIGHT_ACTIVE);
      setPump(false);

      // Wait 1 s after lock before any swipe can start pour
      if (millis() - stateEnterMs < VOLUME_SWIPE_DELAY_MS) {
        // Track hand leaving during wait so swipe after delay is a clean edge
        if (!handPresent) handDetected = false;
        break;
      }

      if (handPresent && !handDetected) {
        handDetected = true;
        startDispensing(selectedVolume);
        currentState = STATE_DISPENSING;
      }
      if (!handPresent) handDetected = false;

      if (millis() - stateEnterMs > LOCK_TIMEOUT_MS) {
        currentState = STATE_IDLE;
        handDetected = false;
      }
      break;

    // ---------- DISPENSING ----------
    case STATE_DISPENSING: {
      setBacklight(BACKLIGHT_ACTIVE);
      setPump(true);

      dispensedVolume = (millis() - dispenseStartMs) / 1000.0f / secondsPerLiter;
      float target = min(selectedVolume, MAX_VOLUME_L);

      if (dispensedVolume >= target) {
        stopDispensing(true);
        currentState = STATE_COMPLETE;
        stateEnterMs = millis();
        break;
      }

      // Early stop with another wave - only after 1 s ignore window
      if (sensorStopAllowed()) {
        if (handPresent && !handDetected) {
          handDetected = true;
          stopDispensing(true);
          currentState = STATE_COMPLETE;
          stateEnterMs = millis();
        }
        if (!handPresent) handDetected = false;
      } else {
        // During ignore: track hand leaving so a wave after 1s is a clean edge
        if (!handPresent) handDetected = false;
      }
      break;
    }

    // ---------- COMPLETE ----------
    case STATE_COMPLETE:
      setPump(false);
      setBacklight(BACKLIGHT_ACTIVE);
      if (millis() - stateEnterMs > 2500) {
        currentState = STATE_IDLE;
        handDetected = false;
      }
      break;

    // ---------- SYSTEM LOCKED ----------
    case STATE_SYSTEM_LOCKED:
      setPump(false);
      setBacklight(BACKLIGHT_IDLE);
      break;

    // ---------- CALIBRATING (web-only) ----------
    case STATE_CALIBRATING:
      // Flow test keeps pump ON; distance cal keeps pump OFF
      if (!flowTestRunning) {
        setPump(false);
      }
      setBacklight(BACKLIGHT_ACTIVE);
      // Gestures ignored; wait for calibrationMode=false via web
      break;
  }

  // LCD throttle (skip while relay EMI settles)
  if ((long)(millis() - lcdQuietUntilMs) >= 0 &&
      millis() - lastLcdUpdateMs >= LCD_UPDATE_MS) {
    lastLcdUpdateMs = millis();
    updateLCD();
  }

  delay(5);
}

// ====================== HELPERS ======================
float mapFloat(float x, float inMin, float inMax, float outMin, float outMax) {
  if (inMax == inMin) return outMin;
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// Map hand distance -> volume.
// Rule: closer = less volume, farther = more volume.
// Uses calibrated minCalDist (0.1 L) and maxCalDist (2.0 L).
// Snaps to equal 0.1 L divisions across the range.
float mapDistanceToVolume(float dist) {
  float dNear = min(minCalDist, maxCalDist);
  float dFar  = max(minCalDist, maxCalDist);

  // Degenerate calibration -> fall back to full sensor range
  if ((dFar - dNear) < 1.0f) {
    dNear = MIN_DIST_CM;
    dFar  = MAX_DIST_CM;
  }

  // User rule always: nearer distance -> MIN_VOLUME, farther -> MAX_VOLUME
  // Regardless of which cal button was pressed at which height,
  // we orient by actual distance (closer/farther).
  // If the user calibrated min at far and max at near, still enforce closer=less.
  float t = mapFloat(dist, dNear, dFar, 0.0f, 1.0f);
  t = constrain(t, 0.0f, 1.0f);

  float vol = MIN_VOLUME_L + t * (MAX_VOLUME_L - MIN_VOLUME_L);

  // Equal 0.1 L steps
  vol = roundf(vol * 10.0f) / 10.0f;
  return constrain(vol, MIN_VOLUME_L, MAX_VOLUME_L);
}

// True when hand-stop gestures are allowed (outside 1 s ignore window)
bool sensorStopAllowed() {
  return (long)(millis() - sensorIgnoreUntilMs) >= 0;
}

// ====================== SENSOR ======================
float readDistanceAveraged() {
  float sum = 0;
  int valid = 0;
  for (int i = 0; i < AVG_SAMPLES; i++) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    if (duration > 0) {
      float d = duration * 0.0343f / 2.0f;
      if (d > 1.0f && d < 100.0f) {
        sum += d;
        valid++;
      }
    }
    delayMicroseconds(200);
  }
  if (valid == 0) return 999.0f;
  return sum / valid;
}

void updateHandPresence() {
  handPresent = (currentDistance >= MIN_DIST_CM && currentDistance <= MAX_DIST_CM);
}

// ====================== PUMP & BACKLIGHT ======================
// Relay coil + pump motor inject noise into shared 5V / GND and
// can corrupt HD44780 LCD data. Only toggle when state changes,
// pause LCD writes briefly, then re-init the display.
void setPump(bool on) {
  if (on == pumpIsOn) return;

  // Pause LCD traffic while the relay switches
  lcdQuietUntilMs = millis() + LCD_QUIET_MS;

  digitalWrite(RELAY_PIN, on ? LOW : HIGH);   // ACTIVE LOW
  pumpIsOn = on;
  pumpChangedMs = millis();

  // Let coil / motor spike settle before touching LCD bus
  delay(15);
  reinitLCD();
  lastLcdUpdateMs = 0;  // force refresh after quiet window
}

void setBacklight(int brightness) {
  ledcWrite(LCD_BACKLIGHT, constrain(brightness, 0, 255));
}

// Full HD44780 re-init after EMI event
void reinitLCD() {
  lcd.begin(16, 2);
  lcd.clear();
  lcd.display();
}

// Write a full 16-char row (pad with spaces). Avoids lcd.clear() garbage.
void lcdWriteLine(int row, const char* text) {
  char buf[17];
  int i = 0;
  if (text) {
    while (text[i] && i < 16) {
      buf[i] = text[i];
      i++;
    }
  }
  while (i < 16) buf[i++] = ' ';
  buf[16] = '\0';
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void lcdWriteLineF(int row, const char* fmt, ...) {
  char msg[40];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  lcdWriteLine(row, msg);
}

// ====================== DISPENSE HELPERS ======================
void startDispensing(float targetVol) {
  dispensedVolume = 0.0f;
  selectedVolume  = constrain(targetVol, MIN_VOLUME_L, MAX_VOLUME_L);
  dispenseStartMs = millis();
  // Block hand-stop for 1 s so residual hand / bounce cannot stop pour
  sensorIgnoreUntilMs = millis() + SENSOR_IGNORE_MS;
  setPump(true);
  Serial.printf("Start dispense target=%.2f L (hand-stop locked %lu ms)\n",
                selectedVolume, SENSOR_IGNORE_MS);
}

void stopDispensing(bool completed) {
  setPump(false);

  // Only count as real dispense if above minimum threshold
  if (dispensedVolume >= MIN_DISPENSED_COUNT) {
    remainingCan -= dispensedVolume;
    if (remainingCan < 0) remainingCan = 0;
    totalDispensedAllTime += dispensedVolume;
    totalDispenseCount++;
    saveSettings();
    Serial.printf("Stopped. Dispensed %.3f L | Can left: %.2f L\n",
                  dispensedVolume, remainingCan);
  } else {
    Serial.printf("Ignored micro-dispense: %.3f L\n", dispensedVolume);
  }
}

// ====================== LCD ======================
// Never use lcd.clear() in the refresh path: under motor EMI it often
// leaves random characters. Overwrite both rows with padded 16-char lines.
void updateLCD() {
  if ((long)(millis() - lcdQuietUntilMs) < 0) return;

  if (calibrationMode || currentState == STATE_CALIBRATING) {
    if (flowTestRunning) {
      float elapsed = (millis() - flowTestStartMs) / 1000.0f;
      lcdWriteLine(0, "FLOW CAL 1L");
      lcdWriteLineF(1, "Time: %.1fs", elapsed);
    } else {
      lcdWriteLine(0, "DIST CAL");
      lcdWriteLine(1, "Hold steady");
    }
    return;
  }

  if (systemLocked || currentState == STATE_SYSTEM_LOCKED) {
    lcdWriteLine(0, "SYSTEM LOCKED");
    lcdWriteLine(1, "Admin only");
    return;
  }

  if (remainingCan < LOW_WATER_THRESH &&
      (currentState == STATE_IDLE || currentState == STATE_GESTURE_DETECT)) {
    lcdWriteLine(0, "LOW WATER");
    lcdWriteLine(1, "Replace can soon");
    return;
  }

  switch (currentState) {
    case STATE_IDLE:
    case STATE_GESTURE_DETECT:
      lcdWriteLine(0, "Wave Hand Over");
      lcdWriteLine(1, "Sensor to Start");
      break;

    case STATE_MANUAL_POUR:
      lcdWriteLine(0, "MANUAL POUR");
      lcdWriteLineF(1, "Out: %.1fL", dispensedVolume);
      break;

    case STATE_SELECT_VOLUME: {
      if (handPresent) {
        lcdWriteLineF(0, "SELECT %.1fL", selectedVolume);
        lcdWriteLineF(1, "Dist %.1fcm", currentDistance);
      } else if (handAbsentValid) {
        unsigned long gone = millis() - handAbsentStartMs;
        int left = (int)((VOLUME_ABSENT_LOCK_MS > gone)
                         ? ((VOLUME_ABSENT_LOCK_MS - gone + 999) / 1000)
                         : 0);
        lcdWriteLineF(0, "Lock in %ds", left);
        lcdWriteLineF(1, "Vol: %.1fL", selectedVolume);
      } else {
        lcdWriteLineF(0, "SELECT %.1fL", selectedVolume);
        lcdWriteLine(1, "Adjust / leave");
      }
      break;
    }

    case STATE_VOLUME_LOCKED: {
      lcdWriteLineF(0, "LOCKED %.1fL", selectedVolume);
      if (millis() - stateEnterMs < VOLUME_SWIPE_DELAY_MS) {
        unsigned long waitLeft = VOLUME_SWIPE_DELAY_MS - (millis() - stateEnterMs);
        int sec = (int)((waitLeft + 999) / 1000);
        lcdWriteLineF(1, "Wait %ds", sec > 0 ? sec : 1);
      } else {
        lcdWriteLine(1, "Swipe to pour");
      }
      break;
    }

    case STATE_DISPENSING:
      lcdWriteLine(0, "DISPENSING...");
      lcdWriteLineF(1, "Out: %.1fL", dispensedVolume);
      break;

    case STATE_COMPLETE:
      lcdWriteLine(0, "COMPLETE");
      lcdWriteLineF(1, "Done: %.1fL", dispensedVolume);
      break;

    default:
      lcdWriteLine(0, "Wave Hand Over");
      lcdWriteLine(1, "Sensor to Start");
      break;
  }
}

// ====================== PREFERENCES ======================
void loadSettings() {
  prefs.begin("dispenser", true);
  secondsPerLiter       = prefs.getFloat("secPerL", 12.0f);
  minCalDist            = prefs.getFloat("minDist", 8.0f);   // closer default
  maxCalDist            = prefs.getFloat("maxDist", 25.0f);  // farther default
  remainingCan          = prefs.getFloat("remain", CAN_CAPACITY_L);
  systemLocked          = prefs.getBool("locked", false);
  totalDispensedAllTime = prefs.getFloat("totalVol", 0.0f);
  totalDispenseCount    = prefs.getInt("totalCnt", 0);
  prefs.end();

  Serial.printf("Loaded sec/L=%.2f minD=%.1f maxD=%.1f remain=%.2f locked=%d totalVol=%.2f cnt=%d\n",
                secondsPerLiter, minCalDist, maxCalDist, remainingCan, systemLocked,
                totalDispensedAllTime, totalDispenseCount);
}

void saveSettings() {
  prefs.begin("dispenser", false);
  prefs.putFloat("secPerL", secondsPerLiter);
  prefs.putFloat("minDist", minCalDist);
  prefs.putFloat("maxDist", maxCalDist);
  prefs.putFloat("remain", remainingCan);
  prefs.putBool("locked", systemLocked);
  prefs.putFloat("totalVol", totalDispensedAllTime);
  prefs.putInt("totalCnt", totalDispenseCount);
  prefs.end();
}

// ====================== WEB PORTAL ======================
void handleWebRequests() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Water Dispenser</title>
  <style>
    body{font-family:Arial,sans-serif;margin:16px;background:#f0f4f8;color:#222}
    h1{color:#0066cc;margin:0 0 12px 0;font-size:22px}
    h3{margin:0 0 10px 0;color:#333;font-size:16px}
    .card{background:#fff;padding:14px;border-radius:10px;margin-bottom:14px;box-shadow:0 2px 6px rgba(0,0,0,.1)}
    button{padding:10px 14px;margin:4px 4px 4px 0;border:none;border-radius:6px;background:#0066cc;color:#fff;font-size:14px;cursor:pointer}
    button:active{background:#004999}
    button.danger{background:#cc3300}
    button.ok{background:#228833}
    button.cal{background:#886600}
    button:disabled{opacity:.5}
    input[type=range]{width:95%}
    .status{font-size:18px;font-weight:bold;padding:10px;border-radius:6px;text-align:center}
    .status.idle{background:#e8f5e9;color:#2e7d32}
    .status.disp{background:#fff3e0;color:#e65100}
    .status.lock{background:#ffebee;color:#c62828}
    .status.cal{background:#fff8e1;color:#886600}
    .dist{font-size:32px;font-weight:bold;color:#0066cc}
    .uptime{color:#888;font-size:12px}
    .grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
    .stat{background:#f5f7fa;padding:10px;border-radius:8px}
    .stat .lbl{font-size:11px;color:#666;text-transform:uppercase}
    .stat .val{font-size:18px;font-weight:bold;color:#111;margin-top:2px}
    .bar{height:10px;background:#e0e0e0;border-radius:5px;overflow:hidden;margin-top:8px}
    .bar > i{display:block;height:100%;background:#0066cc;width:0%}
    .bar.low > i{background:#cc3300}
    .hint{font-size:13px;color:#555;margin:6px 0}
    .flowbox{background:#fffde7;padding:10px;border-radius:8px;margin:8px 0;text-align:center}
    .flowtime{font-size:28px;font-weight:bold;color:#886600}
    #flowResult,#calResult{font-size:13px;color:#333;min-height:18px}
  </style>
</head>
<body>
  <h1>Smart Water Dispenser</h1>

  <div class="card" style="text-align:center">
    <div>Hand Distance</div>
    <div class="dist" id="dist">--</div>
    <div class="hint">cm | Valid range 6 to 30 cm</div>
    <div class="hint">Mode B: closer = less volume, farther = more. Leave hand 3s to lock, wait 1s, then swipe.</div>
  </div>

  <div class="card">
    <div class="status idle" id="statusBox"><span id="status">Loading...</span></div>
    <p>Remaining in can: <b id="remain">--</b> L</p>
    <p>Selected volume: <b id="selvol">0.5</b> L</p>
    <p class="uptime">Uptime: <span id="uptime">--</span></p>
  </div>

  <div class="card">
    <h3>Statistics</h3>
    <div class="grid">
      <div class="stat"><div class="lbl">Total dispensed</div><div class="val" id="statVol">-- L</div></div>
      <div class="stat"><div class="lbl">Dispense count</div><div class="val" id="statCnt">--</div></div>
      <div class="stat"><div class="lbl">Average pour</div><div class="val" id="statAvg">-- L</div></div>
      <div class="stat"><div class="lbl">Cans used</div><div class="val" id="statCan">--</div></div>
      <div class="stat"><div class="lbl">Can remaining</div><div class="val" id="statRemain">-- L</div></div>
      <div class="stat"><div class="lbl">Flow rate</div><div class="val" id="statFlow">--</div></div>
    </div>
    <div class="hint" style="margin-top:10px">Can level</div>
    <div class="bar" id="canBar"><i id="canBarFill"></i></div>
    <div class="hint"><span id="canPct">--</span>% full | Cal dist min <span id="calMinD">--</span> cm, max <span id="calMaxD">--</span> cm</div>
  </div>

  <div class="card">
    <h3>Dispense</h3>
    <label>Volume: <span id="volLabel">0.5</span> L</label>
    <input type="range" id="volSlider" min="0.1" max="2.0" step="0.1" value="0.5"
           oninput="document.getElementById('volLabel').innerText=this.value">
    <br>
    <button onclick="setVol(0.5)">0.5L</button>
    <button onclick="setVol(1.0)">1.0L</button>
    <button onclick="setVol(1.5)">1.5L</button>
    <button onclick="setVol(2.0)">2.0L</button>
    <br><br>
    <button class="ok" onclick="doDispense()">Dispense (wait 1s then swipe)</button>
    <button onclick="manualOn()">Manual ON</button>
    <button class="danger" onclick="manualOff()">Manual OFF / Stop</button>
  </div>

  <div class="card" style="background:#fff8e1">
    <h3>Calibration</h3>
    <p class="hint"><b>Flow rate:</b> Place empty 1 L container under spout. Press Start. When full to exactly 1 L, press Stop. LCD shows live elapsed time.</p>
    <div class="flowbox">
      <div class="hint">Flow test timer</div>
      <div class="flowtime" id="flowTimer">0.0 s</div>
      <div class="hint" id="flowStatus">Idle</div>
    </div>
    <button class="cal" id="btnFlowStart" onclick="flowStart()">Start Flow Test</button>
    <button class="cal" id="btnFlowStop" onclick="flowStop()">Stop (1L Done)</button>
    <p id="flowResult"></p>
    <hr style="border:none;border-top:1px solid #e0d0a0;margin:12px 0">
    <p class="hint"><b>Distance (Mode B):</b> Hold hand steady 2s at each mark. Closer = 0.1 L. Farther = 2.0 L. Do not wave.</p>
    <button class="cal" onclick="calMin()">Cal Min (0.1L closer)</button>
    <button class="cal" onclick="calMax()">Cal Max (2.0L farther)</button>
    <p id="calResult"></p>
  </div>

  <div class="card">
    <h3>Maintenance</h3>
    <button class="ok" onclick="newCan()">New Can Installed (18.9L)</button>
    <button onclick="toggleLock()">Lock / Unlock System</button>
    <button class="danger" onclick="resetAll()">Reset All Settings</button>
  </div>

<script>
function formatUptime(ms){
  var s=Math.floor(ms/1000), m=Math.floor(s/60), h=Math.floor(m/60), d=Math.floor(h/24);
  if(d>0) return d+'d '+(h%24)+'h '+(m%60)+'m';
  if(h>0) return h+'h '+(m%60)+'m '+(s%60)+'s';
  return (m%60)+'m '+(s%60)+'s';
}
function refresh(){
  fetch('/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('status').innerText = d.state;
    document.getElementById('remain').innerText = d.remain.toFixed(1);
    document.getElementById('selvol').innerText = d.selected.toFixed(1);
    document.getElementById('dist').innerText = d.distance.toFixed(1);
    document.getElementById('statVol').innerText = d.totalVol.toFixed(2)+' L';
    document.getElementById('statCnt').innerText = d.totalCnt;
    document.getElementById('statAvg').innerText = d.avgVol.toFixed(2)+' L';
    document.getElementById('statCan').innerText = d.cansUsed;
    document.getElementById('statRemain').innerText = d.remain.toFixed(1)+' L';
    document.getElementById('statFlow').innerText = d.secPerL.toFixed(1)+' s/L';
    document.getElementById('canPct').innerText = d.canPct.toFixed(0);
    document.getElementById('calMinD').innerText = d.minDist.toFixed(1);
    document.getElementById('calMaxD').innerText = d.maxDist.toFixed(1);
    document.getElementById('uptime').innerText = formatUptime(d.uptime);
    var fill=document.getElementById('canBarFill');
    var bar=document.getElementById('canBar');
    fill.style.width = d.canPct.toFixed(0)+'%';
    if(d.canPct < 10) bar.className='bar low'; else bar.className='bar';
    var box=document.getElementById('statusBox');
    var st=d.state || '';
    if(st.indexOf('LOCK')>=0) box.className='status lock';
    else if(st.indexOf('Calibrat')>=0 || st.indexOf('Flow')>=0) box.className='status cal';
    else if(st.indexOf('Dispens')>=0 || st.indexOf('Manual')>=0) box.className='status disp';
    else box.className='status idle';
    if(d.flowRunning){
      document.getElementById('flowTimer').innerText = d.flowElapsed.toFixed(1)+' s';
      document.getElementById('flowStatus').innerText = 'Pump ON. Stop at exactly 1 L';
      document.getElementById('btnFlowStart').disabled = true;
      document.getElementById('btnFlowStop').disabled = false;
    } else {
      if(document.getElementById('flowStatus').innerText.indexOf('Pump ON')===0){
        document.getElementById('flowStatus').innerText = 'Idle';
      }
      document.getElementById('btnFlowStart').disabled = false;
      document.getElementById('btnFlowStop').disabled = false;
    }
  }).catch(function(){});
}
setInterval(refresh,400);
refresh();
function setVol(v){
  document.getElementById('volSlider').value=v;
  document.getElementById('volLabel').innerText=v;
  fetch('/setvol?v='+v);
}
function doDispense(){
  var v=document.getElementById('volSlider').value;
  fetch('/dispense?v='+v).then(function(r){return r.text();}).then(function(t){alert(t);});
}
function manualOn(){ fetch('/manual?on=1'); }
function manualOff(){ fetch('/manual?on=0'); }
function flowStart(){
  document.getElementById('flowResult').innerText='';
  document.getElementById('flowStatus').innerText='Starting...';
  document.getElementById('flowTimer').innerText='0.0 s';
  fetch('/flowstart').then(function(r){return r.text();}).then(function(t){
    document.getElementById('flowResult').innerText=t;
    refresh();
  });
}
function flowStop(){
  fetch('/flowstop').then(function(r){return r.text();}).then(function(t){
    document.getElementById('flowResult').innerText=t;
    document.getElementById('flowStatus').innerText='Done';
    refresh();
  });
}
function calMax(){
  document.getElementById('calResult').innerText='Hold steady for 2s...';
  fetch('/calmax').then(function(r){return r.text();}).then(function(t){
    document.getElementById('calResult').innerText=t; refresh();
  });
}
function calMin(){
  document.getElementById('calResult').innerText='Hold steady for 2s...';
  fetch('/calmin').then(function(r){return r.text();}).then(function(t){
    document.getElementById('calResult').innerText=t; refresh();
  });
}
function newCan(){ fetch('/newcan').then(function(r){return r.text();}).then(function(t){alert(t);}); }
function toggleLock(){ fetch('/lock').then(function(r){return r.text();}).then(function(t){alert(t);}); }
function resetAll(){
  if(confirm('Reset EVERYTHING?')) fetch('/reset').then(function(r){return r.text();}).then(function(t){alert(t);});
}
</script>
</body>
</html>
)rawliteral";
    request->send(200, "text/html", html);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", getStatusJSON());
  });

  server.on("/setvol", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("v")) {
      selectedVolume = constrain(request->getParam("v")->value().toFloat(), MIN_VOLUME_L, MAX_VOLUME_L);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/dispense", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (systemLocked) { request->send(403, "text/plain", "Locked"); return; }
    float v = selectedVolume;
    if (request->hasParam("v")) v = request->getParam("v")->value().toFloat();
    selectedVolume = constrain(v, MIN_VOLUME_L, MAX_VOLUME_L);
    currentState   = STATE_VOLUME_LOCKED;
    stateEnterMs   = millis();
    handDetected   = false;
    handAbsentValid = false;
    request->send(200, "text/plain",
                  "Volume locked. Wait 1s, then swipe to dispense " +
                  String(selectedVolume, 1) + "L");
  });

  server.on("/manual", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (systemLocked) { request->send(403, "text/plain", "Locked"); return; }
    bool on = false;
    if (request->hasParam("on")) on = (request->getParam("on")->value() == "1");
    if (on) {
      startDispensing(MAX_VOLUME_L);
      currentState = STATE_MANUAL_POUR;
      handDetected = true;
    } else {
      stopDispensing(true);
      currentState = STATE_COMPLETE;
      stateEnterMs = millis();
    }
    request->send(200, "text/plain", "OK");
  });

  // ---------- FLOW CALIBRATION ----------
  // Place empty 1 L container under spout, Start, wait until full, Stop.
  // LCD shows live elapsed seconds. Result = seconds per litre.
  server.on("/flowstart", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (systemLocked) { request->send(403, "text/plain", "Locked"); return; }
    if (flowTestRunning) {
      request->send(200, "text/plain", "Already running");
      return;
    }
    // Stop any active pour first
    setPump(false);
    calibrationMode  = true;
    flowTestRunning  = true;
    flowTestStartMs  = millis();
    dispensedVolume  = 0.0f;
    setPump(true);
    currentState = STATE_CALIBRATING;
    stateEnterMs = millis();
    Serial.println("Flow cal START: pump ON, fill exactly 1 L then Stop");
    request->send(200, "text/plain", "Pump ON. Stop when container has exactly 1 L");
  });

  server.on("/flowstop", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!flowTestRunning) {
      request->send(400, "text/plain", "Flow test not running");
      return;
    }
    setPump(false);
    float elapsed = (millis() - flowTestStartMs) / 1000.0f;
    flowTestRunning = false;
    calibrationMode = false;
    currentState = STATE_IDLE;
    handDetected = false;

    if (elapsed < 3.0f) {
      Serial.printf("Flow cal too short: %.1fs\n", elapsed);
      request->send(400, "text/plain",
                    "Too short (" + String(elapsed, 1) + "s). Need at least 3s");
      return;
    }
    if (elapsed > 300.0f) {
      Serial.printf("Flow cal too long: %.1fs\n", elapsed);
      request->send(400, "text/plain",
                    "Too long (" + String(elapsed, 1) + "s). Check pump");
      return;
    }

    secondsPerLiter = elapsed;   // seconds to fill 1 litre
    saveSettings();
    Serial.printf("Flow calibrated: %.2f s/L (%.2f L/min)\n",
                  secondsPerLiter, 60.0f / secondsPerLiter);
    request->send(200, "text/plain",
                  "OK " + String(secondsPerLiter, 2) + " s per L (" +
                  String(60.0f / secondsPerLiter, 2) + " L/min)");
  });

  // ---------- DISTANCE CALIBRATION ----------
  // IMPORTANT: Runs with calibrationMode=true so FSM ignores gestures
  server.on("/calmax", HTTP_GET, [](AsyncWebServerRequest *request) {
    calibrationMode = true;
    currentState = STATE_CALIBRATING;
    setPump(false);

    float sum = 0; int n = 0;
    unsigned long start = millis();
    while (millis() - start < 2000) {
      float d = readDistanceAveraged();
      if (d > 3.0f && d < 50.0f) {
        sum += d; n++;
      }
      delay(60);
    }

    calibrationMode = false;
    currentState = STATE_IDLE;

    if (n > 10) {
      maxCalDist = sum / n;   // farther position = 2.0 L
      saveSettings();
      Serial.printf("Calibrated max dist (2.0L): %.1f cm\n", maxCalDist);
      request->send(200, "text/plain",
                    "Max calibrated (2.0L farther): " + String(maxCalDist, 1) + " cm");
    } else {
      request->send(400, "text/plain",
                    "Unstable readings (" + String(n) + " valid). Hold hand steady");
    }
  });

  server.on("/calmin", HTTP_GET, [](AsyncWebServerRequest *request) {
    calibrationMode = true;
    currentState = STATE_CALIBRATING;
    setPump(false);

    float sum = 0; int n = 0;
    unsigned long start = millis();
    while (millis() - start < 2000) {
      float d = readDistanceAveraged();
      if (d > 3.0f && d < 50.0f) {
        sum += d; n++;
      }
      delay(60);
    }

    calibrationMode = false;
    currentState = STATE_IDLE;

    if (n > 10) {
      minCalDist = sum / n;   // closer position = 0.1 L
      saveSettings();
      Serial.printf("Calibrated min dist (0.1L): %.1f cm\n", minCalDist);
      request->send(200, "text/plain",
                    "Min calibrated (0.1L closer): " + String(minCalDist, 1) + " cm");
    } else {
      request->send(400, "text/plain",
                    "Unstable readings (" + String(n) + " valid). Hold hand steady");
    }
  });

  // ---------- MAINTENANCE ----------
  server.on("/newcan", HTTP_GET, [](AsyncWebServerRequest *request) {
    remainingCan = CAN_CAPACITY_L;
    saveSettings();
    request->send(200, "text/plain", "Can reset to 18.9 L");
  });

  server.on("/lock", HTTP_GET, [](AsyncWebServerRequest *request) {
    systemLocked = !systemLocked;
    saveSettings();
    if (systemLocked) {
      setPump(false);
      currentState = STATE_SYSTEM_LOCKED;
    } else {
      currentState = STATE_IDLE;
    }
    request->send(200, "text/plain", systemLocked ? "SYSTEM LOCKED" : "UNLOCKED");
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    prefs.begin("dispenser", false);
    prefs.clear();
    prefs.end();
    secondsPerLiter       = 12.0f;
    minCalDist            = 8.0f;
    maxCalDist            = 25.0f;
    remainingCan          = CAN_CAPACITY_L;
    systemLocked          = false;
    totalDispensedAllTime = 0.0f;
    totalDispenseCount    = 0;
    currentState          = STATE_IDLE;
    calibrationMode       = false;
    request->send(200, "text/plain", "All settings reset to defaults");
  });
}

String getStatusJSON() {
  String stateStr = "Idle";
  switch (currentState) {
    case STATE_IDLE:
    case STATE_GESTURE_DETECT: stateStr = "Idle"; break;
    case STATE_MANUAL_POUR:    stateStr = "Manual Pour"; break;
    case STATE_SELECT_VOLUME:  stateStr = "Selecting Volume"; break;
    case STATE_VOLUME_LOCKED:  stateStr = "Locked Swipe"; break;
    case STATE_DISPENSING:     stateStr = "Dispensing"; break;
    case STATE_COMPLETE:       stateStr = "Complete"; break;
    case STATE_SYSTEM_LOCKED:  stateStr = "SYSTEM LOCKED"; break;
    case STATE_CALIBRATING:
      stateStr = flowTestRunning ? "Flow Calibration" : "Distance Calibration";
      break;
    default: break;
  }
  if (remainingCan < LOW_WATER_THRESH && !systemLocked && !calibrationMode)
    stateStr = "Low Water";

  float avgVol = (totalDispenseCount > 0)
                   ? (totalDispensedAllTime / (float)totalDispenseCount)
                   : 0.0f;
  int cansUsed = (totalDispensedAllTime > 0.0f)
                   ? (int)floor(totalDispensedAllTime / CAN_CAPACITY_L) + 1
                   : 0;
  float canPct = constrain((remainingCan / CAN_CAPACITY_L) * 100.0f, 0.0f, 100.0f);
  float litersPerMin = (secondsPerLiter > 0.1f) ? (60.0f / secondsPerLiter) : 0.0f;
  float flowElapsed = flowTestRunning
                        ? ((millis() - flowTestStartMs) / 1000.0f)
                        : 0.0f;

  String json = "{";
  json += "\"state\":\"" + stateStr + "\",";
  json += "\"remain\":" + String(remainingCan, 2) + ",";
  json += "\"selected\":" + String(selectedVolume, 1) + ",";
  json += "\"dispensed\":" + String(dispensedVolume, 3) + ",";
  json += "\"locked\":" + String(systemLocked ? "true" : "false") + ",";
  json += "\"secPerL\":" + String(secondsPerLiter, 2) + ",";
  json += "\"lpm\":" + String(litersPerMin, 2) + ",";
  json += "\"distance\":" + String(currentDistance, 1) + ",";
  json += "\"totalVol\":" + String(totalDispensedAllTime, 2) + ",";
  json += "\"totalCnt\":" + String(totalDispenseCount) + ",";
  json += "\"avgVol\":" + String(avgVol, 2) + ",";
  json += "\"cansUsed\":" + String(cansUsed) + ",";
  json += "\"canPct\":" + String(canPct, 1) + ",";
  json += "\"minDist\":" + String(minCalDist, 1) + ",";
  json += "\"maxDist\":" + String(maxCalDist, 1) + ",";
  json += "\"flowRunning\":" + String(flowTestRunning ? "true" : "false") + ",";
  json += "\"flowElapsed\":" + String(flowElapsed, 1) + ",";
  json += "\"uptime\":" + String(systemUptimeMs);
  json += "}";
  return json;
}
