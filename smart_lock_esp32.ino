/**
 * Smart Door Lock – ESP32 Firmware
 * Works with WEBSITE and KEYPAD
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>

const char* API_BASE_URL = "https://script.google.com/macros/s/AKfycbzHVuiVRD8zBq3_TA5HhgL5eQDbnwP5zUERn-Yu8VT4Zw0rPzJceJ_Zm4pqk_QbGNaZpw/exec";
const char* WIFI_SSID     = "none";
const char* WIFI_PASSWORD = "";
const char* API_KEY = "satyam123";

const int  RELAY_PIN        = 23;
const bool RELAY_ACTIVE_LOW = false;
const unsigned long POLL_INTERVAL_MS   = 2000;
const unsigned long UNLOCK_DURATION_MS = 4000;

#define R1 13
#define R2 12
#define R3 14
#define R4 27
#define C1 26
#define C2 25
#define C3 19

char keys[4][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

byte keyPressed[4][3] = {0};
unsigned long lastKeyScanTime = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);
unsigned long lastLCDRefresh = 0;

enum KeypadState { KS_IDLE, KS_ENTERING_OTP };

KeypadState   kState     = KS_IDLE;
String        enteredOTP = "";
unsigned long lastPollTime = 0;
unsigned long otpRequestTime = 0;

String displayLine1 = "Status:";
String displayLine2 = "  LOCKED";

void lcdShow(String line1, String line2) {
  displayLine1 = line1;
  displayLine2 = line2;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  lcd.backlight();
  
  Serial.printf("[LCD] L1: %s | L2: %s\n", line1.c_str(), line2.c_str());
  lastLCDRefresh = millis();
}

void lcdKeepAlive() {
  if (millis() - lastLCDRefresh > 500) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(displayLine1);
    lcd.setCursor(0, 1);
    lcd.print(displayLine2);
    lcd.backlight();
    lastLCDRefresh = millis();
  }
}

void initLCD() {
  delay(200);
  Wire.begin(21, 22);
  delay(100);
  lcd.init();
  delay(50);
  lcd.init();
  delay(50);
  lcd.backlight();
  lcdShow("Smart Door Lock", "  Booting...  ");
}

void reinitLCD() {
  delay(200);
  Wire.end();
  delay(100);
  Wire.begin(21, 22);
  delay(100);
  lcd.init();
  delay(50);
  lcd.init();
  delay(50);
  lcd.backlight();
  delay(100);
}

void reinitKeypadPins() {
  pinMode(R1, INPUT_PULLUP);
  pinMode(R2, INPUT_PULLUP);
  pinMode(R3, INPUT_PULLUP);
  pinMode(R4, INPUT_PULLUP);
  pinMode(C1, OUTPUT);
  pinMode(C2, OUTPUT);
  pinMode(C3, OUTPUT);
  digitalWrite(C1, HIGH);
  digitalWrite(C2, HIGH);
  digitalWrite(C3, HIGH);
}

void relayOn()  { 
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? LOW  : HIGH);
  Serial.println("[RELAY] ON");
}

void relayOff() { 
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  Serial.println("[RELAY] OFF");
}

void initRelay() {
  pinMode(RELAY_PIN, OUTPUT);
  relayOff();
}

void scanKeypad() {
  if (millis() - lastKeyScanTime < 50) {
    return;
  }
  lastKeyScanTime = millis();
  
  byte rowPins[4] = {R1, R2, R3, R4};
  byte colPins[3] = {C1, C2, C3};
  
  for (int col = 0; col < 3; col++) {
    pinMode(colPins[col], OUTPUT);
    digitalWrite(colPins[col], LOW);
    delayMicroseconds(200);
    
    for (int row = 0; row < 4; row++) {
      pinMode(rowPins[row], INPUT_PULLUP);
      byte reading = !digitalRead(rowPins[row]);
      
      if (reading == 1 && keyPressed[row][col] == 0) {
        keyPressed[row][col] = 1;
        char key = keys[row][col];
        Serial.printf("[KEY PRESS] %c\n", key);
        processKey(key);
      }
      else if (reading == 0 && keyPressed[row][col] == 1) {
        keyPressed[row][col] = 0;
      }
    }
    
    digitalWrite(colPins[col], HIGH);
  }
}

void processKey(char key) {
  Serial.printf("[PROCESS] Key: %c | State: %d\n", key, kState);
  
  if (key == '*') {
    enteredOTP = "";
    kState = KS_IDLE;
    lcdShow("Cleared", "Press # for OTP");
    delay(1500);
    lcdShow("Status:", "  LOCKED");
    return;
  }
  
  if (kState == KS_IDLE) {
    if (key == '#') {
      lcdShow("Requesting OTP", "Please wait...");
      if (requestOTP()) {
        kState = KS_ENTERING_OTP;
        enteredOTP = "";
        otpRequestTime = millis();
        lcdShow("Enter OTP:", "");
      } else {
        lcdShow("OTP Failed!", "Try again");
        delay(1500);
        lcdShow("Status:", "  LOCKED");
      }
    } else {
      lcdShow("Press # first", "to request OTP");
      delay(1200);
      lcdShow("Status:", "  LOCKED");
    }
    return;
  }
  
  if (kState == KS_ENTERING_OTP) {
    unsigned long timeSinceRequest = millis() - otpRequestTime;
    if (timeSinceRequest > 600000) {
      lcdShow("OTP Expired!", "Request new one");
      delay(1500);
      kState = KS_IDLE;
      enteredOTP = "";
      lcdShow("Status:", "  LOCKED");
      return;
    }
    
    if (key == '#') {
      if (enteredOTP.length() < 4) {
        lcdShow("Need 4 digits!", "Keep typing");
        delay(1200);
        return;
      }
      
      lcdShow("Verifying...", "Please wait...");
      if (verifyOTP(enteredOTP)) {
        performUnlock();
      } else {
        enteredOTP = "";
        lcdShow("Wrong OTP!", "Try again");
        delay(1500);
        lcdShow("Enter OTP:", "");
      }
    } else {
      if (enteredOTP.length() < 4) {
        enteredOTP += key;
        String dots = "";
        for (int i = 0; i < enteredOTP.length(); i++) {
          dots += "*";
        }
        lcdShow("Enter OTP:", dots);
      }
    }
  }
}

void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s\n", WIFI_SSID);
  lcdShow("WiFi Connecting", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000) {
      lcdShow("WiFi Timeout!", "Restarting...");
      delay(1500);
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected\n");
  lcdShow("WiFi Connected!", WiFi.localIP().toString().c_str());
  delay(2000);
  lcdShow("Status:", "  LOCKED");
}

void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    lcdShow("WiFi Lost!", "Reconnecting...");
    connectWiFi();
  }
}

String httpGet(String url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);

  int code = http.GET();
  String body = "";
  if (code == HTTP_CODE_OK) {
    body = http.getString();
    Serial.printf("[HTTP] OK\n");
  } else {
    Serial.printf("[HTTP] Error %d\n", code);
  }
  http.end();
  return body;
}

bool checkUnlockFlag() {
  String url = String(API_BASE_URL) + "?action=check_flag&key=" + API_KEY;
  String body = httpGet(url);
  if (body == "") return false;
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, body)) return false;
  bool flag = doc["unlock_flag"] | false;
  Serial.printf("[CHECK] Flag: %s\n", flag ? "TRUE" : "FALSE");
  return flag;
}

void resetFlag() {
  String url = String(API_BASE_URL) + "?action=reset_flag&key=" + API_KEY;
  httpGet(url);
  Serial.println("[RESET] Flag cleared");
}

bool requestOTP() {
  String url = String(API_BASE_URL) + "?action=request_unlock&method=KEYPAD&key=" + API_KEY;
  String body = httpGet(url);
  if (body == "") return false;
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, body)) return false;
  return doc["success"] | false;
}

bool verifyOTP(String otp) {
  String url = String(API_BASE_URL) + "?action=verify_otp&method=KEYPAD&otp=" + otp + "&key=" + API_KEY;
  String body = httpGet(url);
  if (body == "") return false;
  
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, body)) return false;
  
  bool success = doc["success"] | false;
  if (!success) {
    const char* error = doc["error"] | "Unknown";
    lcdShow("Error:", error);
    delay(1500);
  }
  
  return success;
}

void performUnlock() {
  lcdShow("UNLOCKING!", "Door open...");
  resetFlag();
  
  relayOn();
  delay(UNLOCK_DURATION_MS);
  relayOff();
  
  delay(300);
  reinitLCD();
  reinitKeypadPins();
  delay(200);
  
  lcdShow("Status:", "  LOCKED");
  kState = KS_IDLE;
  enteredOTP = "";
  lastPollTime = millis();
  
  Serial.println("[SYSTEM] Ready");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Smart Door Lock ===\n");

  pinMode(R1, INPUT_PULLUP);
  pinMode(R2, INPUT_PULLUP);
  pinMode(R3, INPUT_PULLUP);
  pinMode(R4, INPUT_PULLUP);
  pinMode(C1, OUTPUT);
  pinMode(C2, OUTPUT);
  pinMode(C3, OUTPUT);
  digitalWrite(C1, HIGH);
  digitalWrite(C2, HIGH);
  digitalWrite(C3, HIGH);

  initLCD();
  delay(1500);
  initRelay();
  connectWiFi();
  Serial.println("[BOOT] Ready\n");
}

void loop() {
  lcdKeepAlive();
  scanKeypad();

  if (kState == KS_IDLE) {
    if (millis() - lastPollTime >= POLL_INTERVAL_MS) {
      lastPollTime = millis();
      ensureWiFi();
      if (checkUnlockFlag()) {
        lcdShow("WEB UNLOCK!", "Opening...");
        performUnlock();
      }
    }
  }
}