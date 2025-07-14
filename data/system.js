// system.js

// globale WS-Variable
let socket = null;

/* ---------- WebSocket initialisieren ---------- */
function initWebSocket() {
  socket = new WebSocket('ws://' + location.hostname + ':81/');

  socket.onopen   = () => {
    console.log('WS open');
    socket.send(JSON.stringify({ action:'subscribeUpdates', page:'system' }));
  };

  socket.onmessage = ev => processMsg(ev.data);
  socket.onclose   = ev => console.log('WS close', ev);
  socket.onerror   = err => console.error('WS error', err);

  // Beim Verlassen der Seite wieder abmelden
  window.addEventListener('beforeunload', () => {
    if (socket?.readyState === 1) {
      socket.send(JSON.stringify({ action:'unsubscribeUpdates', page:'system' }));
      socket.close();
    }
  });
}

/* ---------- eingehende JSON-Pakete ---------- */
function processMsg(txt) {
  console.log('WS-Frame:', txt); 
  let j;
  try { j = JSON.parse(txt); } catch { return; }

  document.getElementById('fwMainVersion').textContent = j.otaMainVersion;

  /* ---------- Prozent ---------- */
  if (j.otaMainProgress !== undefined) {
    /* NEU: Tabelle ein- / aus­blenden */
    const box = document.getElementById('fwMainBox');
    if (j.otaMainProgress > 0) box.style.display = 'table';
    else                       box.style.display = 'none';

    document.getElementById('fwMainProgress').textContent =
        j.otaMainProgress + ' %';
  }

  /* ---------- Code (busy / ok / error) ---------- */
  if (j.otaMainCode !== undefined) {
    const statusEl = document.getElementById('fwMainStatus');
    statusEl.textContent = j.otaMainCode;
    statusEl.style.color =
        (j.otaMainCode > 0)  ? 'green'  :
        (j.otaMainCode == 0) ? 'orange' : 'red';
  }

  /* ---------- Klartext-Message ---------- */
  if (j.otaMainMessage !== undefined) {
    document.getElementById('fwMainMsg').textContent = j.otaMainMessage;
  }

  if (j.otaMainCode === 1)
       alert('Update OK – device will restart… reload page');
  if (j.otaMainCode && j.otaMainCode < 0)
       alert('OTA error: ' + (j.otaMainMessage || 'unknown'));
}


/* ---------- Upload ohne Seiten-Reload ---------- */
async function handleFwForm(ev) {
  ev.preventDefault();                         // kein Reload!
  const file = document.getElementById('fwFile').files[0];
  if (!file) { alert('No BIN selected'); return; }

  document.getElementById('fwMainProgress').textContent = '0 %';

  const fd = new FormData();
  fd.append('update', file, file.name);        // Muss "update" heißen!

  try {
    const res = await fetch('/uploadfw', { method:'POST', body:fd });
    console.log('Server response:', await res.text());
    if (!res.ok) alert('Upload failed!');
  } catch (e) {
    alert('Network error: ' + e);
  }
}

/* ---------- DOM ready ---------- */
document.addEventListener('DOMContentLoaded', () => {
  initWebSocket();
  document.getElementById('fwForm')
          .addEventListener('submit', handleFwForm);
});
