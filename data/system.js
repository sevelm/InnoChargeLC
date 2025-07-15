// system.js  – Download-Link bleibt auf GitHub =================================

// global WebSocket variable
let socket = null;

/* ---------- initialize WebSocket ---------- */
function initWebSocket() {
  socket = new WebSocket('ws://' + location.hostname + ':81/');

  socket.onopen = () => {
    console.log('WS open');
    socket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'system' }));
  };

  socket.onmessage = ev => processMsg(ev.data);
  socket.onclose   = ev => console.log('WS close', ev);
  socket.onerror   = err => console.error('WS error', err);

  // unsubscribe when leaving the page
  window.addEventListener('beforeunload', () => {
    if (socket?.readyState === 1) {
      socket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'system' }));
      socket.close();
    }
  });
}

// -----------------------------------------------------------------------------
// fetch latest version strings from GitHub
const GITHUB_RAW = 'https://raw.githubusercontent.com/sevelm/InnoChargeLC/main';

async function loadLatest(txtFile, spanId) {
  try {
    const res = await fetch(`${GITHUB_RAW}/${txtFile}`);
    const ver = (await res.text()).split('\n')[0].trim();
    document.getElementById(spanId).textContent = ver;
  } catch (e) {
    console.warn('Version fetch failed', txtFile, e);
    document.getElementById(spanId).textContent = '—';
  }
}

// -----------------------------------------------------------------------------
// client‑side filename validation
function validateFile(inputEl, requiredPrefix) {
  const f = inputEl.files[0];
  if (!f) return false;                              // nothing selected
  if (!f.name.startsWith(requiredPrefix)) {
    alert(`Wrong file selected!\nExpected prefix: ${requiredPrefix}`);
    inputEl.value = '';                              // clear selection
    return false;
  }
  return true;
}

// -----------------------------------------------------------------------------
// handle incoming WebSocket frames
function processMsg(txt) {
  let j;
  try { j = JSON.parse(txt); } catch { return; }

  // current versions
  document.getElementById('fwMainVersion').textContent = j.otaMainVersion;
  document.getElementById('fwUiVersion').textContent   = j.otaUiVersion;

  // progress MAIN
  if (j.otaMainProgress !== undefined) {
    const box = document.getElementById('fwMainBox');
    box.style.display = j.otaMainProgress > 0 ? 'table' : 'none';
    document.getElementById('fwMainProgress').textContent =
        j.otaMainProgress + ' %';
  }

  // progress UI
  if (j.otaUiProgress !== undefined) {
    const box = document.getElementById('fwUiBox');
    box.style.display = j.otaUiProgress > 0 ? 'table' : 'none';
    document.getElementById('fwUiProgress').textContent =
        j.otaUiProgress + ' %';
  }

  // status codes
  if (j.otaMainCode !== undefined) {
    const el = document.getElementById('fwMainStatus');
    el.textContent = j.otaMainCode;
    el.style.color = j.otaMainCode > 0 ? 'green'
                   : j.otaMainCode == 0 ? 'orange' : 'red';
  }
  if (j.otaUiCode !== undefined) {
    const el = document.getElementById('fwUiStatus');
    el.textContent = j.otaUiCode;
    el.style.color = j.otaUiCode > 0 ? 'green'
                   : j.otaUiCode == 0 ? 'orange' : 'red';
  }

  // plain‑text messages
  if (j.otaMainMessage !== undefined)
    document.getElementById('fwMainMsg').textContent = j.otaMainMessage;
  if (j.otaUiMessage   !== undefined)
    document.getElementById('fwUiMsg').textContent   = j.otaUiMessage;

  if (j.otaMainCode === 1)
    alert('MAIN update OK – device will restart, reload the page.');
  if (j.otaMainCode && j.otaMainCode < 0)
    alert('MAIN OTA error: ' + (j.otaMainMessage || 'unknown'));

  if (j.otaUiCode === 1)
    alert('UI update OK – device will restart, reload the page.');
  if (j.otaUiCode && j.otaUiCode < 0)
    alert('UI OTA error: ' + (j.otaUiMessage || 'unknown'));
}

// -----------------------------------------------------------------------------
// upload without page reload – MAIN
async function handleFwMainForm(ev) {
  ev.preventDefault();
  const fileInput = document.getElementById('fwMainFile');
  if (!validateFile(fileInput, 'MAIN_IC_Fw_')) return;

  const file = fileInput.files[0];
  document.getElementById('fwMainProgress').textContent = '0 %';

  const fd = new FormData();
  fd.append('update', file, file.name);

  try {
    const res = await fetch('/uploadfw', { method: 'POST', body: fd });
    if (!res.ok) alert('Upload failed!');
  } catch (e) {
    alert('MAIN network error: ' + e);
  }
}

// upload without page reload – UI
async function handleFwUiForm(ev) {
  ev.preventDefault();
  const fileInput = document.getElementById('fwUiFile');
  if (!validateFile(fileInput, 'UI_IC_Fw_')) return;     // <- FIX

  const file = fileInput.files[0];
  document.getElementById('fwUiProgress').textContent = '0 %';

  const fd = new FormData();
  fd.append('update', file, file.name);

  try {
    const res = await fetch('/uploadui', { method: 'POST', body: fd });
    if (!res.ok) alert('Upload failed!');
  } catch (e) {
    alert('UI network error: ' + e);
  }
}

// -----------------------------------------------------------------------------
// DOM ready
document.addEventListener('DOMContentLoaded', () => {
  initWebSocket();

  loadLatest('VersionMain.txt', 'fwMainVersionLast');
  loadLatest('VersionUi.txt',   'fwUiVersionLast');

  // validate immediately on file selection
  document.getElementById('fwMainFile').addEventListener('change', () =>
      validateFile(document.getElementById('fwMainFile'), 'MAIN_IC_Fw_'));
  document.getElementById('fwUiFile').addEventListener('change', () =>
      validateFile(document.getElementById('fwUiFile'), 'UI_IC_Fw_'));

  document.getElementById('fwMainForm')
          .addEventListener('submit', handleFwMainForm);
  document.getElementById('fwUiForm')
          .addEventListener('submit', handleFwUiForm);
});