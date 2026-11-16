// Uses the RFID page's authenticated WebSocket; no additional connection.
(function () {
  'use strict';
  const fields = {
    backendUrl: 'ocppBackendUrl', chargePointId: 'ocppChargePointId',
    vendor: 'ocppVendor', model: 'ocppModel', serialNumber: 'ocppSerialNumber'
  };
  let loaded = false;
  let busy = false;
  let controllerReady = false;
  let loadSavedFields = false;
  let requestPending = false;
  let requestTimer;
  let saveTimer;
  let burstTimer;
  let burstRemaining = 0;
  let pendingCa = '';
  let readingCa = false;
  let caReadRevision = 0;
  const el = id => document.getElementById(id);
  const connected = () => window.Socket && Socket.readyState === WebSocket.OPEN;

  function controls() {
    el('ocppEnabled').disabled = !connected() || !loaded || !controllerReady || busy || readingCa;
    el('ocppSettingsFields').disabled = !connected() || !loaded || !controllerReady || busy || readingCa;
    el('ocppRefresh').disabled = !connected();
    el('ocppClearLog').disabled = !connected() || busy;
  }

  function profileFields() {
    const tls = el('ocppSecurityProfile').value === '2';
    document.querySelectorAll('.ocpp-tls-field').forEach(row => { row.hidden = !tls; });
    el('ocppPassword').disabled = !tls;
    el('ocppCaFile').disabled = !tls;
  }

  function clearSecrets() {
    el('ocppPassword').value = '';
    el('ocppCaFile').value = '';
    pendingCa = '';
    el('ocppCaSelection').textContent = 'No replacement selected.';
  }

  function feedback(message, error) {
    el('ocppSaveStatus').textContent = message;
    el('ocppSaveStatus').style.color = error ? '#b3261e' : '#333';
  }

  function requestState() {
    if (!connected() || document.hidden || requestPending) return;
    requestPending = true;
    Socket.send(JSON.stringify({ action: 'getOcppState', includeLog: true }));
    clearTimeout(requestTimer);
    requestTimer = setTimeout(() => {
      requestPending = false;
      el('ocppControllerStatus').textContent = 'No response — refresh to retry';
    }, 6000);
  }

  function save(action, settings) {
    if (!connected() || busy || readingCa) return;
    busy = true;
    controls();
    feedback('Saving...', false);
    Socket.send(JSON.stringify({ action, settings }));
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
      busy = false;
      controls();
      feedback('No confirmation received. Refresh to check the saved state.', true);
      requestPending = false;
      requestState();
    }, 6000);
  }

  function render(message) {
    const cfg = message.config;
    const state = message.status;
    if (!cfg || !state) return;
    requestPending = false;
    clearTimeout(requestTimer);
    if (!loaded || loadSavedFields) {
      Object.keys(fields).forEach(key => { el(fields[key]).value = cfg[key] || ''; });
      el('ocppSecurityProfile').value = String(cfg.securityProfile);
      profileFields();
      loadSavedFields = false;
    }
    if (!loaded) feedback('Settings loaded.', false);
    loaded = true;
    controllerReady = state.controllerReady;
    if (!busy) el('ocppEnabled').checked = cfg.enabled;
    el('ocppFirmware').textContent = cfg.firmware || '-';
    el('ocppControllerStatus').textContent = state.label;
    el('ocppConnectionStatus').textContent =
      (state.connected ? 'Connected' : 'Disconnected') + ' / ' +
      (state.bootAccepted ? 'Accepted' : 'Not accepted');
    el('ocppHeartbeats').textContent = String(state.heartbeats) +
      (state.heartbeatAgeSeconds >= 0 ? ' (last: ' + state.heartbeatAgeSeconds + ' s ago)' : '');
    el('ocppReconnects').textContent = state.reconnects;
    el('ocppError').textContent = state.lastError || '-';
    el('ocppPasswordStatus').textContent = cfg.passwordSet ? 'Password is saved.' : 'No password saved.';
    el('ocppCaStatus').textContent = cfg.caConfigured ? 'Imported CA stored in NVS' : 'No CA imported';
    el('ocppTimeStatus').textContent = state.timeValid ? 'Available for certificate validity check' : 'Not valid yet (required for TLS)';
    el('ocppTlsStatus').textContent = state.pending ? 'Applying settings...' :
      !cfg.enabled ? 'Disconnected (OCPP disabled)' :
      state.tlsVerified ? 'TLS 1.2 / CA and hostname verified' :
      cfg.securityProfile === 2 ? 'Profile 2 / no verified connection yet' : 'Profile 0 / unencrypted';
    if (Array.isArray(message.log)) {
      const box = el('ocppLog');
      const text = message.log.map(entry =>
        '[' + (entry.atMs / 1000).toFixed(3) + ' s] ' + entry.message).join('\n') || 'No OCPP events.';
      if (box.textContent !== text) {
        box.textContent = text; // Never render backend strings as HTML.
        box.scrollTop = box.scrollHeight;
      }
    }
    controls();
    if (burstRemaining > 0) {
      --burstRemaining;
      clearTimeout(burstTimer);
      burstTimer = setTimeout(requestState, 750);
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    // rfid.js registers first and creates Socket before this handler runs.
    Socket.addEventListener('open', () => { controls(); requestState(); });
    Socket.addEventListener('message', event => {
      let message;
      try { message = JSON.parse(event.data); } catch (_) { return; }
      if (message.type === 'ocppState') render(message);
      if (message.type === 'ocppSaved') {
        clearTimeout(saveTimer);
        busy = false;
        loadSavedFields = message.ok && message.action === 'saveOcppSettings';
        if (loadSavedFields) clearSecrets();
        feedback(message.message, !message.ok);
        burstRemaining = message.ok ? 6 : 0;
        controls();
      }
    });
    Socket.addEventListener('close', () => {
      clearTimeout(saveTimer);
      clearTimeout(requestTimer);
      clearTimeout(burstTimer);
      busy = false;
      requestPending = false;
      ++caReadRevision;
      readingCa = false;
      clearSecrets();
      el('ocppControllerStatus').textContent = 'Web UI disconnected — displayed values are stale';
      feedback('Connection lost. Reload to check the saved OCPP settings.', true);
      controls();
    });
    el('ocppEnabled').addEventListener('change', event => {
      save('setOcppEnabled', { enabled: event.target.checked });
    });
    el('ocppSettingsForm').addEventListener('submit', event => {
      event.preventDefault();
      if (!el('ocppSettingsForm').reportValidity()) return;
      const settings = {};
      Object.keys(fields).forEach(key => { settings[key] = el(fields[key]).value.trim(); });
      settings.securityProfile = Number(el('ocppSecurityProfile').value);
      settings.authorizationKey = settings.securityProfile === 2 ? el('ocppPassword').value : '';
      if (settings.authorizationKey && !/^[\x21-\x7e]{16,20}$/.test(settings.authorizationKey) &&
          !/^hex:(?:[A-Fa-f0-9]{2}){16,20}$/.test(settings.authorizationKey)) {
        feedback('Password: 16-20 printable ASCII characters, or hex: followed by 32-40 hexadecimal digits.', true);
        return;
      }
      if (!settings.backendUrl.startsWith(settings.securityProfile === 2 ? 'wss://' : 'ws://')) {
        feedback('URL must match the profile: ws:// for 0, wss:// for 2.', true);
        return;
      }
      if (settings.securityProfile === 2 && pendingCa) settings.caPem = pendingCa;
      save('saveOcppSettings', settings);
    });
    Object.values(fields).forEach(id => el(id).addEventListener('input', () => {
      feedback('Unsaved changes. Enable uses the last saved settings.', false);
    }));
    el('ocppSecurityProfile').addEventListener('change', () => {
      profileFields();
      feedback('Unsaved profile. Adjust the backend URL, then save settings.', false);
    });
    el('ocppPassword').addEventListener('input', () => {
      feedback('Unsaved password. An empty field keeps the saved password.', false);
    });
    el('ocppCaFile').addEventListener('change', async event => {
      const revision = ++caReadRevision;
      const file = event.target.files[0];
      pendingCa = '';
      el('ocppCaSelection').textContent = 'No replacement selected.';
      if (!file) return;
      if (!file.size || file.size > 4096) {
        event.target.value = '';
        feedback('CA file must contain 1-4096 bytes of public PEM certificates.', true);
        return;
      }
      readingCa = true;
      controls();
      try {
        const pem = await file.text();
        if (revision !== caReadRevision) return;
        if (!pem.includes('-----BEGIN CERTIFICATE-----') || pem.includes('PRIVATE KEY') || pem.includes('\0')) {
          throw new Error('Select a public CA certificate in PEM format, never a private key.');
        }
        pendingCa = pem;
        el('ocppCaSelection').textContent = file.name + ' selected; not saved yet.';
        feedback('CA selected. Save settings to validate and apply it.', false);
      } catch (error) {
        if (revision !== caReadRevision) return;
        event.target.value = '';
        feedback(error.message || 'Could not read the CA file.', true);
      } finally {
        if (revision === caReadRevision) { readingCa = false; controls(); }
      }
    });
    el('ocppRefresh').addEventListener('click', requestState);
    el('ocppClearLog').addEventListener('click', () => {
      if (connected()) Socket.send(JSON.stringify({ action: 'clearOcppLog' }));
    });
    el('ocppAutoRefresh').addEventListener('change', event => {
      if (event.target.checked) requestState();
    });
    setInterval(() => {
      if (el('ocppAutoRefresh').checked) requestState();
    }, 3000);
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden && el('ocppAutoRefresh').checked) requestState();
    });
    controls();
    if (connected()) requestState();
  });
})();
