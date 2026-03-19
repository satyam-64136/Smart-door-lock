# Satyam's Store — Candy & Chocolate POS

<div align="center">

### 🔗 [Live Access → satyam-64136.github.io/My-store](https://satyam-64136.github.io/My-store/)

![Built with HTML](https://img.shields.io/badge/HTML-5-orange?style=flat-square)
![Vanilla JS](https://img.shields.io/badge/JavaScript-Vanilla-yellow?style=flat-square)
![No Build Tools](https://img.shields.io/badge/No%20Build%20Tools-✓-brightgreen?style=flat-square)
![Mobile Ready](https://img.shields.io/badge/Mobile-Responsive-blue?style=flat-square)

A sleek, dark-themed **Point of Sale system** for a candy & chocolate shop — runs entirely in the browser with no installations or build steps.

</div>

---

## ✨ Features

| Feature | Details |
|--------|---------|
| 🛍️ **Product Grid** | Card-based product browser with live search |
| 🛒 **Cart System** | Add/remove items with quantity controls, persistent across pages |
| 💳 **UPI Payment** | Deep-link opens any UPI app (PhonePe, GPay, Paytm, BHIM) |
| 📷 **QR Code** | Dynamic QR generated from the UPI payment link |
| ✅ **Payment Confirmation** | "Pending payment" detection when user returns from UPI app |
| 🔐 **Admin Panel** | Password-protected panel to add/delete products |
| 📊 **Sales History** | View daily sales with revenue summary, filterable by date |
| ☁️ **Google Sheets Backend** | All data (products, sales, stock) synced via Google Apps Script |
| 📱 **Mobile Responsive** | Works seamlessly on phones — no hidden buttons, smooth scrolling |

---

## 📁 File Structure

```
├── index.html            # Main store / product grid
├── payment.html          # Cart summary + UPI payment + confirmation
├── admin.html            # Admin panel (add/delete products)
├── admin-sales.html      # Sales history viewer
├── shared.js             # Shared utilities (API calls, helpers)
├── style.css             # Full dark-futuristic design system
└── apps-script-backend.js  # Google Apps Script (paste into Code.gs)
```

---

## 🚀 Setup

### 1. Google Sheets Backend

1. Create a new **Google Sheet**
2. Open **Extensions → Apps Script**
3. Paste the contents of `apps-script-backend.js` into `Code.gs`
4. Run `setupSheets()` once manually (creates the Products sheet)
5. Deploy → **New Deployment → Web App**
   - Execute as: **Me**
   - Who has access: **Anyone**
6. Copy the Web App URL

### 2. Connect to Frontend

Open `shared.js` and update the GAS URL:

```js
const GAS_URL = 'https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID/exec';
```

### 3. UPI ID

In `payment.html`, update your UPI ID:

```js
const UPI_ID = 'yourname@upi';
```

### 4. Admin Password

In `admin.html`, update the password:

```js
const ADMIN_PASSWORD = 'your_password';
```

---

## 💳 UPI Payment Flow

```
Customer adds items → Proceed to Payment
        ↓
  UPI deep-link opens payment app
        ↓
  Customer pays and returns to browser
        ↓
  "Pending Payment Detected" banner appears
        ↓
  Customer taps "Yes, I've Paid" → Sale recorded + stock deducted
```

> UPI apps don't reliably redirect back automatically, so a **manual confirmation fallback** is built in. Cart data is saved to `localStorage` and survives the UPI app redirect.

---

## 🛠️ Tech Stack

- **HTML5 + Vanilla JS** — no frameworks, no build tools
- **CSS** — custom dark futuristic design system
- **Lucide Icons** via CDN
- **Google Fonts** — Syne, DM Sans, JetBrains Mono
- **Google Apps Script** — serverless backend on Google Sheets
- **QR Server API** — dynamic UPI QR code generation

---

## 📱 Mobile

- Nav bar wraps gracefully on small screens (no hidden buttons)
- Bottom cart bar always visible with "Proceed to Payment"
- Touch-friendly quantity controls
- Smooth scrolling throughout

---

<div align="center">
Made with ❤️ by <a href="https://github.com/satyam-64136">Satyam</a>
</div>
