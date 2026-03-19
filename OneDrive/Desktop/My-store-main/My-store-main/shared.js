/* ═══════════════════════════════════════════
   Satyam's Store — shared.js
   Common API helpers used by all pages
   ═══════════════════════════════════════════ */

const API_URL = 'https://script.google.com/macros/s/AKfycbzBg1w08jyfv2DJcYGE82hxouxAILztBNHlrjNU62OcC6mmxSOnIMMRHemt2mzq5Edy/exec';

async function gasGet(action, params) {
  let url = `${API_URL}?action=${encodeURIComponent(action)}`;
  if (params) {
    Object.entries(params).forEach(([k, v]) => {
      url += `&${encodeURIComponent(k)}=${encodeURIComponent(v)}`;
    });
  }
  const res = await fetch(url, { redirect: 'follow' });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  const text = await res.text();
  try { return JSON.parse(text); }
  catch { throw new Error('Non-JSON: ' + text.slice(0, 100)); }
}

async function gasPost(payload) {
  return fetch(API_URL, {
    method: 'POST',
    mode: 'no-cors',
    headers: { 'Content-Type': 'text/plain;charset=utf-8' },
    body: JSON.stringify(payload),
  });
}

function esc(str) {
  return String(str ?? '')
    .replace(/&/g,'&amp;').replace(/</g,'&lt;')
    .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function delay(ms) { return new Promise(r => setTimeout(r, ms)); }

let _tt;
function toast(msg, dur = 3000) {
  const el = document.getElementById('toast');
  if (!el) return;
  el.textContent = msg;
  el.classList.remove('hidden');
  clearTimeout(_tt);
  _tt = setTimeout(() => el.classList.add('hidden'), dur);
}

function hideLoader() {
  const el = document.getElementById('page-loader');
  if (!el) return;
  el.classList.add('fade-out');
  setTimeout(() => { el.style.display = 'none'; }, 520);
}

function cleanSheetVal(val) {
  if (!val) return '';
  const s = String(val);
  if (s.includes('T') && s.includes('Z')) {
    const t = new Date(s);
    if (!isNaN(t)) return t.toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit' });
  }
  return s;
}

function parseProducts(raw) {
  return raw
    .map(row => ({
      id:      String(row[0] ?? '').trim(),
      name:    String(row[1] ?? '').trim(),
      price:   parseFloat(row[2])   || 0,
      stock:   parseInt(row[3], 10) || 0,
      image:   String(row[4] ?? '').trim(),
      barcode: String(row[5] ?? '').trim(),
    }))
    .filter(p => p.id && p.name);
}

function placeholderSVG() {
  return `<svg viewBox="0 0 24 24" fill="none" stroke="#22d3ee" stroke-width="1.2" opacity=".25">
    <rect x="2" y="7" width="20" height="14" rx="2"/>
    <circle cx="12" cy="14" r="3"/>
    <path d="M2 10h20"/>
  </svg>`;
}

// Hamster loader HTML
function hamsterHTML() {
  return `
  <div id="page-loader">
    <div class="loader-inner">
      <div aria-label="Loading" role="img" class="wheel-and-hamster">
        <div class="wheel"></div>
        <div class="hamster">
          <div class="hamster__body">
            <div class="hamster__head">
              <div class="hamster__ear"></div>
              <div class="hamster__eye"></div>
              <div class="hamster__nose"></div>
            </div>
            <div class="hamster__limb hamster__limb--fr"></div>
            <div class="hamster__limb hamster__limb--fl"></div>
            <div class="hamster__limb hamster__limb--br"></div>
            <div class="hamster__limb hamster__limb--bl"></div>
            <div class="hamster__tail"></div>
          </div>
        </div>
        <div class="spoke"></div>
      </div>
      <p class="loader-label">Loading<span class="loader-dots"></span></p>
    </div>
  </div>`;
}
