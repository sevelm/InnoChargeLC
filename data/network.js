// network.js  – sequential version
// ------------------------------------------------------------
// 1) Ethernet‑Infos holen → 2) WiFi‑Infos holen → 3) ggf. WiFi‑Scan
// ------------------------------------------------------------

let currentSSID = '';
let Socket;           // WebSocket instance
let rescueModeActive = false;
let scanInProgress = false;   // verhindert Parallel‑Scans

/* ------------------------------------------------------------------
   WebSocket initialisation
------------------------------------------------------------------ */
let wsReconnectTimer = null;

function initWebSocket() {
  // bestehende Verbindung ggf. schließen
  try { if (Socket && Socket.readyState === WebSocket.OPEN) Socket.close(); } catch {}

  Socket = new WebSocket('ws://' + window.location.hostname + ':81/');

  Socket.onopen = () => {
    console.log('[WS] opened');
    // One-Shot: einmalig ETH + WiFi anfordern
    requestNetworkInfo();
  };

  Socket.onmessage = (e) => {
    // One-Shot Antwort verarbeiten
    let d;
    try {
      d = JSON.parse(e.data);
    } catch (err) {
      console.warn('[WS] non-JSON or parse error:', err, e.data);
      return;
    }

    // Netzwerkpaket erkannt? → UI füllen
    if (d.eth_mac || d.eth_ip || d.wifi_ssid || d.wifi_ip) {
      rescueModeActive = d.rescueMode === true;
      if (typeof setEthernetInfo === 'function') setEthernetInfo(d);
      if (typeof setWifiInfo === 'function') setWifiInfo(d);

      // WiFi-Scan-Bereich passend ein-/ausblenden (defensiv)
      const rescanBtn  = document.getElementById('wifi_rescan_btn');
      const scanMsg    = document.getElementById('wifi_scan_message');
      const scanResult = document.getElementById('wifi_scan_results');

      if (d.wifi_enable) {
        if (typeof showWifiScanResults === 'function') showWifiScanResults();
        if (rescanBtn)  rescanBtn.style.display = 'inline-block';
        if (scanMsg)    scanMsg.style.display   = 'none';
        if (scanResult) scanResult.innerHTML    = '';
      } else {
        if (typeof hideWifiScanResults === 'function') hideWifiScanResults();
        if (rescanBtn)  rescanBtn.style.display = 'none';
      }
      return; // erledigt
    }

    // …hier ggf. andere WS-Nachrichten verarbeiten …
     console.log('[WS] msg:', d);
  };

  Socket.onclose = (e) => {
    console.log('[WS] closed:', e.code, e.reason || '');
    // sanfter Reconnect mit kleinem Delay
    if (!wsReconnectTimer) {
      wsReconnectTimer = setTimeout(() => {
        wsReconnectTimer = null;
        initWebSocket();
      }, 800);
    }
  };

  Socket.onerror = (e) => {
    console.log('[WS] error:', e);
    // Fehler -> Verbindung schließen, onclose triggert Reconnect
    try { Socket.close(); } catch {}
  };

  // sauber schließen, wenn Seite verlassen wird
  window.onbeforeunload = () => {
    try { if (Socket) Socket.close(); } catch {}
  };
}

// One-Shot Anfrage senden (bei Bedarf auch manuell aufrufbar)
function requestNetworkInfo() {
  if (Socket && Socket.readyState === WebSocket.OPEN) {
    Socket.send(JSON.stringify({ action: 'requestNetworkInfo' }));
  } else {
    // falls noch im Aufbau: kurz später erneut versuchen
    setTimeout(requestNetworkInfo, 250);
  }
}

/* ------------------------------------------------------------------
   Sequential initialisation flow
------------------------------------------------------------------ */
async function initialiseNetworkPage() {
  // Keine HTTP-GETs mehr auf /get_* – die Daten kommen per WS One-Shot
  try {
    // Optional: UI kurz „leer“/Loading setzen
    setEthernetInfo({});   // leert die Felder
    setWifiInfo({});       // leert die Felder
    hideWifiScanResults(); // bis Antwort da ist
  } catch (err) {
    console.error('[INIT] failed:', err);
  }
}

/* ------------------------------------------------------------------
   WiFi scan helpers (unchanged + "no parallel"‑Guard)
------------------------------------------------------------------ */
function startWifiScan() {
  if (scanInProgress) return;   // bereits am Laufen?
  scanInProgress = true;

  showWifiScanResults();
  const msg = document.getElementById('wifi_scan_message');
  msg.style.display = 'block';
  msg.innerText     = 'Scanning...';
  document.getElementById('wifi_rescan_btn').style.display = 'none';
  document.getElementById('wifi_scan_results').innerHTML   = '';

  fetch('/wifi_scan')
    .then(r => r.json())
    .then(data => {
      console.log('[WiFi] scan data:', data);
      displayWifiScanResults(data);
      populateWifiNetworks(data);
      document.getElementById('wifi_rescan_btn').style.display = 'inline-block';
    })
    .catch(err => {
      console.error('[WiFi] scan error:', err);
      document.getElementById('wifi_scan_results').innerHTML = 'Error scanning WiFi networks.';
    })
    .finally(() => {
      msg.style.display = 'none';
      scanInProgress    = false;
    });
}

/* ------------------------------------------------------------------
   Helper‑Funktionen (unverändert aus Original)
   – setEthernetInfo, setWifiInfo, populateWifiNetworks,
     hideWifiScanResults, showWifiScanResults, displayWifiScanResults,
     getSignalQuality, saveETHNetworkSettings, saveWifiNetworkSettings,
     toggleETHInputFields, toggleWifiInputFields, toggleWiFiInputFieldsForDHCP
------------------------------------------------------------------ */
// ▼▼▼  (kopiert aus Original, keine Logik‑Änderungen)  ▼▼▼

function setEthernetInfo(data) {
  document.getElementById('eth_mac-address').value     = data.eth_mac || '';
  document.getElementById('eth_ip-address').value      = data.eth_ip || '';
  document.getElementById('eth_subnet-mask').value     = data.eth_netmask || '';
  document.getElementById('eth_gateway').value         = data.eth_gateway || '';
  document.getElementById('eth_dns1').value            = data.eth_dns1 || '';
  document.getElementById('eth_dns2').value            = data.eth_dns2 || '';

  const ethDhcpEnabled = data.eth_static;
  document.getElementById('eth_dhcp-toggle').checked = ethDhcpEnabled;
  toggleETHInputFields(!ethDhcpEnabled);

  const wifiEnabled = data.wifi_enable;
  document.getElementById('wifi_enable').checked = wifiEnabled;
  toggleWifiInputFields(wifiEnabled);

  console.log('[ETH]', {
    mac:  document.getElementById('eth_mac-address').value,
    ip:   document.getElementById('eth_ip-address').value,
    mask: document.getElementById('eth_subnet-mask').value,
    gw:   document.getElementById('eth_gateway').value,
    dns1: document.getElementById('eth_dns1').value,
    dns2: document.getElementById('eth_dns2').value,
    dhcp: document.getElementById('eth_dhcp-toggle').checked
  });


}

function setWifiInfo(data) {
  const wifiEnabled = data.wifi_enable;
  document.getElementById('wifi_enable').checked = wifiEnabled;
  toggleWifiInputFields(wifiEnabled);

  // SSID immer vorbefüllen, wenn geliefert
  if (typeof data.wifi_ssid === 'string') {
    document.getElementById('wifi_ssid').value = data.wifi_ssid;
    // Merk dir die aktuelle SSID für Dropdown-Markierung
    currentSSID = data.wifi_ssid;
    // ensure the select shows the current SSID even without a scan
    const sel = document.getElementById('wifi_ssid');
    if (sel) {
      let found = false;
      for (const opt of sel.options) {
        if (opt.value === currentSSID) { found = true; break; }
      }
      if (!found && currentSSID) {
        // insert a preselected option at top
        const opt = new Option(currentSSID + ' (Current)', currentSSID, true, true);
        sel.add(opt, 0);
      }
      sel.value = currentSSID || '';
    }
  }

  // Passwort NUR setzen, wenn explizit geliefert (nicht auf '' leeren!)
  if (typeof data.wifi_pwd === 'string') {
    document.getElementById('wifi_password').value = data.wifi_pwd;
  }

  document.getElementById('wifi_connected').innerText       = data.wifi_connected ? 'Connected' : 'Disconnected';
  document.getElementById('wifi_ssid_selected').innerText   = data.wifi_ssid || '';
  document.getElementById('wifi_mac-address').value         = data.wifi_mac || '';
  document.getElementById('wifi_ip-address').value          = data.wifi_ip || '';
  document.getElementById('wifi_subnet-mask').value         = data.wifi_netmask || '';
  document.getElementById('wifi_gateway').value             = data.wifi_gateway || '';
  document.getElementById('wifi_dns1').value                = data.wifi_dns1 || '';
  document.getElementById('wifi_dns2').value                = data.wifi_dns2 || '';

  const wifiDhcpEnabled = data.wifi_static;
  document.getElementById('wifi_dhcp-toggle').checked = wifiDhcpEnabled;
  toggleWiFiInputFieldsForDHCP(!wifiDhcpEnabled);

  // --- Signalqualität & RSSI nur anzeigen, wenn verbunden und gültig ---
  const elSignal  = document.getElementById('wifi_signal');
  const elRssi    = document.getElementById('wifi_rssi');
  const connected = !!data.wifi_connected;
  const hasRssi   = typeof data.wifi_rssi === 'number' && data.wifi_rssi !== -127;

  if (connected && hasRssi) {
    const text = (typeof data.wifi_quality === 'number')
      ? `${getSignalQuality(data.wifi_rssi)} (${data.wifi_quality}%)`
      : getSignalQuality(data.wifi_rssi);
    if (elSignal) elSignal.textContent = text;
    if (elRssi)   elRssi.textContent   = `${data.wifi_rssi} dBm`;
  } else {
    if (elSignal) elSignal.textContent = '';
    if (elRssi)   elRssi.textContent   = '';
  }

  console.log('[WIFI]', {
    enabled:  document.getElementById('wifi_enable').checked,
    ssid:     document.getElementById('wifi_ssid').value,
    pwd:      document.getElementById('wifi_password').value,
    state:    document.getElementById('wifi_connected').innerText,
    mac:      document.getElementById('wifi_mac-address').value,
    ip:       document.getElementById('wifi_ip-address').value,
    mask:     document.getElementById('wifi_subnet-mask').value,
    gw:       document.getElementById('wifi_gateway').value,
    dns1:     document.getElementById('wifi_dns1').value,
    dns2:     document.getElementById('wifi_dns2').value,
    dhcp:     document.getElementById('wifi_dhcp-toggle').checked,
    signal:   (document.getElementById('wifi_signal')||{}).textContent,
    rssi:     (document.getElementById('wifi_rssi')||{}).textContent
  });

}


function populateWifiNetworks(data) {
  const sel = document.getElementById('wifi_ssid');
  sel.innerHTML = '';
  let exists = false;

  if (data.networks && data.networks.length) {
    data.networks.forEach(n => {
      const opt = document.createElement('option');
      opt.value = opt.text = n.name;
      if (n.name === currentSSID) { opt.selected = true; exists = true; }
      sel.appendChild(opt);
    });
  }
  if (!exists && currentSSID) {
    const opt = document.createElement('option');
    opt.value = currentSSID;
    opt.text  = currentSSID + ' (Currently Connected)';
    opt.selected = true;
    sel.appendChild(opt);
  }
  if (!sel.options.length) {
    const opt = document.createElement('option');
    opt.value = '';
    opt.text  = 'No networks found';
    sel.appendChild(opt);
  }
}

function hideWifiScanResults() {
  document.getElementById('wifi_scan_results').innerHTML = '';
  document.getElementById('wifi_rescan_btn').style.display = 'none';
  document.getElementById('wifi_scan_message').style.display = 'none';
  document.getElementById('wifi-options-results').style.display = 'none';
}
function showWifiScanResults() {
  document.getElementById('wifi-options-results').style.display = 'block';
}
function displayWifiScanResults(data) {
  const c = document.getElementById('wifi_scan_results');
  if (!c) return;
  c.innerHTML = '';
  if (!(data.networks && data.networks.length)) {
    c.innerHTML = 'No WiFi networks found.';
    return;
  }
  const t = document.createElement('table');
  t.className = 'wifi-table';
  const hdr = t.insertRow();
  ['Name', 'Signal', 'Encryption'].forEach(h => {
    const th = document.createElement('th');
    th.textContent = h; hdr.appendChild(th);
  });
  data.networks.forEach(n => {
    const r = t.insertRow();
    r.insertCell().textContent = n.name;
    r.insertCell().textContent = getSignalQuality(n.signal);
    r.insertCell().textContent = n.encryption;
  });
  c.appendChild(t);
}
function getSignalQuality(rssi) {
  if (rssi >= -50) return 'Excellent';
  if (rssi >= -60) return 'Good';
  if (rssi >= -70) return 'Fair';
  return 'Weak';
}

function showNetworkSavePopup() {
  const text = document.getElementById('confirmation-popup-text');
  if (text) {
    text.textContent = rescueModeActive
      ? 'The system will restart. Turn DIP Switch 2 OFF before rebooting, otherwise rescue mode will start again. Do you want to proceed?'
      : 'The system will restart. Do you want to proceed?';
  }
  document.getElementById('confirmation-popup').style.display = 'flex';
}

  // Function to save Ethernet network settings
  function saveETHNetworkSettings() {
    var msg = {
      action: 'resetTCP',
      setEthStatic: document.getElementById('eth_dhcp-toggle').checked,
      setEthIpAdr: document.getElementById('eth_ip-address').value.split('.').map(Number),
      setEthNetmask: document.getElementById('eth_subnet-mask').value.split('.').map(Number),
      setEthGw: document.getElementById('eth_gateway').value.split('.').map(Number),
      setEthDns1: document.getElementById('eth_dns1').value.split('.').map(Number),
      setEthDns2: document.getElementById('eth_dns2').value.split('.').map(Number)
    };

    console.log('Sending Ethernet settings to server:', JSON.stringify(msg));

    if (Socket.readyState === WebSocket.OPEN) {
      Socket.send(JSON.stringify(msg));
    } else {
      console.error('WebSocket is not open. ReadyState: ' + Socket.readyState);
    }
  }

  // Function to save WiFi network settings
  function saveWifiNetworkSettings() {
    var msg = {
      setWifiEnable: document.getElementById('wifi_enable').checked,
      setWifiStatic: document.getElementById('wifi_dhcp-toggle').checked,
      setWifiSSID: document.getElementById('wifi_ssid').value,
      setWifiPassword: document.getElementById('wifi_password').value,
      setWifiIpAdr: document.getElementById('wifi_ip-address').value.split('.').map(Number),
      setWifiNetmask: document.getElementById('wifi_subnet-mask').value.split('.').map(Number),
      setWifiGw: document.getElementById('wifi_gateway').value.split('.').map(Number),
      setWifiDns1: document.getElementById('wifi_dns1').value.split('.').map(Number),
      setWifiDns2: document.getElementById('wifi_dns2').value.split('.').map(Number)
    };
    console.log('Sending WiFi settings to server:', JSON.stringify(msg));
    if (Socket.readyState === WebSocket.OPEN) {
      Socket.send(JSON.stringify(msg));
    } else {
      console.error('WebSocket is not open. ReadyState: ' + Socket.readyState);
    }
  }

  // Function to toggle Ethernet input fields based on DHCP setting
  function toggleETHInputFields(enable) {
    var ipFields = document.querySelectorAll('#eth_ip-address, #eth_subnet-mask, #eth_gateway, #eth_dns1, #eth_dns2');
    ipFields.forEach(function(field) {
      field.disabled = enable;
    });
  }

  // Function to toggle WiFi input fields based on WiFi enable toggle
  function toggleWifiInputFields(enable) {
    var wifiFields = document.getElementById('wifi_input_fields');
    if (wifiFields) {
      wifiFields.style.display = enable ? 'table' : 'none';
    }
  }

  // Function to toggle WiFi IP input fields based on DHCP setting
  function toggleWiFiInputFieldsForDHCP(enable) {
    var ipFields = document.querySelectorAll('#wifi_ip-address, #wifi_subnet-mask, #wifi_gateway, #wifi_dns1, #wifi_dns2');
    ipFields.forEach(function(field) {
      field.disabled = enable;
    });
  }

/* ------------------------------------------------------------------
   DOMContentLoaded – event listeners (leichte Aufräumung, Logik gleich)
------------------------------------------------------------------ */
document.addEventListener('DOMContentLoaded', () => {
  initWebSocket();
  initialiseNetworkPage();

  let lastBtn = null;
  document.getElementById('eth_btn_save').onclick  = () => { lastBtn = 'eth';  showNetworkSavePopup(); };
  document.getElementById('wifi_btn_save').onclick = () => { lastBtn = 'wifi'; showNetworkSavePopup(); };

  document.getElementById('popup-yes').onclick = () => {
    document.getElementById('confirmation-popup').style.display = 'none';
    document.getElementById('loading-indicator').style.display    = 'flex';
    if (lastBtn === 'eth')  saveETHNetworkSettings();
    if (lastBtn === 'wifi') saveWifiNetworkSettings();
    lastBtn = null;
    setTimeout(() => window.location.href = 'index.html', 5000);
  };
  document.getElementById('popup-no').onclick = () => {
    document.getElementById('confirmation-popup').style.display = 'none';
    lastBtn = null;
  };

  document.getElementById('eth_dhcp-toggle').onchange  = (e) => toggleETHInputFields(!e.target.checked);
  document.getElementById('wifi_dhcp-toggle').onchange = (e) => toggleWiFiInputFieldsForDHCP(!e.target.checked);
  const wifiEnable = document.getElementById('wifi_enable');
  if (wifiEnable) {
    wifiEnable.onchange = (e) => {
      const en = e.target.checked;
      toggleWifiInputFields(en);
      //en ? startWifiScan() : hideWifiScanResults();
      if (en) {
        showWifiScanResults();
        document.getElementById('wifi_rescan_btn').style.display = 'inline-block';
        document.getElementById('wifi_scan_message').style.display = 'none';
        document.getElementById('wifi_scan_results').innerHTML = '';
      } else {
        hideWifiScanResults();
      }
    };
  }
  document.getElementById('wifi_rescan_btn').onclick = () => startWifiScan();
});
