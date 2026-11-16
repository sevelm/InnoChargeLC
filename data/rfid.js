var Socket;
var lastTag = '';
var authRequiredInitialized = false;
var localSettingsInitialized = false;
var localSettingsBusy = false;
var localSettingsTimer;
var ocppEnabledForRfid = false;
var localRfidControlsAvailable = false;
var savedLocalRfidSettings = { mode: 'actual', customId: '' };

function updateLocalRfidOptions() {
  const mode = document.getElementById('localRfidOcppReporting').value;
  document.getElementById('localRfidOcppOptions').hidden =
    !ocppEnabledForRfid || !document.getElementById('localRfidEnabled').checked;
  document.getElementById('localRfidCustomIdRow').hidden = mode !== 'custom';
  document.getElementById('localRfidReportingHint').textContent = !localRfidControlsAvailable
    ? 'Reporting inactive.' : mode === 'none'
    ? 'Status only. No card ID or remote stop.'
    : 'Starts locally. Backend may stop. Applies to new sessions.';
}

function saveLocalRfidReporting() {
  const mode = document.getElementById('localRfidOcppReporting').value;
  const id = document.getElementById('localRfidCustomOcppId').value;
  if ((mode === 'custom' || id.length) && !/^[\x21-\x7e]{1,20}$/.test(id)) {
    document.getElementById('localRfidSaveStatus').textContent = 'Not saved. Custom OCPP ID: 1-20 printable ASCII characters, no spaces.';
    return;
  }
  saveAuthorizationSettings({action: 'saveLocalRfidSettings',
    enabled: document.getElementById('localRfidEnabled').checked, mode, customId: id});
}

function saveAuthorizationSettings(payload) {
  if (localSettingsBusy || !Socket || Socket.readyState !== WebSocket.OPEN) return;
  localSettingsBusy = true;
  document.getElementById('localRfidSettings').disabled = true;
  document.getElementById('rfidAuthRequired').disabled = true;
  document.getElementById('localRfidSaveStatus').textContent = 'Saving...';
  Socket.send(JSON.stringify(payload));
  clearTimeout(localSettingsTimer);
  localSettingsTimer = setTimeout(() => {
    localSettingsBusy = false;
    localSettingsInitialized = false;
    authRequiredInitialized = false;
    document.getElementById('localRfidSaveStatus').textContent = 'No confirmation. Reload to check saved settings.';
  }, 6000);
}

function initWebSocket() {
  Socket = new WebSocket('ws://' + window.location.hostname + ':81/');

  Socket.onopen = function() {
    console.log('WebSocket connection opened');
    Socket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'rfid' }));
  };

  Socket.onmessage = function(event) {
    processCommand(event.data);
  };

  Socket.onclose = function(event) {
    console.log('WebSocket connection closed:', event);
    document.getElementById('localRfidSettings').disabled = true;
    document.getElementById('rfidAuthRequired').disabled = true;
  };

  Socket.onerror = function(error) {
    console.log('WebSocket error:', error);
  };

  window.onbeforeunload = function() {
    Socket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'rfid' }));
    Socket.close();
  };
}

function processCommand(txt) {
  let obj;
  try { obj = JSON.parse(txt); } catch { return; }

  if (obj.type === 'ocppState' && obj.config) {
    ocppEnabledForRfid = !!obj.config.enabled;
    updateLocalRfidOptions();
  }
  if (obj.type === 'rfidSettingsSaved') {
    clearTimeout(localSettingsTimer);
    localSettingsBusy = false;
    localSettingsInitialized = false;
    authRequiredInitialized = false;
    document.getElementById('localRfidSaveStatus').textContent = obj.ok ? '' : (obj.message || 'Save failed.');
  }
  if (obj.localRfidEnabled !== undefined && !localSettingsInitialized && !localSettingsBusy) {
    localRfidControlsAvailable = obj.localRfidControlsAvailable !== false;
    savedLocalRfidSettings = {
      mode: obj.localRfidOcppReporting || 'actual',
      customId: obj.localRfidCustomOcppId || ''
    };
    document.getElementById('localRfidEnabled').checked = obj.localRfidEnabled;
    document.getElementById('localRfidOcppReporting').value = obj.localRfidOcppReporting || 'actual';
    document.getElementById('localRfidCustomOcppId').value = obj.localRfidCustomOcppId || '';
    document.getElementById('localRfidSettings').disabled = !localRfidControlsAvailable;
    document.getElementById('rfidAuthRequired').disabled = false;
    localSettingsInitialized = true;
    updateLocalRfidOptions();
  }

  if (obj.rfidTag !== undefined) {
    document.getElementById('rfidTag').textContent = obj.rfidTag;
  }

  if (obj.lastRfidTag !== undefined) {
    lastTag = obj.lastRfidTag;
    document.getElementById('lastRfidTag').textContent = obj.lastRfidTag;
  }

  if (obj.rfidAuthorized !== undefined) {
    const el = document.getElementById('rfidAuthStatus');
    if (obj.rfidAuthorized) {
      el.textContent = obj.rfidUserName ? 'authorized: ' + obj.rfidUserName : 'authorized';
      el.className = 'status ready';
    } else {
      el.textContent = 'not authorized';
      el.className = 'status error';
    }
  }

  if (obj.rfidAuthRequired !== undefined && !authRequiredInitialized && !localSettingsBusy) {
    document.getElementById('rfidAuthRequired').checked = obj.rfidAuthRequired;
    authRequiredInitialized = true;
  }

  if (obj.chargeSessionAuthorized !== undefined) {
    setStatus('chargeSessionAuthorized', obj.chargeSessionAuthorized, 'yes', 'no');
    if (obj.authorizationPending) {
      document.getElementById('chargeSessionAuthorized').textContent = 'pending authorization / transaction preparation';
    } else if (obj.authorizationStopLatched) {
      document.getElementById('chargeSessionAuthorized').textContent = 'stopped - new authorization or unplug required';
    }
  }

  if (obj.chargeSessionVehicleWasConnected !== undefined) {
    setStatus('chargeSessionVehicleWasConnected', obj.chargeSessionVehicleWasConnected, 'yes', 'no');
  }

  setTextIfPresent('chargeSessionIdTag', obj.chargeSessionIdTag);
  setTextIfPresent('chargeSessionUserName', obj.chargeSessionUserName);
  setTextIfPresent('chargeSessionAuthorizationMillis', obj.chargeSessionAuthorizationMillis);
  setTextIfPresent('chargeSessionLastChargeMillis', obj.chargeSessionLastChargeMillis);
  setTextIfPresent('chargeSessionAuthorizationTime', obj.chargeSessionAuthorizationTime);
  setTextIfPresent('chargeSessionLastChargeTime', obj.chargeSessionLastChargeTime);

  if (obj.rfidDb?.users) {
    renderUsers(obj.rfidDb.users);
  }
}

function setTextIfPresent(id, value) {
  if (value === undefined) return;
  const el = document.getElementById(id);
  if (el) el.textContent = value || '';
}

function setStatus(id, value, trueText, falseText) {
  const el = document.getElementById(id);
  if (!el) return;

  if (value) {
    el.textContent = trueText;
    el.className = 'status ready';
  } else {
    el.textContent = falseText;
    el.className = 'status error';
  }
}

function normalizeTag(value) {
  return (value || '').replace(/[:\-\s]/g, '').toUpperCase();
}

function renderUsers(users) {
  const tbody = document.getElementById('rfidUsers');
  tbody.innerHTML = '';

  for (const user of users) {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${escapeHtml(user.idTag || '')}</td>
      <td>${escapeHtml(user.name || '')}</td>
      <td>${user.enabled ? 'yes' : 'no'}</td>
      <td>${user.maxChargeMinutes ? escapeHtml(user.maxChargeMinutes) + ' min' : '-'}</td>
      <td>${escapeHtml(user.note || '')}</td>
      <td>
        <button type="button" data-edit="${escapeHtml(user.idTag || '')}">Edit</button>
        <button type="button" data-delete="${escapeHtml(user.idTag || '')}">Delete</button>
      </td>
    `;
    tbody.appendChild(tr);
  }

  tbody.querySelectorAll('[data-edit]').forEach(btn => {
    btn.addEventListener('click', () => {
      const user = users.find(item => item.idTag === btn.dataset.edit);
      if (!user) return;
      document.getElementById('idTag').value = user.idTag || '';
      document.getElementById('userName').value = user.name || '';
      document.getElementById('userEnabled').checked = user.enabled !== false;
      document.getElementById('maxChargeMinutes').value = user.maxChargeMinutes || 0;
      document.getElementById('userNote').value = user.note || '';
    });
  });

  tbody.querySelectorAll('[data-delete]').forEach(btn => {
    btn.addEventListener('click', () => {
      Socket.send(JSON.stringify({
        action: 'deleteRfidUser',
        idTag: btn.dataset.delete
      }));
    });
  });
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

document.addEventListener('DOMContentLoaded', function() {
  initWebSocket();

  document.getElementById('useLastTag').addEventListener('click', function() {
    document.getElementById('idTag').value = normalizeTag(lastTag);
  });

  document.getElementById('saveRfidUser').addEventListener('click', function() {
    const user = {
      action: 'saveRfidUser',
      idTag: normalizeTag(document.getElementById('idTag').value),
      name: document.getElementById('userName').value.trim(),
      enabled: document.getElementById('userEnabled').checked,
      maxChargeMinutes: Math.max(0, Number.parseInt(document.getElementById('maxChargeMinutes').value, 10) || 0),
      note: document.getElementById('userNote').value.trim()
    };

    if (!user.idTag) {
      alert('ID Tag fehlt.');
      return;
    }

    Socket.send(JSON.stringify(user));
  });

  document.getElementById('rfidAuthRequired').addEventListener('change', function(event) {
    saveAuthorizationSettings({
      action: 'setRfidAuthorizationRequired',
      state: event.target.checked
    });
  });
  document.getElementById('localRfidEnabled').addEventListener('change', function(event) {
    // A switch change must not save an unfinished custom-ID draft.
    saveAuthorizationSettings({action: 'saveLocalRfidSettings',
      enabled: event.target.checked, ...savedLocalRfidSettings});
  });
  document.getElementById('localRfidOcppReporting').addEventListener('change', function() {
    updateLocalRfidOptions();
    const customId = document.getElementById('localRfidCustomOcppId');
    if (this.value === 'custom' && !/^[\x21-\x7e]{1,20}$/.test(customId.value)) {
      document.getElementById('localRfidSaveStatus').textContent = 'Enter a valid custom OCPP ID to save this selection.';
      customId.focus();
      return;
    }
    if (this.value !== 'custom') customId.value = savedLocalRfidSettings.customId;
    saveLocalRfidReporting();
  });
  document.getElementById('localRfidCustomOcppId').addEventListener('change', saveLocalRfidReporting);
  document.getElementById('localRfidCustomOcppId').addEventListener('keydown', function(event) {
    if (event.key === 'Enter') {
      event.preventDefault();
      this.blur();
    }
  });
});
