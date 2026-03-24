/**
 * Smart Door Lock – ESP8266 NodeMCU Firmware
 * Board  : NodeMCU 1.0 (ESP-12E) / CP2102
 * Relay  : 2-channel, ACTIVE HIGH, on GPIO14 (D5)   ← moved from D2 to free I2C pins
 *            HIGH = ON (unlock) | LOW = OFF (locked)
 * LED    : Built-in LED (D4 / GPIO2), ACTIVE LOW — blinks when WiFi connected
 * LCD    : 16x2 I2C — SDA → D2 (GPIO4) | SCL → D1 (GPIO5)
 * Logic  : Poll Google Apps Script every 1.5 s.
 *          If unlock_flag == true → open relay 4 s → reset flag.
 *
 * BOOT SAFETY NOTES:
 *   - Relay pin driven LOW (relay OFF) on boot — active-HIGH relay is
 *     naturally safe since ESP8266 pins default LOW at power-on.
 *   - D5 (GPIO14) has no special boot function; safe for relay control.
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>   // required for HTTPS on ESP8266
#include <ArduinoJson.h>               // v6.x  (Library Manager: "ArduinoJson" by Benoit Blanchon)

// ── LCD ADDITION: required libraries ──────────────
#include <Wire.h>                      // I2C bus (built-in with ESP8266 core)
#include <LiquidCrystal_I2C.h>        // Library Manager: "LiquidCrystal I2C" by Frank de Brabander
// ──────────────────────────────────────────────────

// ─────────────────────────────────────────────
//  USER CONFIG  — edit before uploading
// ─────────────────────────────────────────────
const char* WIFI_SSID     = "none";
const char* WIFI_PASSWORD = "";


const char* API_BASE_URL =
  "https://script.google.com/macros/s/AKfycbylvc6xeCWt1qGqZ4Vwtg7XAmLuCef11YHlAaOnDe8a6w24QqygtlZDXGHI8xH-EuvysQ/exec";

const char* API_KEY = "satyam123";

// ─────────────────────────────────────────────
//  HARDWARE CONFIG
// ─────────────────────────────────────────────
// ⚠️ PIN CHANGE: Relay moved from D2 (GPIO4) → D5 (GPIO14)
// Reason: D2 (GPIO4) = I2C SDA needed by the LCD module.
// D5 (GPIO14) has no special boot-state concerns and is safe for relay control.
const int RELAY_PIN         = D5;      // GPIO14 — wire IN1 here  (was D2/GPIO4)
const bool RELAY_ACTIVE_LOW = false;   // ACTIVE HIGH: HIGH = ON (unlock), LOW = OFF (locked)

// Built-in LED is on D4 (GPIO2), active LOW.
const int STATUS_LED        = LED_BUILTIN;  // D4

const unsigned long POLL_INTERVAL_MS   = 500;  // ms between API polls
const unsigned long UNLOCK_DURATION_MS = 8000;  // ms relay stays on
const unsigned long LED_BLINK_INTERVAL = 600;   // ms per blink half-cycle (non-blocking)

// ── LCD ADDITION: LCD object ───────────────────────
// Address 0x27 is the most common I2C address for these modules.
// If the display stays blank, try 0x3F instead.
// SDA → D2 (GPIO4) | SCL → D1 (GPIO5)
LiquidCrystal_I2C lcd(0x27, 16, 2);
// ──────────────────────────────────────────────────

// ─────────────────────────────────────────────
//  GLOBALS
// ─────────────────────────────────────────────
unsigned long lastPollTime    = 0;
unsigned long lastLedToggle   = 0;
bool          ledState        = false;
bool          wifiConnected   = false;

// ─────────────────────────────────────────────
//  RELAY HELPERS
// ─────────────────────────────────────────────
void relayOn() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? LOW : HIGH);
  Serial.println("[RELAY] ON  — door unlocking");
}

void relayOff() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  Serial.println("[RELAY] OFF — door locked");
}

void initRelay() {
  // Active-HIGH relay: LOW = OFF (locked), HIGH = ON (unlock).
  // ESP8266 pins default LOW at boot, so this relay is naturally safe —
  // it cannot accidentally fire before firmware runs.
  // We still assert LOW explicitly for clarity and reset-safety.
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);    // LOW = relay OFF (active-HIGH module)
  delay(50);
  Serial.println("[RELAY] Initialized — locked");
}

// ─────────────────────────────────────────────
//  LED STATUS  (non-blocking, millis-based)
// ─────────────────────────────────────────────
void initLed() {
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);  // HIGH = OFF (active LOW)
}

// Call this from loop() on every iteration.
// When WiFi is connected: blinks at LED_BLINK_INTERVAL rate.
// When WiFi is down: LED stays off.
// During unlock the LED fast-blinks automatically because
// performUnlock() calls this in its own tight loop.
void updateLed() {
  if (!wifiConnected) {
    digitalWrite(STATUS_LED, HIGH);  // OFF when no WiFi
    return;
  }
  if (millis() - lastLedToggle >= LED_BLINK_INTERVAL) {
    lastLedToggle = millis();
    ledState = !ledState;
    digitalWrite(STATUS_LED, ledState ? LOW : HIGH);  // active LOW
  }
}

// Fast-blink during unlock so user gets visual feedback
void ledFastBlink(unsigned long durationMs) {
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    digitalWrite(STATUS_LED, LOW);   delay(80);
    digitalWrite(STATUS_LED, HIGH);  delay(80);
  }
  ledState = false;
}

// ─────────────────────────────────────────────
//  LCD ADDITION: LCD HELPERS
// ─────────────────────────────────────────────
void initLCD() {
  Wire.begin(4, 5);          // SDA = GPIO4 (D2), SCL = GPIO5 (D1)
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("  Booting...    ");
  Serial.println("[LCD] Initialized");
}

void lcdShow(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
// ──────────────────────────────────────────────────

// ─────────────────────────────────────────────
//  WIFI
// ─────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("\n[WiFi] Connecting to %s", WIFI_SSID);

  // ── LCD ADDITION ──
  lcdShow("WiFi Connecting", WIFI_SSID);
  // ──────────────────

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000) {
      Serial.println("\n[WiFi] Timeout — restarting");

      // ── LCD ADDITION ──
      lcdShow("WiFi Timeout!", "Restarting...");
      delay(1500);
      // ──────────────────

      ESP.restart();
    }
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected  IP: %s\n", WiFi.localIP().toString().c_str());
  wifiConnected = true;

  // ── LCD ADDITION ──
  lcdShow("WiFi Connected!", WiFi.localIP().toString().c_str());
  delay(2000);           // let user read the IP, then fall through to LOCKED
  lcdShow("Status:", "  LOCKED");
  // ──────────────────
}

void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    Serial.println("[WiFi] Lost — reconnecting...");

    // ── LCD ADDITION ──
    lcdShow("WiFi Lost!", "Reconnecting...");
    // ──────────────────

    connectWiFi();
  }
}

// ─────────────────────────────────────────────
//  HTTP GET  — returns parsed JSON or empty doc
// ─────────────────────────────────────────────
bool checkUnlockFlag() {
  // Build URL:  ...exec?action=check_flag&key=satyam123
  String url = String(API_BASE_URL) + "?action=check_flag&key=" + API_KEY;

  // ── LCD ADDITION ──
  lcdShow("Checking...", "Please wait");
  // ──────────────────

  // HTTPS on ESP8266 requires BearSSL client.
  // Using setInsecure() skips certificate validation — acceptable for
  // a local IoT device polling a known Google endpoint.
  BearSSL::WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // follow Google redirect
  http.setTimeout(8000);

  int httpCode = http.GET();
  bool flagValue = false;

  if (httpCode == HTTP_CODE_OK) {
    String body = http.getString();
    Serial.printf("[HTTP] Response: %s\n", body.c_str());

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (!err) {
      flagValue = doc["unlock_flag"] | false;
    } else {
      Serial.printf("[JSON] Parse error: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[HTTP] Error: %d  %s\n", httpCode, http.errorToString(httpCode).c_str());
  }

  http.end();

  // ── LCD ADDITION: restore LOCKED display if no flag ──
  if (!flagValue) {
    lcdShow("Status:", "  LOCKED");
  }
  // ──────────────────────────────────────────────────────

  return flagValue;
}

// ─────────────────────────────────────────────
//  RESET FLAG  — tell backend flag was consumed
// ─────────────────────────────────────────────
void resetFlag() {
  String url = String(API_BASE_URL) + "?action=reset_flag&key=" + API_KEY;

  BearSSL::WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    Serial.println("[HTTP] Flag reset OK");
  } else {
    Serial.printf("[HTTP] Reset error: %d\n", httpCode);
  }
  http.end();
}

// ─────────────────────────────────────────────
//  UNLOCK SEQUENCE
// ─────────────────────────────────────────────
void performUnlock() {
  Serial.println("[UNLOCK] Flag detected — triggering relay");

  // ── LCD ADDITION ──
  lcdShow("UNLOCKING...", "Please wait...");
  // ──────────────────

  resetFlag();                          // clear flag before opening (prevents re-trigger)
  relayOn();
  ledFastBlink(UNLOCK_DURATION_MS);    // visual feedback; also provides the 4 s timing
  relayOff();

  // ── LCD ADDITION ──
  lcdShow("Status:", "  LOCKED AGAIN");
  delay(2000);
  lcdShow("Status:", "  LOCKED");
  // ──────────────────

  Serial.println("[UNLOCK] Sequence complete");
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=============================");
  Serial.println(" Smart Door Lock — ESP8266  ");
  Serial.println("=============================");

  // ── LCD ADDITION: init LCD first so boot messages are visible ──
  initLCD();
  delay(1500);   // show "Booting..." briefly
  // ───────────────────────────────────────────────────────────────

  initRelay();
  initLed();
  Serial.println("[BOOT] Relay: D5 (GPIO14) | LED: D4 (GPIO2, built-in) | LCD: SDA=D2 SCL=D1");
  connectWiFi();

  Serial.println("[BOOT] Ready. Polling started.");
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
  updateLed();   // non-blocking; must run every loop iteration

  if (millis() - lastPollTime >= POLL_INTERVAL_MS) {
    lastPollTime = millis();
    ensureWiFi();

    if (checkUnlockFlag()) {
      performUnlock();
    }
  }
}
