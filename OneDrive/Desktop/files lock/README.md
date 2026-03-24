# Smart Door Lock – Setup Guide

## Files

```
smart_lock/
├── esp32/
│   └── smart_lock.ino          ← Upload to ESP32
├── apps_script/
│   └── Code.gs                 ← Paste into Google Apps Script
└── website/
    └── index.html              ← Deploy to Netlify / GitHub Pages
```

---

## ⚡ Quick-start checklist

- [ ] 1. Deploy Apps Script → copy Deployment URL
- [ ] 2. Pick a secret API key (any random string, e.g. `xK9mP2qZ`)
- [ ] 3. Paste URL + key into `Code.gs`, `smart_lock.ino`, `index.html`
- [ ] 4. Upload firmware to ESP32
- [ ] 5. Deploy website
- [ ] 6. Test end-to-end

---

## Step 1 – Google Sheets + Apps Script

1. Create a new Google Spreadsheet.
2. Open **Extensions → Apps Script**.
3. Delete the placeholder code and paste `Code.gs` contents.
4. At the top of the file set your API key:
   ```js
   var API_KEY = "your-chosen-secret";
   ```
5. **Deploy → New Deployment**
   - Type: **Web App**
   - Execute as: **Me**
   - Who has access: **Anyone**
6. Copy the deployment URL — looks like:
   `https://script.google.com/macros/s/AKfyc.../exec`

The script auto-creates two sheets:
| Sheet | Contents |
|-------|----------|
| `logs`  | One row per event: Timestamp, Method, Status, Extra |
| `flags` | `A1` = unlock_flag (`true`/`false`), `B1` = active OTP |

> **Every time you edit Code.gs you must create a NEW deployment** — editing an existing one doesn't update the live URL.

---

## Step 2 – ESP32 Firmware

### Prerequisites

- Arduino IDE with **esp32 by Espressif** board package
- Library: **ArduinoJson** by Benoit Blanchon (Library Manager)

### Edit these 4 lines at the top of `smart_lock.ino`

```cpp
#define WIFI_SSID       "your-wifi-name"
#define WIFI_PASSWORD   "your-wifi-password"
#define API_BASE_URL    "https://script.google.com/macros/s/YOUR_ID/exec"
#define API_KEY         "your-chosen-secret"   // must match Code.gs
```

### Wiring

```
ESP32 GPIO 26  ──►  Relay IN
ESP32 5V/3.3V  ──►  Relay VCC  (check relay module datasheet)
ESP32 GND      ──►  Relay GND
Relay NO       ──►  One terminal of solenoid
12V supply     ──►  Other terminal of solenoid + Relay COM
```

> If your relay activates on LOW instead of HIGH, set `RELAY_ACTIVE_HIGH false` in the config block.

### Upload

Select **DOIT ESP32 DevKit V1** + correct COM port → Upload.
Open Serial Monitor at **115200 baud** to watch logs.

---

## Step 3 – Website

1. Open `website/index.html` in a text editor.
2. Edit the two lines inside the `<script>` block at the bottom:
   ```js
   const API_URL = "https://script.google.com/macros/s/YOUR_ID/exec";
   const API_KEY = "your-chosen-secret";
   ```
3. Deploy:
   - **Netlify**: drag-and-drop the `website/` folder at netlify.com/drop
   - **GitHub Pages**: push `index.html` to a repo and enable Pages

---

## Complete data flow

```
USER
 │
 │  1. Clicks "SEND OTP TO TELEGRAM"
 ▼
WEBSITE  ──POST {action:"request_unlock"}──►  APPS SCRIPT
                                                │
                                                ├─ Generates random 4-digit OTP
                                                ├─ Saves OTP to flags sheet B1
                                                ├─ Sends Telegram message:
                                                │    "Your Smart Lock OTP is: 1234"
                                                └─ Returns {success: true}
USER receives Telegram message
 │
 │  2. Enters OTP on website → clicks "VERIFY & UNLOCK"
 ▼
WEBSITE  ──POST {action:"verify_otp", otp:"1234"}──►  APPS SCRIPT
                                                          │
                                                          ├─ Compares submitted vs stored OTP
                                                          ├─ Match: sets flags A1 = "true"
                                                          ├─ Clears B1 (OTP used up)
                                                          ├─ Logs SUCCESS row
                                                          └─ Returns {success: true}
 │
 │  3. Website shows "Door Unlocked" screen
 │
ESP32 (polling every 1.5s)
 │  ──GET {action:"check_flag"}──►  APPS SCRIPT → {unlock_flag: true}
 │
 ├─ POST reset_flag  (clears A1 immediately – no double trigger)
 ├─ Relay ON  (4 seconds)
 ├─ Relay OFF
 └─ POST log_event {method:"WEB", status:"SUCCESS"}
```

---

## API reference

| Method | Payload / Params | Purpose |
|--------|-----------------|---------|
| `POST` | `{action:"request_unlock", method, key}` | Generate OTP → send to Telegram |
| `POST` | `{action:"verify_otp", otp, method, key}` | Validate OTP → set unlock flag |
| `GET`  | `?action=check_flag&key=...` | ESP32 polls this |
| `POST` | `{action:"reset_flag", key}` | ESP32 clears flag after unlock |
| `POST` | `{action:"log_event", method, status, extra, key}` | ESP32 logs outcome |

---

## Adding hardware later (no core rewrites needed)

### Keypad
1. Wire 4×4 keypad to free GPIOs.
2. Install `Keypad.h` library.
3. Fill in the `keypadCheckPin()` stub in `smart_lock.ino` — it already sits in the main loop.

### RFID / NFC (RC522)
1. Wire RC522 via SPI.
2. Install `MFRC522.h`.
3. Fill in the `rfidCheckTag()` stub and add allowed UIDs.

### LCD / Buzzer
1. Add `initLCD()` / `initBuzzer()` in `setup()`.
2. Call them from `performUnlock()` and the failed-auth branches.
