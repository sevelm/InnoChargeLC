var Socket;
var lastSessionsPayload = { sessions: [] };
var mailerInitialized = false;
var sessionSort = { key: 'transactionId', direction: 'desc' };
var currentRenderedSessions = [];
var sessionPageOffset = 0;
var sessionPageLimit = 100;
var sessionPageTotal = 0;
var wallboxName = 'InnoCharge';

function initWebSocket() {
  Socket = new WebSocket('ws://' + window.location.hostname + ':81/');

  Socket.onopen = function() {
    Socket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'sessions' }));
    requestSessionPage(0);
  };

  Socket.onmessage = function(event) {
    processCommand(event.data);
  };

  window.onbeforeunload = function() {
    Socket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'sessions' }));
    Socket.close();
  };
}

function processCommand(txt) {
  let obj;
  try { obj = JSON.parse(txt); } catch { return; }

  if (obj.sessions) {
    lastSessionsPayload = obj;
    wallboxName = obj.wallboxName || 'InnoCharge';
    sessionPageOffset = Number(obj.sessionOffset) || 0;
    sessionPageLimit = Number(obj.sessionLimit) || 100;
    sessionPageTotal = Number(obj.sessionTotal) || obj.sessions.length || 0;
    renderSessions(obj.sessions);
    updateSessionPaging();
  }

  if (obj.mailer) {
    renderMailer(obj.mailer);
  }
}

function renderSessions(sessions) {
  const tbody = document.getElementById('chargeSessions');
  tbody.innerHTML = '';

  const sortedSessions = sortedSessionList(filteredSessionList(sessions));
  currentRenderedSessions = sortedSessions;

  if (!sortedSessions.length) {
    const tr = document.createElement('tr');
    tr.innerHTML = '<td colspan="9">No charge sessions recorded.</td>';
    tbody.appendChild(tr);
    updateSortHeaders();
    updateSessionPaging();
    return;
  }

  for (const session of sortedSessions) {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${escapeHtml(session.transactionId || '')}</td>
      <td>${session.active ? '<span class="status ready">active</span>' : 'closed'}</td>
      <td>${escapeHtml(session.idTag || '')}</td>
      <td>${escapeHtml(session.userName || '')}</td>
      <td>${formatTime(session.startTime)}</td>
      <td>${formatTime(session.stopTime)}</td>
      <td>${formatDuration(session.durationSeconds || 0)}</td>
      <td>${formatEnergy(session.energyWh || 0)}</td>
      <td>${escapeHtml(session.stopReason || '')}</td>
    `;
    tbody.appendChild(tr);
  }
  updateSortHeaders();
  updateSessionPaging();
}

function requestSessionPage(offset) {
  if (!Socket || Socket.readyState !== 1) return;
  const safeOffset = Math.max(0, Number(offset) || 0);
  Socket.send(JSON.stringify({ action: 'requestSessionPage', offset: safeOffset }));
}

function updateSessionPaging() {
  const info = document.getElementById('sessionPageInfo');
  const prev = document.getElementById('sessionPrevPage');
  const next = document.getElementById('sessionNextPage');
  if (!info || !prev || !next) return;

  const visible = (lastSessionsPayload.sessions || []).length;
  const from = sessionPageTotal && visible ? sessionPageOffset + 1 : 0;
  const to = sessionPageTotal && visible ? Math.min(sessionPageOffset + visible, sessionPageTotal) : 0;
  info.textContent = from + '-' + to + ' of ' + sessionPageTotal;

  prev.disabled = sessionPageOffset <= 0;
  next.disabled = sessionPageOffset + sessionPageLimit >= sessionPageTotal;
}

function filteredSessionList(sessions) {
  const text = document.getElementById('sessionFilterText')?.value.trim().toLowerCase() || '';
  const status = document.getElementById('sessionFilterStatus')?.value || 'all';
  const fromValue = document.getElementById('sessionFilterFrom')?.value || '';
  const toValue = document.getElementById('sessionFilterTo')?.value || '';
  const fromTime = fromValue ? new Date(fromValue + 'T00:00:00').getTime() : 0;
  const toTime = toValue ? new Date(toValue + 'T23:59:59').getTime() : 0;

  return sessions.filter(session => {
    if (status === 'closed' && session.active) return false;
    if (status === 'active' && !session.active) return false;

    const stopOrStart = session.stopTime || session.startTime || '';
    const sessionTime = stopOrStart ? new Date(stopOrStart).getTime() : 0;
    if (fromTime && sessionTime && sessionTime < fromTime) return false;
    if (toTime && sessionTime && sessionTime > toTime) return false;

    if (!text) return true;
    const haystack = [
      session.transactionId,
      session.active ? 'active' : 'closed',
      session.idTag,
      session.userName,
      session.startTime,
      session.stopTime,
      session.durationSeconds,
      session.energyWh,
      session.stopReason
    ].join(' ').toLowerCase();
    return haystack.includes(text);
  });
}

function sortedSessionList(sessions) {
  const numericKeys = [
    'transactionId',
    'startTimeEpoch',
    'stopTimeEpoch',
    'durationSeconds',
    'energyWh'
  ];
  const key = sessionSort.key;
  const direction = sessionSort.direction === 'asc' ? 1 : -1;

  return [...sessions].sort((a, b) => {
    let av = key === 'status' ? (a.active ? 'active' : 'closed') : a[key];
    let bv = key === 'status' ? (b.active ? 'active' : 'closed') : b[key];

    if (numericKeys.includes(key)) {
      av = Number(av) || 0;
      bv = Number(bv) || 0;
      return (av - bv) * direction;
    }

    av = String(av || '').toLowerCase();
    bv = String(bv || '').toLowerCase();
    return av.localeCompare(bv) * direction;
  });
}

function updateSortHeaders() {
  document.querySelectorAll('th[data-sort]').forEach(th => {
    const label = th.textContent.replace(/\s*[▲▼]$/, '');
    th.textContent = label;
    th.classList.add('sortable-header');
    if (th.dataset.sort === sessionSort.key) {
      th.textContent = label + (sessionSort.direction === 'asc' ? ' ▲' : ' ▼');
    }
  });
}

function setupSorting() {
  document.querySelectorAll('th[data-sort]').forEach(th => {
    th.addEventListener('click', () => {
      const key = th.dataset.sort;
      if (sessionSort.key === key) {
        sessionSort.direction = sessionSort.direction === 'asc' ? 'desc' : 'asc';
      } else {
        sessionSort.key = key;
        sessionSort.direction = ['transactionId', 'startTimeEpoch', 'stopTimeEpoch', 'durationSeconds', 'energyWh'].includes(key) ? 'desc' : 'asc';
      }
      renderSessions(lastSessionsPayload.sessions || []);
    });
  });
}

function formatTime(value) {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return escapeHtml(value);
  return date.toLocaleString();
}

function formatDuration(seconds) {
  const total = Math.max(0, Number(seconds) || 0);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  return String(h).padStart(2, '0') + ':' +
         String(m).padStart(2, '0') + ':' +
         String(s).padStart(2, '0');
}

function formatEnergy(wh) {
  return ((Number(wh) || 0) / 1000).toFixed(3) + ' kWh';
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function setFieldIfNotFocused(id, value) {
  const el = document.getElementById(id);
  if (!el || document.activeElement === el) return;
  el.value = value || '';
}

function renderMailer(mailer) {
  if (!mailerInitialized) {
    setFieldIfNotFocused('mailServer', mailer.server);
    setFieldIfNotFocused('mailPort', mailer.port || 465);
    document.getElementById('mailSsl').checked = mailer.ssl !== false;
    setFieldIfNotFocused('mailUsername', mailer.username);
    setFieldIfNotFocused('mailFrom', mailer.from);
    setFieldIfNotFocused('mailTo', mailer.to);
    setFieldIfNotFocused('mailSubject', mailer.subject || 'InnoCharge charge sessions');
    document.getElementById('mailEnable').checked = mailer.enable === true;
    setFieldIfNotFocused('mailMode', mailer.mode || 0);
    mailerInitialized = true;
  }
  document.getElementById('mailStatus').textContent = mailer.lastStatus || '';
}

function saveMailerSettings() {
  if (!Socket || Socket.readyState !== 1) return;
  Socket.send(JSON.stringify({
    action: 'saveSessionMailerSettings',
    settings: {
      server: document.getElementById('mailServer').value.trim(),
      port: Math.max(1, Number.parseInt(document.getElementById('mailPort').value, 10) || 465),
      ssl: document.getElementById('mailSsl').checked,
      username: document.getElementById('mailUsername').value.trim(),
      password: document.getElementById('mailPassword').value,
      from: document.getElementById('mailFrom').value.trim(),
      to: document.getElementById('mailTo').value.trim(),
      subject: document.getElementById('mailSubject').value.trim() || 'InnoCharge charge sessions',
      enable: document.getElementById('mailEnable').checked,
      mode: Number.parseInt(document.getElementById('mailMode').value, 10) || 0
    }
  }));
  document.getElementById('mailPassword').value = '';
}

function sendSessionReport() {
  if (!Socket || Socket.readyState !== 1) return;
  document.getElementById('mailStatus').textContent = 'Sending...';
  Socket.send(JSON.stringify({ action: 'sendSessionReportNow' }));
}

function exportSessions() {
  const payload = { sessions: currentRenderedSessions };
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = 'charge_sessions.json';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
}

function csvEscape(value) {
  const text = String(value ?? '');
  if (/[",\r\n;]/.test(text)) {
    return '"' + text.replace(/"/g, '""') + '"';
  }
  return text;
}

function exportSessionsCsv() {
  const header = [
    'Transaction',
    'Status',
    'ID Tag',
    'User',
    'Start',
    'Stop',
    'Duration',
    'Energy kWh',
    'Stop reason'
  ];

  const lines = [header.map(csvEscape).join(';')];
  for (const session of currentRenderedSessions || []) {
    lines.push([
      session.transactionId || '',
      session.active ? 'active' : 'closed',
      session.idTag || '',
      session.userName || '',
      formatTime(session.startTime),
      formatTime(session.stopTime),
      formatDuration(session.durationSeconds || 0),
      ((Number(session.energyWh) || 0) / 1000).toFixed(3),
      session.stopReason || ''
    ].map(csvEscape).join(';'));
  }

  const blob = new Blob([lines.join('\r\n')], { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = 'charge_sessions.csv';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
}

function exportSessionsPdf() {
  const sessions = currentRenderedSessions || [];
  const printedAt = new Date().toLocaleString();
  const rows = sessions.map(session => `
    <tr>
      <td>${escapeHtml(session.transactionId || '')}</td>
      <td>${session.active ? 'active' : 'closed'}</td>
      <td>${escapeHtml(session.idTag || '')}</td>
      <td>${escapeHtml(session.userName || '')}</td>
      <td>${formatTime(session.startTime)}</td>
      <td>${formatTime(session.stopTime)}</td>
      <td>${formatDuration(session.durationSeconds || 0)}</td>
      <td>${formatEnergy(session.energyWh || 0)}</td>
      <td>${escapeHtml(session.stopReason || '')}</td>
    </tr>
  `).join('');

  const printWindow = window.open('', '_blank');
  if (!printWindow) {
    alert('Popup blocked. Please allow popups for PDF export.');
    return;
  }

  printWindow.document.open();
  printWindow.document.write(`<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Charge Sessions</title>
  <style>
    @page { size: A4 landscape; margin: 12mm 12mm 22mm 12mm; }
    * { box-sizing: border-box; }
    body { font-family: Arial, sans-serif; color: #111; margin: 0; }
    .header { display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid #b0bec5; padding-bottom: 8px; margin-bottom: 12px; }
    .brand { display: flex; align-items: center; gap: 16px; }
    .brand img { width: 150px; height: auto; }
    h1 { font-size: 22px; margin: 0 0 6px 0; color: #263238; }
    .meta { font-size: 12px; line-height: 1.5; text-align: right; }
    table { width: 100%; border-collapse: collapse; font-size: 10px; }
    th, td { border: 1px solid #cfd8dc; padding: 5px 6px; text-align: left; vertical-align: top; }
    th { background: #eef2f4; color: #263238; font-weight: bold; }
    tr:nth-child(even) td { background: #f8fafb; }
    thead { display: table-header-group; }
    .empty { padding: 20px 0; font-size: 13px; }
  </style>
</head>
<body>
  <div class="header">
    <div class="brand">
      <img src="/innocharge.png" alt="InnoCharge">
      <div>
        <h1>Charge Sessions</h1>
        <div>Wallbox: ${escapeHtml(wallboxName)}</div>
      </div>
    </div>
    <div class="meta">
      <div>Printed: ${escapeHtml(printedAt)}</div>
      <div>Sessions: ${sessions.length}</div>
      <div>Page data: ${sessionPageOffset + 1}-${Math.min(sessionPageOffset + sessions.length, sessionPageTotal)} of ${sessionPageTotal}</div>
    </div>
  </div>
  ${sessions.length ? `
  <table>
    <thead>
      <tr>
        <th>Transaction</th>
        <th>Status</th>
        <th>ID Tag</th>
        <th>User</th>
        <th>Start</th>
        <th>Stop</th>
        <th>Duration</th>
        <th>Energy</th>
        <th>Stop reason</th>
      </tr>
    </thead>
    <tbody>${rows}</tbody>
  </table>` : '<div class="empty">No charge sessions recorded.</div>'}
</body>
</html>`);
  printWindow.document.close();
  try {
    printWindow.history.replaceState(null, 'Charge Sessions', '/sessions-print');
  } catch (e) {
  }
  printWindow.focus();
  setTimeout(() => printWindow.print(), 300);
}

function setupFilters() {
  ['sessionFilterText', 'sessionFilterStatus', 'sessionFilterFrom', 'sessionFilterTo'].forEach(id => {
    document.getElementById(id).addEventListener('input', () => renderSessions(lastSessionsPayload.sessions || []));
    document.getElementById(id).addEventListener('change', () => renderSessions(lastSessionsPayload.sessions || []));
  });

  document.getElementById('clearSessionFilters').addEventListener('click', () => {
    document.getElementById('sessionFilterText').value = '';
    document.getElementById('sessionFilterStatus').value = 'all';
    document.getElementById('sessionFilterFrom').value = '';
    document.getElementById('sessionFilterTo').value = '';
    renderSessions(lastSessionsPayload.sessions || []);
  });
}

function importSessions() {
  const input = document.getElementById('importSessionsFile');
  const file = input.files && input.files[0];
  if (!file) {
    alert('No file selected.');
    return;
  }

  const reader = new FileReader();
  reader.onload = async function() {
    let parsed;
    try {
      parsed = JSON.parse(String(reader.result || ''));
    } catch {
      alert('Invalid JSON file.');
      return;
    }

    if (!Array.isArray(parsed.sessions)) {
      alert('No sessions found in file.');
      return;
    }

    if (parsed.sessions.length > 100) {
      alert('Import failed: maximum 100 sessions per file.');
      return;
    }

    try {
      const res = await fetch('/importsessions', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(parsed)
      });
      if (!res.ok) {
        alert('Import failed.');
        return;
      }
      alert('Import OK.');
    } catch (e) {
      alert('Import network error: ' + e);
    }
  };
  reader.readAsText(file);
}

document.addEventListener('DOMContentLoaded', function() {
  initWebSocket();
  setupSorting();
  setupFilters();
  document.getElementById('exportSessions').addEventListener('click', exportSessions);
  document.getElementById('exportSessionsCsv').addEventListener('click', exportSessionsCsv);
  document.getElementById('exportSessionsPdf').addEventListener('click', exportSessionsPdf);
  document.getElementById('importSessions').addEventListener('click', importSessions);
  document.getElementById('sessionPrevPage').addEventListener('click', () => requestSessionPage(sessionPageOffset - sessionPageLimit));
  document.getElementById('sessionNextPage').addEventListener('click', () => requestSessionPage(sessionPageOffset + sessionPageLimit));
  document.getElementById('saveMailerSettings').addEventListener('click', saveMailerSettings);
  document.getElementById('sendSessionReport').addEventListener('click', sendSessionReport);
});
