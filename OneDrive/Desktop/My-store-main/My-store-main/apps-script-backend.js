// ═══════════════════════════════════════════════════════
//  ChocoShop POS — Google Apps Script Backend
//  Paste this into Code.gs, then:
//  1. Run setupSheets() once manually
//  2. Deploy → New Deployment → Web App
//     Execute as: Me | Who has access: Anyone
// ═══════════════════════════════════════════════════════

var PRODUCTS_SHEET = "Products";

// ── GET: load products ──────────────────────────────────
function doGet(e) {
  var action = e.parameter.action;
  var ss = SpreadsheetApp.getActiveSpreadsheet();

  if (action === "getProducts") {
    var sheet = ss.getSheetByName(PRODUCTS_SHEET);
    if (!sheet) return jsonOut([]);

    var values = sheet.getDataRange().getValues();
    values.shift(); // remove header row

    var rows = values.filter(function(row) {
      return row[0] !== "" && row[1] !== "";
    });

    return jsonOut(rows);
  }

  // ── getSales ──
  // Returns today's sales sheet rows — force all cells to strings
  if (action === "getSales") {
    var today = new Date().toLocaleDateString("en-IN");
    var sheet = ss.getSheetByName(today);
    if (!sheet) return jsonOut([]);
    // getDisplayValues returns exactly what the cell shows (no ISO conversion)
    var values = sheet.getDataRange().getDisplayValues();
    values.shift(); // remove header
    return jsonOut(values);
  }

  // ── getSales (today) ──
  if (action === "getSales") {
    var today = new Date().toLocaleDateString("en-IN");
    var sheet = ss.getSheetByName(today);
    if (!sheet) return jsonOut([]);
    var values = sheet.getDataRange().getDisplayValues();
    values.shift();
    return jsonOut(values);
  }

  // ── getSalesByDate ──
  if (action === "getSalesByDate") {
    var date = e.parameter.date || new Date().toLocaleDateString("en-IN");
    var sheet = ss.getSheetByName(date);
    if (!sheet) return jsonOut([]);
    var values = sheet.getDataRange().getDisplayValues();
    values.shift();
    return jsonOut(values);
  }

  return jsonOut({ error: "Unknown action: " + action });
}

// ── POST: add product / delete / record sale ────────────
function doPost(e) {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var data = {};

  try {
    data = JSON.parse(e.postData.contents);
  } catch (err) {
    return okOut("Error: bad JSON");
  }

  var action = data.action;

  // ── addProduct ──
  if (action === "addProduct") {
    var sheet = ss.getSheetByName(PRODUCTS_SHEET);
    if (!sheet) {
      sheet = ss.insertSheet(PRODUCTS_SHEET);
      sheet.appendRow(["ID", "Name", "Price", "Stock", "Image", "Barcode"]);
      sheet.setFrozenRows(1);
    }
    sheet.appendRow([
      Date.now(),
      data.name    || "",
      parseFloat(data.price)   || 0,
      parseInt(data.stock, 10) || 0,
      data.image   || "",
      data.barcode || ""
    ]);
    return okOut("Product added");
  }

  // ── deleteProduct ──
  if (action === "deleteProduct") {
    var sheet = ss.getSheetByName(PRODUCTS_SHEET);
    if (!sheet) return okOut("Products sheet not found");

    var rows = sheet.getDataRange().getValues();
    for (var i = 1; i < rows.length; i++) {
      if (String(rows[i][0]) === String(data.id)) {
        sheet.deleteRow(i + 1);
        return okOut("Deleted");
      }
    }
    return okOut("ID not found");
  }

  // ── recordSale ──
  // Records the sale AND deducts stock in one atomic operation
  if (action === "recordSale") {
    var date     = data.date  || "";
    var time     = data.time  || "";
    var items    = data.items || "";
    var total    = data.total || "";
    var products = data.products || []; // [{id, qty}]

    // 1. Append sale to today's date-named sheet
    var sheetName = date;
    var salesSheet = ss.getSheetByName(sheetName);
    if (!salesSheet) {
      salesSheet = ss.insertSheet(sheetName);
      salesSheet.appendRow(["Date", "Time", "Items", "Total"]);
      salesSheet.setFrozenRows(1);
    }
    salesSheet.appendRow([date, time, items, total]);

    // 2. Deduct stock from Products sheet
    if (products.length > 0) {
      var productSheet = ss.getSheetByName(PRODUCTS_SHEET);
      if (productSheet) {
        var rows = productSheet.getDataRange().getValues();
        products.forEach(function(p) {
          for (var i = 1; i < rows.length; i++) {
            if (String(rows[i][0]) === String(p.id)) {
              var currentStock = parseInt(rows[i][3], 10) || 0;
              var newStock = Math.max(0, currentStock - parseInt(p.qty, 10));
              productSheet.getRange(i + 1, 4).setValue(newStock);
              break;
            }
          }
        });
      }
    }

    return okOut("Sale recorded");
  }

  return okOut("Unknown action: " + action);
}

// ── Helpers ─────────────────────────────────────────────
function jsonOut(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function okOut(msg) {
  return ContentService
    .createTextOutput(msg || "OK")
    .setMimeType(ContentService.MimeType.TEXT);
}

// ── Run this ONCE manually before deploying ─────────────
function setupSheets() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();

  if (!ss.getSheetByName(PRODUCTS_SHEET)) {
    var p = ss.insertSheet(PRODUCTS_SHEET);
    p.appendRow(["ID", "Name", "Price", "Stock", "Image", "Barcode"]);
    p.setFrozenRows(1);
    Logger.log("Products sheet created.");
  } else {
    Logger.log("Products sheet already exists.");
  }

  Logger.log("Setup complete!");
}
