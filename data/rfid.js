var Socket;
var lastTag = '';
var authRequiredInitialized = false;

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

  if (obj.rfidAuthRequired !== undefined && !authRequiredInitialized) {
    document.getElementById('rfidAuthRequired').checked = obj.rfidAuthRequired;
    authRequiredInitialized = true;
  }

  if (obj.chargeSessionAuthorized !== undefined) {
    setStatus('chargeSessionAuthorized', obj.chargeSessionAuthorized, 'yes', 'no');
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
    Socket.send(JSON.stringify({
      action: 'setRfidAuthorizationRequired',
      state: event.target.checked
    }));
  });
});
