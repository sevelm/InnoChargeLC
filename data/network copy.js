// network.js  – sequential version
// ------------------------------------------------------------
// 1) Ethernet‑Infos holen → 2) WiFi‑Infos holen → 3) ggf. WiFi‑Scan
// ------------------------------------------------------------

let currentSSID = '';
let Socket;           // WebSocket instance
let scanInProgress = false;   // verhindert Parallel‑Scans

/* ------------------------------------------------------------------
   WebSocket initialisation
------------------------------------------------------------------ */
function initWebSocket() {
  Socket = new WebSocket('ws://' + window.location.hostname + ':81/');

  Socket.onopen = () => {
    console.log('[WS] opened');
    //Socket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'network' }));
  };

  Socket.onmessage = (e) => console.log('[WS] msg:', e.data);
  Socket.onclose   = (e) => console.log('[WS] closed:', e);
  Socket.onerror   = (e) => console.log('[WS] error:',  e);

  window.onbeforeunload = () => {
    //Socket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'network' }));
    Socket.close();
  };
}

/* ------------------------------------------------------------------
   Fetch helpers – return Promises
------------------------------------------------------------------ */
async function fetchEthSettings() {
  const r  = await fetch('/get_EthNetwork_info');
  const d  = await r.json();
  console.log('[ETH] info:', d);
  setEthernetInfo(d);
  return d;
}

async function fetchWifiSettings() {
  const r  = await fetch('/get_WifiNetwork_info');
  const d  = await r.json();
  console.log('[WiFi] info:', d);
  currentSSID = d.wifi_ssid;
  setWifiInfo(d);
  return d;
}

/* ------------------------------------------------------------------
   Sequential initialisation flow
------------------------------------------------------------------ */
async function initialiseNetworkPage() {
  try {
    await fetchEthSettings();                    // 1️⃣ Ethernet
    //const wifi = await fetchWifiSettings();      // 2️⃣ Wi‑Fi!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  //  if (wifi.wifi_enable) {                     // 3️⃣ optional Scan
  //    startWifiScan();
  //  } else {
  //    hideWifiScanResults();
  //  }
  
  
 // if (wifi.wifi_enable) { !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 //   // Scan NICHT automatisch starten – nur UI zeigen und Button aktivieren
 //   showWifiScanResults();
 //   document.getElementById('wifi_rescan_btn').style.display = 'inline-block';
 //   document.getElementById('wifi_scan_message').style.display = 'none';
 //   document.getElementById('wifi_scan_results').innerHTML = '';
 // } else {
 //   hideWifiScanResults();
 // }
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
}

function setWifiInfo(data) {
  const wifiEnabled = data.wifi_enable;
  document.getElementById('wifi_enable').checked = wifiEnabled;
  toggleWifiInputFields(wifiEnabled);

  document.getElementById('wifi_ssid').value            = data.wifi_ssid || '';
  document.getElementById('wifi_password').value        = data.wifi_pwd || '';
  document.getElementById('wifi_connected').innerText   = data.wifi_connected ? 'Connected' : 'Disconnected';
  document.getElementById('wifi_ssid_selected').innerText = data.wifi_ssid || '';
  document.getElementById('wifi_mac-address').value     = data.wifi_mac || '';
  document.getElementById('wifi_ip-address').value      = data.wifi_ip || '';
  document.getElementById('wifi_subnet-mask').value     = data.wifi_netmask || '';
  document.getElementById('wifi_gateway').value         = data.wifi_gateway || '';
  document.getElementById('wifi_dns1').value            = data.wifi_dns1 || '';
  document.getElementById('wifi_dns2').value            = data.wifi_dns2 || '';

  const wifiDhcpEnabled = data.wifi_static;
  document.getElementById('wifi_dhcp-toggle').checked = wifiDhcpEnabled;
  toggleWiFiInputFieldsForDHCP(!wifiDhcpEnabled);
  // --- Add WiFi signal info display ---
  if (data.wifi_rssi !== undefined) {
    const signalText = (data.wifi_quality !== undefined)
      ? `${getSignalQuality(data.wifi_rssi)} (${data.wifi_quality}%)`
      : getSignalQuality(data.wifi_rssi);

    // Set text in existing or new placeholders
    const elSignal = document.getElementById('wifi_signal');
    const elRssi   = document.getElementById('wifi_rssi');

    if (elSignal) elSignal.innerText = signalText;
    if (elRssi)   elRssi.innerText   = `${data.wifi_rssi} dBm`;
  }
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
  document.getElementById('eth_btn_save').onclick  = () => { lastBtn = 'eth';  document.getElementById('confirmation-popup').style.display = 'flex'; };
  document.getElementById('wifi_btn_save').onclick = () => { lastBtn = 'wifi'; document.getElementById('confirmation-popup').style.display = 'flex'; };

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
