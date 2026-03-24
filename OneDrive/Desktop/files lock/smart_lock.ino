/**
 * Smart Door Lock – ESP32 Firmware
 * Architecture: WiFi client, polling-based unlock via Google Apps Script API
 *
 * Active modules:   WiFi + Relay (solenoid lock)
 * Stubbed modules:  Keypad, RFID/NFC, LCD, Buzzer
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>   // Install via Library Manager: "ArduinoJson" by Benoit Blanchon

// ─────────────────────────────────────────────
//  CONFIG  –  Edit these before uploading
// ─────────────────────────────────────────────
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// Google Apps Script Web App URL (deployed as "Anyone" access)
#define API_BASE_URL    "https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID/exec"

// Shared secret sent with every request – must match Apps Script
#define API_KEY         "CHANGE_ME_SECRET_KEY"

// Hardware pins
#define RELAY_PIN       26    // GPIO connected to relay IN pin (active-LOW or HIGH – see RELAY_ACTIVE_HIGH)
#define RELAY_ACTIVE_HIGH  true   // Set false if your relay triggers on LOW

// Timing
#define POLL_INTERVAL_MS   1500   // How often ESP32 checks for unlock flag (ms)
#define UNLOCK_DURATION_MS 4000   // How long relay stays ON (ms)

// ─────────────────────────────────────────────
//  GLOBALS
// ─────────────────────────────────────────────
unsigned long lastPollTime = 0;

// ─────────────────────────────────────────────
//  HARDWARE HELPERS
// ─────────────────────────────────────────────

void relayOn() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
}

void relayOff() {
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
}

void initRelay() {
  pinMode(RELAY_PIN, OUTPUT);
  relayOff();   // Ensure locked on boot
}

// ─────────────────────────────────────────────
//  WIFI
// ─────────────────────────────────────────────

void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000) {
      Serial.println("\n[WiFi] TIMEOUT – restarting");
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconnecting...");
    connectWiFi();
  }
}

// ─────────────────────────────────────────────
//  HTTP HELPERS
// ─────────────────────────────────────────────

/**
 * GET request to Apps Script.
 * Returns raw response body, or "" on error.
 */
String httpGet(const String& url) {
  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // Apps Script redirects
  http.setTimeout(8000);

  int code = http.GET();
  String body = "";
  if (code == 200) {
    body = http.getString();
  } else {
    Serial.printf("[HTTP GET] Error %d\n", code);
  }
  http.end();
  return body;
}

/**
 * POST request to Apps Script with JSON body.
 * Returns raw response body, or "" on error.
 */
String httpPost(const String& url, const String& jsonPayload) {
  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(jsonPayload);
  String body = "";
  if (code == 200) {
    body = http.getString();
  } else {
    Serial.printf("[HTTP POST] Error %d\n", code);
  }
  http.end();
  return body;
}

// ─────────────────────────────────────────────
//  API CALLS  (Apps Script endpoints)
// ─────────────────────────────────────────────

/**
 * GET  /exec?action=check_flag&key=API_KEY
 * Returns true if server has a pending unlock_flag.
 */
bool checkUnlockFlag() {
  String url = String(API_BASE_URL)
    + "?action=check_flag"
    + "&key=" + API_KEY;

  String response = httpGet(url);
  if (response == "") return false;

  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("[checkUnlockFlag] JSON parse error: %s\n", err.c_str());
    return false;
  }
  return doc["unlock_flag"] | false;
}

/**
 * POST  {action:"reset_flag", key:...}
 * Clears the unlock_flag in the sheet so the door doesn't re-trigger.
 */
void resetUnlockFlag() {
  StaticJsonDocument<128> doc;
  doc["action"] = "reset_flag";
  doc["key"]    = API_KEY;

  String payload;
  serializeJson(doc, payload);
  httpPost(API_BASE_URL, payload);
  Serial.println("[API] Flag reset.");
}

/**
 * POST  {action:"log_event", method:..., status:..., extra:..., key:...}
 * Logs an event row to Google Sheets.
 */
void logEvent(const char* method, const char* status, const char* extra) {
  StaticJsonDocument<256> doc;
  doc["action"] = "log_event";
  doc["method"] = method;
  doc["status"] = status;
  doc["extra"]  = extra;
  doc["key"]    = API_KEY;

  String payload;
  serializeJson(doc, payload);
  String resp = httpPost(API_BASE_URL, payload);
  Serial.printf("[API] Log sent – method:%s status:%s | resp: %s\n", method, status, resp.c_str());
}

// ─────────────────────────────────────────────
//  UNLOCK FLOW
// ─────────────────────────────────────────────

void performUnlock(const char* method) {
  Serial.printf("[UNLOCK] Triggered via %s\n", method);

  relayOn();
  Serial.println("[UNLOCK] Relay ON – door open");
  delay(UNLOCK_DURATION_MS);
  relayOff();
  Serial.println("[UNLOCK] Relay OFF – door locked");

  logEvent(method, "SUCCESS", "Door unlocked");
}

// ─────────────────────────────────────────────
//  MODULE STUBS  (wire in later – no code change to core loop)
// ─────────────────────────────────────────────

/**
 * KEYPAD MODULE STUB
 * Wire a 4x4 keypad here. Return true + populate `enteredCode`
 * when user has finished entering a PIN.
 * Requires: Keypad.h library  +  physical 4x4 keypad on GPIOs
 */
bool keypadCheckPin(String& enteredCode) {
  // TODO: initialize Keypad in setup(), read keys here
  // Example (after wiring):
  //   char key = keypad.getKey();
  //   if (key) { buffer += key; }
  //   if (buffer.length() == 4) { enteredCode = buffer; buffer = ""; return true; }
  return false;
}

/**
 * RFID/NFC MODULE STUB
 * Wire an RC522 or PN532 here. Return true + populate `tagId`
 * when a card is presented.
 * Requires: MFRC522.h or Adafruit_PN532.h  +  SPI wiring
 */
bool rfidCheckTag(String& tagId) {
  // TODO: initialize RFID in setup(), scan for tag here
  // Example (MFRC522):
  //   if (!mfrc522.PICC_IsNewCardPresent()) return false;
  //   if (!mfrc522.PICC_ReadCardSerial()) return false;
  //   tagId = uidToString(mfrc522.uid);
  //   return true;
  return false;
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Smart Door Lock – Booting ===");

  initRelay();
  connectWiFi();

  // TODO (future): initKeypad();
  // TODO (future): initRFID();
  // TODO (future): initLCD();
  // TODO (future): initBuzzer();

  Serial.println("[BOOT] System ready. Polling...");
}

// ─────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────

void loop() {

  // ── 1. Web / Cloud polling ──────────────────
  if (millis() - lastPollTime >= POLL_INTERVAL_MS) {
    lastPollTime = millis();
    ensureWiFi();

    if (checkUnlockFlag()) {
      resetUnlockFlag();       // Clear flag BEFORE unlocking (prevent double-trigger)
      performUnlock("WEB");
    }
  }

  // ── 2. Keypad check (stub – no-op until wired) ──
  {
    String pin;
    if (keypadCheckPin(pin)) {
      if (pin == "1234") {     // TODO: replace with configurable PIN
        performUnlock("KEYPAD");
      } else {
        logEvent("KEYPAD", "FAILED", ("Wrong PIN: " + pin).c_str());
        Serial.println("[KEYPAD] Wrong PIN.");
      }
    }
  }

  // ── 3. RFID check (stub – no-op until wired) ──
  {
    String tag;
    if (rfidCheckTag(tag)) {
      // TODO: maintain an allowlist of authorised tag UIDs
      String allowedTag = "AABBCCDD";
      if (tag == allowedTag) {
        performUnlock("RFID");
      } else {
        logEvent("RFID", "FAILED", ("Unknown tag: " + tag).c_str());
        Serial.println("[RFID] Unknown tag.");
      }
    }
  }
}
