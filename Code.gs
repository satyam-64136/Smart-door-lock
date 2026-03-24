/**
 * Smart Door Lock – Google Apps Script Backend
 *
 * Deploy as:  Extensions → Apps Script → Deploy → New deployment
 *             Type: Web App | Execute as: Me | Access: Anyone
 *
 * Sheet columns (Row 1 = headers):
 *   A: Timestamp  B: Method  C: Status  D: Extra
 *
 * A second sheet named "flags" holds the unlock_flag:
 *   A1: unlock_flag  (value: "true" or "false")
 *
 * Endpoints:
 *   GET  ?action=check_flag&key=SECRET     → {unlock_flag: bool}
 *   POST {action:"request_unlock", method, key} → generates OTP, sends to Telegram, logs PENDING
 *   POST {action:"verify_otp", otp, method, key} → validates OTP, sets unlock flag on match
 *   POST {action:"reset_flag", key}            → clears flag
 *   POST {action:"log_event", method, status, extra, key}  → appends log row
 */

// ── Config ──────────────────────────────────────
var API_KEY        = "satyam123";  // Must match ESP32 firmware
var LOG_SHEET      = "logs";
var FLAGS_SHEET    = "flags";
var FLAG_CELL      = "A1";  // Cell in flags sheet: holds "true"/"false"
var OTP_CELL       = "B1";  // Cell in flags sheet: holds the active OTP

var TELEGRAM_TOKEN = "8614619376:AAEhcyNtmJnFB6ylNAoAT03kNVC9qLGsp3U";
var TELEGRAM_CHAT  = "5471661264";
// ────────────────────────────────────────────────

// ── Helpers ─────────────────────────────────────

function getLogSheet() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sh = ss.getSheetByName(LOG_SHEET);
  if (!sh) {
    sh = ss.insertSheet(LOG_SHEET);
    sh.appendRow(["Timestamp", "Method", "Status", "Extra"]);
  }
  return sh;
}

function getFlagsSheet() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sh = ss.getSheetByName(FLAGS_SHEET);
  if (!sh) {
    sh = ss.insertSheet(FLAGS_SHEET);
    sh.getRange("A1").setValue("false");  // Default: locked
  }
  return sh;
}

function getUnlockFlag() {
  return getFlagsSheet().getRange(FLAG_CELL).getValue() === "true";
}

function setUnlockFlag(value) {
  getFlagsSheet().getRange(FLAG_CELL).setValue(value ? "true" : "false");
}

function saveOtp(otp) {
  getFlagsSheet().getRange(OTP_CELL).setValue(otp);
}

function getStoredOtp() {
  return String(getFlagsSheet().getRange(OTP_CELL).getValue());
}

function clearOtp() {
  getFlagsSheet().getRange(OTP_CELL).setValue("");
}

function appendLog(method, status, extra) {
  var ts = Utilities.formatDate(new Date(), Session.getScriptTimeZone(), "yyyy-MM-dd HH:mm:ss");
  getLogSheet().appendRow([ts, method, status, extra]);
}

function validateKey(key) {
  return key === API_KEY;
}

function jsonResponse(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

// ── GET handler ─────────────────────────────────
// Handles ALL actions from the website (GET avoids CORS preflight)

function doGet(e) {
  var params = e.parameter;

  // Auth
  if (!validateKey(params.key)) {
    return jsonResponse({ success: false, error: "Unauthorized" });
  }

  var action = params.action;

  // ── check_flag (ESP32) ──────────────────────
  if (action === "check_flag") {
    return jsonResponse({ unlock_flag: getUnlockFlag() });
  }

  // ── request_unlock (website Step 1) ─────────
  if (action === "request_unlock") {
    var method = params.method || "WEB";
    var otp = String(Math.floor(1000 + Math.random() * 9000));
    saveOtp(otp);

    UrlFetchApp.fetch(
      "https://api.telegram.org/bot" + TELEGRAM_TOKEN + "/sendMessage",
      {
        method:      "post",
        contentType: "application/json",
        payload:     JSON.stringify({
          chat_id: TELEGRAM_CHAT,
          text:    "Your Smart Lock OTP is: " + otp
        })
      }
    );

    setUnlockFlag(false);
    appendLog(method, "PENDING", "OTP sent via Telegram");
    return jsonResponse({ success: true, message: "OTP sent to Telegram" });
  }

  // ── verify_otp (website Step 2) ─────────────
  if (action === "verify_otp") {
    var method    = params.method || "WEB";
    var submitted = String(params.otp || "").trim();
    var stored    = getStoredOtp();

    if (submitted.length < 4) {
      appendLog(method, "FAILED", "OTP too short");
      return jsonResponse({ success: false, error: "Invalid OTP" });
    }

    if (submitted !== stored) {
      appendLog(method, "FAILED", "Wrong OTP: " + submitted);
      return jsonResponse({ success: false, error: "Incorrect OTP" });
    }

    setUnlockFlag(true);
    clearOtp();
    appendLog(method, "SUCCESS", "OTP verified – unlock flag set");
    return jsonResponse({ success: true, message: "OTP verified – door will unlock" });
  }

  return jsonResponse({ success: false, error: "Unknown action: " + action });
}

// ── POST handler ────────────────────────────────

function doPost(e) {
  var body;
  try {
    // ESP32 sends JSON in the POST body; parse it from postData.contents
    body = JSON.parse(e.postData.contents);
  } catch (err) {
    return jsonResponse({ success: false, error: "Invalid JSON" });
  }

  // Auth
  if (!validateKey(body.key)) {
    return jsonResponse({ success: false, error: "Unauthorized" });
  }

  var action = body.action;

  // ── request_unlock ──────────────────────────
  //    Sent by the website – generates OTP and sends it to Telegram
  if (action === "request_unlock") {
    var method = body.method || "WEB";

    // Generate and persist a 4-digit OTP
    var otp = String(Math.floor(1000 + Math.random() * 9000));
    saveOtp(otp);

    // Send OTP to Telegram
    UrlFetchApp.fetch(
      "https://api.telegram.org/bot" + TELEGRAM_TOKEN + "/sendMessage",
      {
        method:      "post",
        contentType: "application/json",
        payload:     JSON.stringify({
          chat_id: TELEGRAM_CHAT,
          text:    "Your Smart Lock OTP is: " + otp
        })
      }
    );

    setUnlockFlag(false);  // Flag only goes true AFTER OTP is verified
    appendLog(method, "PENDING", "OTP sent via Telegram");
    return jsonResponse({ success: true, message: "OTP sent to Telegram" });
  }

  // ── verify_otp ──────────────────────────────
  //    Sent by the website when user submits the OTP they received
  if (action === "verify_otp") {
    var method     = body.method || "WEB";
    var submitted  = String(body.otp || "").trim();
    var stored     = getStoredOtp();

    if (submitted.length < 4) {
      appendLog(method, "FAILED", "OTP too short");
      return jsonResponse({ success: false, error: "Invalid OTP" });
    }

    if (submitted !== stored) {
      appendLog(method, "FAILED", "Wrong OTP: " + submitted);
      return jsonResponse({ success: false, error: "Incorrect OTP" });
    }

    // OTP matched – arm the unlock flag and clear stored OTP
    setUnlockFlag(true);
    clearOtp();
    appendLog(method, "SUCCESS", "OTP verified – unlock flag set");
    return jsonResponse({ success: true, message: "OTP verified – door will unlock" });
  }

  // ── reset_flag ──────────────────────────────
  //    Sent by ESP32 after it has processed the unlock
  if (action === "reset_flag") {
    setUnlockFlag(false);
    return jsonResponse({ success: true, message: "Flag cleared" });
  }

  // ── log_event ───────────────────────────────
  //    Sent by ESP32 to record an outcome
  if (action === "log_event") {
    var method = body.method || "UNKNOWN";
    var status = body.status || "UNKNOWN";
    var extra  = body.extra  || "";
    appendLog(method, status, extra);
    return jsonResponse({ success: true, message: "Logged" });
  }

  return jsonResponse({ success: false, error: "Unknown action: " + action });
}