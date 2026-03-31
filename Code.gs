/**
 * Smart Door Lock – Google Apps Script Backend
 * Telegram message deletes after 45 seconds
 * OTP remains valid for 10 minutes
 */

// ── Config ──────────────────────────────────────
var API_KEY        = "satyam123";
var LOG_SHEET      = "logs";
var FLAGS_SHEET    = "flags";
var FLAG_CELL      = "A1";
var OTP_CELL       = "B1";
var OTP_TIME_CELL  = "B2";
var MESSAGE_ID_CELL = "B3";
var DELETE_TIME_CELL = "B4";

var TELEGRAM_TOKEN = "8614619376:AAEhcyNtmJnFB6ylNAoAT03kNVC9qLGsp3U";
var TELEGRAM_CHAT  = "5471661264";

var MESSAGE_DELETE_TIME = 45 * 1000;  // 45 seconds
var OTP_VALID_TIME = 10 * 60 * 1000;  // 10 minutes
// ────────────────────────────────────────────────

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
    sh.getRange("A1").setValue("false");
    sh.getRange("B1").setValue("");
    sh.getRange("B2").setValue("");
    sh.getRange("B3").setValue("");
    sh.getRange("B4").setValue("");
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
  var sheet = getFlagsSheet();
  sheet.getRange(OTP_CELL).setValue(otp);
  sheet.getRange(OTP_TIME_CELL).setValue(Date.now());
  sheet.getRange(DELETE_TIME_CELL).setValue(Date.now() + MESSAGE_DELETE_TIME);
}

function getStoredOtp() {
  return String(getFlagsSheet().getRange(OTP_CELL).getValue());
}

function getOtpTimestamp() {
  return getFlagsSheet().getRange(OTP_TIME_CELL).getValue();
}

function getDeleteTime() {
  return getFlagsSheet().getRange(DELETE_TIME_CELL).getValue();
}

function clearOtp() {
  var sheet = getFlagsSheet();
  sheet.getRange(OTP_CELL).setValue("");
  sheet.getRange(OTP_TIME_CELL).setValue("");
  sheet.getRange(MESSAGE_ID_CELL).setValue("");
  sheet.getRange(DELETE_TIME_CELL).setValue("");
}

function saveMessageId(messageId) {
  getFlagsSheet().getRange(MESSAGE_ID_CELL).setValue(messageId);
}

function getMessageId() {
  return String(getFlagsSheet().getRange(MESSAGE_ID_CELL).getValue());
}

function appendLog(method, status, extra) {
  var ts = Utilities.formatDate(new Date(), Session.getScriptTimeZone(), "yyyy-MM-dd HH:mm:ss");
  getLogSheet().appendRow([ts, method, status, extra]);
}

function validateKey(key) {
  return key === API_KEY;
}

function deleteOtpMessage() {
  var messageId = getMessageId();
  if (messageId === "" || messageId === "undefined") {
    Logger.log("No message ID to delete");
    return false;
  }
  
  try {
    UrlFetchApp.fetch(
      "https://api.telegram.org/bot" + TELEGRAM_TOKEN + "/deleteMessage",
      {
        method:      "post",
        contentType: "application/json",
        payload:     JSON.stringify({
          chat_id:    TELEGRAM_CHAT,
          message_id: parseInt(messageId)
        }),
        muteHttpExceptions: true
      }
    );
    
    Logger.log("Message " + messageId + " deleted successfully");
    return true;
  } catch (e) {
    Logger.log("Error deleting message: " + e.toString());
    return false;
  }
}

function processRequest(params) {
  if (!validateKey(params.key)) {
    return { success: false, error: "Unauthorized" };
  }

  var action = params.action;

  // ── check_flag ──────────────────────────────
  if (action === "check_flag") {
    return { unlock_flag: getUnlockFlag() };
  }

  // ── request_unlock ──────────────────────────
  if (action === "request_unlock") {
    var method = params.method || "WEB";
    var otp = String(Math.floor(1000 + Math.random() * 9000));
    saveOtp(otp);

    try {
      var response = UrlFetchApp.fetch(
        "https://api.telegram.org/bot" + TELEGRAM_TOKEN + "/sendMessage",
        {
          method:      "post",
          contentType: "application/json",
          payload:     JSON.stringify({
            chat_id:    TELEGRAM_CHAT,
            text:       "<b>🔐 Smart Lock OTP</b>\n\n<code>" + otp + "</code>\n\n⏱️ <i>Message deletes in 45 seconds</i>",
            parse_mode: "HTML"
          }),
          muteHttpExceptions: true
        }
      );

      var result = JSON.parse(response.getContentText());
      
      if (result.ok && result.result && result.result.message_id) {
        var messageId = result.result.message_id;
        saveMessageId(messageId);
        Logger.log("OTP sent: " + otp + " | Message ID: " + messageId);
      }
    } catch (e) {
      Logger.log("Error sending Telegram: " + e.toString());
    }

    setUnlockFlag(false);
    appendLog(method, "PENDING", "OTP sent");
    return { success: true, message: "OTP sent to Telegram" };
  }

  // ── verify_otp ──────────────────────────────
  if (action === "verify_otp") {
    var method     = params.method || "WEB";
    var submitted  = String(params.otp || "").trim();
    var stored     = getStoredOtp();
    var otpTime    = getOtpTimestamp();
    var currentTime = Date.now();

    Logger.log("Verification: submitted=" + submitted + " | stored=" + stored);

    if (submitted.length < 4) {
      appendLog(method, "FAILED", "OTP too short");
      return { success: false, error: "Invalid OTP" };
    }

    if (submitted !== stored) {
      appendLog(method, "FAILED", "Wrong OTP");
      return { success: false, error: "Wrong OTP" };
    }

    // Check if OTP expired (10 minutes)
    if (otpTime && currentTime - otpTime > OTP_VALID_TIME) {
      Logger.log("OTP expired");
      clearOtp();
      appendLog(method, "FAILED", "OTP expired");
      return { success: false, error: "OTP expired" };
    }

    var timeUsedMs = currentTime - otpTime;
    
    setUnlockFlag(true);
    clearOtp();
    appendLog(method, "SUCCESS", "OTP verified");
    
    Logger.log("✓ Unlock flag set to TRUE");
    
    return { 
      success: true, 
      message: "OTP verified",
      timeUsedSeconds: Math.floor(timeUsedMs / 1000)
    };
  }

  // ── reset_flag ──────────────────────────────
  if (action === "reset_flag") {
    setUnlockFlag(false);
    clearOtp();
    Logger.log("✓ Unlock flag reset to FALSE");
    return { success: true, message: "Flag cleared" };
  }

  // ── log_event ───────────────────────────────
  if (action === "log_event") {
    var method = params.method || "UNKNOWN";
    var status = params.status || "UNKNOWN";
    var extra  = params.extra  || "";
    appendLog(method, status, extra);
    return { success: true, message: "Logged" };
  }

  // ── check_and_delete_message ──────────────────
  if (action === "check_and_delete") {
    var deleteTime = getDeleteTime();
    var currentTime = Date.now();
    
    Logger.log("Delete check: deleteTime=" + deleteTime + " | currentTime=" + currentTime);
    
    // Only delete if the scheduled time has arrived
    if (deleteTime > 0 && currentTime >= deleteTime) {
      Logger.log("Time to delete message");
      var deleted = deleteOtpMessage();
      if (deleted) {
        clearOtp();
        return { success: true, message: "Message deleted" };
      }
    }
    
    return { success: false, message: "Not yet time to delete" };
  }

  return { success: false, error: "Unknown action" };
}

// ── GET HANDLER ──────────────────────────────────
function doGet(e) {
  var result = processRequest(e.parameter);
  var output = ContentService.createTextOutput(JSON.stringify(result));
  output.setMimeType(ContentService.MimeType.JSON);
  return output;
}

// ── POST HANDLER ─────────────────────────────────
function doPost(e) {
  var params = {};
  
  try {
    if (e.postData && e.postData.contents) {
      params = JSON.parse(e.postData.contents);
    } else {
      params = e.parameter;
    }
  } catch (err) {
    var output = ContentService.createTextOutput(JSON.stringify({ success: false, error: "Invalid request" }));
    output.setMimeType(ContentService.MimeType.JSON);
    return output;
  }

  var result = processRequest(params);
  var output = ContentService.createTextOutput(JSON.stringify(result));
  output.setMimeType(ContentService.MimeType.JSON);
  return output;
}