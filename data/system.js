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
  document.getElementById('fwUiVersion').textContent = j.otaUiVersion;

  /* ---------- Prozent MAIN ---------- */
  if (j.otaMainProgress !== undefined) {
    /* NEU: Tabelle ein- / aus­blenden */
    const box = document.getElementById('fwMainBox');
    if (j.otaMainProgress > 0) box.style.display = 'table';
    else                       box.style.display = 'none';

    document.getElementById('fwMainProgress').textContent =
        j.otaMainProgress + ' %';
  }
  /* ---------- Prozent UI ---------- */
  if (j.otaUiProgress !== undefined) {
    /* NEU: Tabelle ein- / aus­blenden */
    const box = document.getElementById('fwUiBox');
    if (j.otaUiProgress > 0) box.style.display = 'table';
    else                       box.style.display = 'none';

    document.getElementById('fwUiProgress').textContent =
        j.otaUiProgress + ' %';
  }




  /* ---------- Code (busy / ok / error) MAIN ---------- */
  if (j.otaMainCode !== undefined) {
    const statusEl = document.getElementById('fwMainStatus');
    statusEl.textContent = j.otaMainCode;
    statusEl.style.color =
        (j.otaMainCode > 0)  ? 'green'  :
        (j.otaMainCode == 0) ? 'orange' : 'red';
  }
  /* ---------- Code (busy / ok / error) UI ---------- */
  if (j.otaUiCode !== undefined) {
    const statusEl = document.getElementById('fwUiStatus');
    statusEl.textContent = j.otaUiCode;
    statusEl.style.color =
        (j.otaUiCode > 0)  ? 'green'  :
        (j.otaUiCode == 0) ? 'orange' : 'red';
  }



  /* ---------- Klartext-Message MAIN ---------- */
  if (j.otaMainMessage !== undefined) {
    document.getElementById('fwMainMsg').textContent = j.otaMainMessage;
  }
  if (j.otaMainCode === 1)
       alert('Update-MAIN OK – device will restart… reload page');
  if (j.otaMainCode && j.otaMainCode < 0)
       alert('OTA-MAIN error: ' + (j.otaMainMessage || 'unknown'));
  /* ---------- Klartext-Message UI ---------- */
  if (j.otaUiMessage !== undefined) {
    document.getElementById('fwUiMsg').textContent = j.otaUiMessage;
  }
  if (j.otaUiCode === 1)
       alert('Update-Ui OK – device will restart… reload page');
  if (j.otaUiCode && j.otaUiCode < 0)
       alert('OTA-Ui error: ' + (j.otaUiMessage || 'unknown'));

}


/* ---------- Upload ohne Seiten-Reload MAIN ---------- */
async function handleFwMainForm(ev) {
  ev.preventDefault();                         // kein Reload!
  const file = document.getElementById('fwMainFile').files[0];
  if (!file) { alert('No BIN selected'); return; }

  document.getElementById('fwMainProgress').textContent = '0 %';

  const fd = new FormData();
  fd.append('update', file, file.name);        // Muss "update" heißen!

  try {
    const res = await fetch('/uploadfw', { method:'POST', body:fd });
    console.log('MAIN-response:', await res.text());
    if (!res.ok) alert('Upload failed!');
  } catch (e) {
    alert('MAIN-Network error: ' + e);
  }
}

/* ---------- Upload ohne Seiten-Reload Ui ---------- */
async function handleFwUiForm(ev) {
  ev.preventDefault();                         // kein Reload!
  const file = document.getElementById('fwUiFile').files[0];
  if (!file) { alert('No BIN selected'); return; }

  document.getElementById('fwUiProgress').textContent = '0 %';

  const fd = new FormData();
  fd.append('update', file, file.name);        // Muss "update" heißen!

  try {
    const res = await fetch('/uploadui', { method:'POST', body:fd });
    console.log('UI-response:', await res.text());
    if (!res.ok) alert('Upload failed!');
  } catch (e) {
    alert('UI-Network error: ' + e);
  }
}




/* ---------- DOM ready ---------- */
document.addEventListener('DOMContentLoaded', () => {
  initWebSocket();
  document.getElementById('fwMainForm')
          .addEventListener('submit', handleFwMainForm);
  document.getElementById('fwUiForm')
          .addEventListener('submit', handleFwUiForm);
});
