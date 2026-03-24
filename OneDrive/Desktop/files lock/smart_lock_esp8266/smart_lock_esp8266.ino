/**
 * Smart Door Lock – ESP8266 NodeMCU Firmware
 * Board  : NodeMCU 1.0 (ESP-12E) / CP2102
 * Relay  : 2-channel, ACTIVE LOW, on GPIO4 (D2)  ← moved from D1 for boot safety
 * LED    : Built-in LED (D4 / GPIO2), ACTIVE LOW — blinks when WiFi connected
 * Logic  : Poll Google Apps Script every 1.5 s.
 *          If unlock_flag == true → open relay 4 s → reset flag.
 *
 * BOOT SAFETY NOTES:
 *   - Relay pin is driven HIGH (relay OFF) BEFORE pinMode, via direct register write.
 *   - D2 (GPIO4) has no special boot function; safe for relay control.
 *   - A 10k pull-up resistor from D2 → 3.3V is recommended in hardware.
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>   // required for HTTPS on ESP8266
#include <ArduinoJson.h>               // v6.x  (Library Manager: "ArduinoJson" by Benoit Blanchon)

// ─────────────────────────────────────────────
//  USER CONFIG  — edit before uploading
// ─────────────────────────────────────────────
const char* WIFI_SSID     = "none";
const char* WIFI_PASSWORD = "";

const char* API_BASE_URL  =
  "https://script.google.com/macros/s/"
  "AKfycbwxT4UXNYJZGtFWGc_sHcWmUQrHfmm1KmGc2x3aWsHosBaII69DHyY8kfr-iMJT_iox3w"
  "/exec";

const char* API_KEY = "satyam123";

// ─────────────────────────────────────────────
//  HARDWARE CONFIG
// ─────────────────────────────────────────────
// D2 (GPIO4) is used instead of D1 (GPIO5).
// Both are safe, but D2 has no special boot-state concerns and is
// the recommended pin when a pull-up resistor is added.
const int RELAY_PIN         = D2;      // GPIO4 — wire IN1 here
const bool RELAY_ACTIVE_LOW = true;    // relay triggers on LOW; OFF = HIGH

// Built-in LED is on D4 (GPIO2), active LOW.
const int STATUS_LED        = LED_BUILTIN;  // D4

const unsigned long POLL_INTERVAL_MS   = 1500;  // ms between API polls
const unsigned long UNLOCK_DURATION_MS = 4000;  // ms relay stays on
const unsigned long LED_BLINK_INTERVAL = 600;   // ms per blink half-cycle (non-blocking)

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
  // STEP 1 — drive pin HIGH via direct GPIO register BEFORE pinMode.
  // This is the earliest possible moment; prevents the ~1 ms LOW glitch
  // that occurs when pinMode(OUTPUT) is called on a pin that defaulted LOW.
  // For an active-LOW relay this glitch is enough to trigger it.
  digitalWrite(RELAY_PIN, HIGH);   // HIGH = relay OFF (active-LOW module)
  pinMode(RELAY_PIN, OUTPUT);      // now set direction — pin stays HIGH
  digitalWrite(RELAY_PIN, HIGH);   // belt-and-suspenders: assert again
  delay(50);                       // let relay settle; confirm no chatter
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
//  WIFI
// ─────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("\n[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000) {
      Serial.println("\n[WiFi] Timeout — restarting");
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected  IP: %s\n", WiFi.localIP().toString().c_str());
  wifiConnected = true;
}

void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    Serial.println("[WiFi] Lost — reconnecting...");
    connectWiFi();
  }
}

// ─────────────────────────────────────────────
//  HTTP GET  — returns parsed JSON or empty doc
// ─────────────────────────────────────────────
bool checkUnlockFlag() {
  // Build URL:  ...exec?action=check_flag&key=satyam123
  String url = String(API_BASE_URL) + "?action=check_flag&key=" + API_KEY;

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
  resetFlag();                          // clear flag before opening (prevents re-trigger)
  relayOn();
  ledFastBlink(UNLOCK_DURATION_MS);    // visual feedback; also provides the 4 s timing
  relayOff();
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

  initRelay();
  initLed();
  Serial.println("[BOOT] Relay: D2 (GPIO4) | LED: D4 (GPIO2, built-in)");
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
