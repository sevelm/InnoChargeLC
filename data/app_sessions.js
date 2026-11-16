(() => {
  'use strict';
  const el = id => document.getElementById(id);
  let sessions = [];
  let complete = false;
  let loading = false;
  let exportBusy = false;
  let visibleCount = 20;
  let descending = true;
  let generation = 0;
  let requestId = 0;
  let pending = null;
  let wallboxName = 'InnoCharge';

  function message(text, error = false) {
    el('appSessionMessage').textContent = text;
    el('appSessionMessage').classList.toggle('error', error);
  }

  function date(value) {
    const parsed = value ? new Date(value) : null;
    return parsed && Number.isFinite(parsed.getTime()) ? parsed : null;
  }

  function time(value) {
    return date(value)?.toLocaleString('de-AT') || 'Time unavailable';
  }

  function duration(value) {
    const seconds = Math.max(0, Math.floor(Number(value) || 0));
    return [Math.floor(seconds / 3600), Math.floor(seconds % 3600 / 60), seconds % 60]
      .map(part => String(part).padStart(2, '0')).join(':');
  }

  function energy(value) {
    return (Math.max(0, Number(value) || 0) / 1000).toLocaleString('de-AT', { minimumFractionDigits: 3, maximumFractionDigits: 3 }) + ' kWh';
  }

  function selection() {
    const from = el('appSessionFrom').value;
    const to = el('appSessionTo').value;
    const user = el('appSessionUser').value.trim().toLowerCase();
    const status = el('appSessionStatus').value;
    const start = from ? new Date(from + 'T00:00:00').getTime() : -Infinity;
    const end = to ? new Date(to + 'T23:59:59.999').getTime() : Infinity;
    const key = el('appSessionSort').value;
    return sessions.filter(session => {
      const stamp = date(session.startTime)?.getTime();
      if ((from || to) && (!Number.isFinite(stamp) || stamp < start || stamp > end)) return false;
      if (status !== 'all' && Boolean(session.active) !== (status === 'active')) return false;
      return !user || [session.userName, session.idTag].some(value => String(value || '').toLowerCase().includes(user));
    }).sort((a, b) => {
      const av = key === 'userName' ? String(a[key] || '') : Number(a[key]) || 0;
      const bv = key === 'userName' ? String(b[key] || '') : Number(b[key]) || 0;
      const comparison = key === 'userName' ? av.localeCompare(bv) : av - bv;
      return (comparison || Number(a.transactionId) - Number(b.transactionId)) * (descending ? -1 : 1);
    });
  }

  function node(tag, text, className) {
    const item = document.createElement(tag);
    if (text !== undefined) item.textContent = text;
    if (className) item.className = className;
    return item;
  }

  function render() {
    const selected = selection();
    const list = el('appSessionList');
    list.replaceChildren();
    selected.slice(0, visibleCount).forEach(session => {
      const card = node('details', undefined, 'session-card');
      const summary = node('summary');
      const title = node('div', undefined, 'session-card-title');
      title.append(node('time', time(session.startTime)), node('span', session.active ? 'Active' : 'Closed', 'session-badge' + (session.active ? ' active' : '')));
      const metrics = node('div', undefined, 'session-metrics');
      metrics.append(node('strong', energy(session.energyWh)), node('span', duration(session.durationSeconds)));
      summary.append(title, metrics, node('p', session.userName || session.idTag || 'Session #' + session.transactionId, 'session-user'));
      const details = node('dl', undefined, 'session-details');
      const delivery = value => ({ none: '-', pending: 'Pending', sent: 'Unconfirmed', confirmed: 'Confirmed', uncertain: 'Review', failed: 'Failed' })[value] || '-';
      for (const [label, value] of [
        ['Local ID', session.transactionId], ['User', session.userName], ['RFID', session.idTag],
        ['Start', time(session.startTime)], ['Stop', session.active ? 'Active' : time(session.stopTime)],
        ['Duration', duration(session.durationSeconds)], ['Energy', energy(session.energyWh)],
        ['Stop reason', session.stopReason], ['OCPP ID', session.ocppTransactionId],
        ['OCPP tag', session.ocppIdTag], ['OCPP start', delivery(session.ocppStart)], ['OCPP stop', delivery(session.ocppStop)]
      ]) {
        const row = node('div', undefined, 'status-row');
        row.append(node('dt', label), node('dd', value === null || value === undefined || value === '' ? '-' : String(value)));
        details.append(row);
      }
      card.append(summary, details);
      list.append(card);
    });
    if (!selected.length && !loading) list.append(node('p', complete ? 'No sessions match this selection.' : 'No sessions loaded.', 'session-count'));
    if (!loading) el('appSessionCount').textContent = 'Showing ' + Math.min(visibleCount, selected.length) + ' of ' + selected.length + ' sessions';
    el('sessionLoadMore').hidden = visibleCount >= selected.length;
    el('sessionRefresh').disabled = loading;
    const supported = window.InnoChargeExportSupported === true;
    el('sessionExport').disabled = !complete || !selected.length || !supported || exportBusy;
    el('sessionExport').title = supported ? '' : 'File export is available in the updated mobile app.';
    for (const id of ['appSessionSort', 'sessionSortDirection', 'appSessionFrom', 'appSessionTo', 'appSessionUser', 'appSessionStatus', 'sessionClearFilters']) el(id).disabled = loading;
  }

  function cancelPending(reason) {
    if (!pending) return;
    clearTimeout(pending.timer);
    pending.reject(new Error(reason));
    pending = null;
  }

  function page(offset) {
    return new Promise((resolve, reject) => {
      const id = ++requestId;
      const timer = setTimeout(() => {
        pending = null;
        reject(new Error('Sessions unavailable. Check the connection and MAIN firmware, then Refresh.'));
      }, 8000);
      pending = { id, timer, resolve, reject };
      if (!sendMessage({ action: 'requestAppSessions', offset, requestId: id })) {
        clearTimeout(timer);
        pending = null;
        reject(new Error('No connection to the wallbox.'));
      }
    });
  }

  async function refresh() {
    const current = ++generation;
    cancelPending('Refresh cancelled.');
    complete = false;
    loading = true;
    visibleCount = 20;
    message('');
    try {
      for (let attempt = 0; attempt < 3; ++attempt) {
        sessions = [];
        let revision = null;
        let total = null;
        let offset = 0;
        const ids = new Set();
        let changed = false;
        do {
          if (current !== generation) return;
          const response = await page(offset);
          if (current !== generation) return;
          if (!Number.isInteger(response.sessionRevision) || !Number.isInteger(response.sessionTotal) || response.sessionTotal < 0 || response.sessionTotal > 10000 || response.sessionOffset !== offset || !Array.isArray(response.sessions)) throw new Error('Invalid session history received.');
          if (revision === null) { revision = response.sessionRevision; total = response.sessionTotal; }
          if (revision !== response.sessionRevision || total !== response.sessionTotal) { changed = true; break; }
          wallboxName = typeof response.wallboxName === 'string' ? response.wallboxName : 'InnoCharge';
          for (const session of response.sessions) {
            if (!session || !Number.isInteger(session.transactionId) || ids.has(session.transactionId)) throw new Error('History changed. Please Refresh.');
            ids.add(session.transactionId);
            sessions.push(session);
          }
          offset += response.sessions.length;
          if (offset > total || (!response.sessions.length && offset < total)) throw new Error('Incomplete history received. Please Refresh.');
          el('appSessionCount').textContent = 'Loading sessions: ' + offset + ' / ' + total;
          render();
          if (offset < total) await new Promise(resolve => setTimeout(resolve, 150));
        } while (offset < total);
        if (!changed) {
          complete = true;
          message('');
          break;
        }
        if (attempt === 2) throw new Error('History is changing. Please Refresh in a moment.');
      }
    } catch (error) {
      if (current === generation) message(error.message || 'Could not load the sessions.', true);
    } finally {
      if (current === generation) { loading = false; render(); }
    }
  }

  function showSessions(show) {
    el('chargingPage').hidden = show;
    el('sessionsPage').hidden = !show;
    el('chargingTab').setAttribute('aria-selected', String(!show));
    el('sessionsTab').setAttribute('aria-selected', String(show));
    if (show && !complete && !loading) refresh();
    if (!show && loading) {
      ++generation;
      cancelPending('Refresh cancelled.');
      loading = false;
      render();
    }
    window.scrollTo(0, 0);
  }

  window.addEventListener('innocharge-sessions-page', event => {
    const data = event.detail;
    if (!pending || data.requestId !== pending.id) return;
    const request = pending;
    pending = null;
    clearTimeout(request.timer);
    if (data.ok === true) request.resolve(data);
    else request.reject(new Error(data.message || 'Could not load the sessions.'));
  });
  window.addEventListener('innocharge-export-ready', render);
  window.addEventListener('innocharge-connection', event => {
    if (!event.detail) cancelPending('Connection interrupted. Please Refresh.');
    else if (!el('sessionsPage').hidden && !complete && !loading) refresh();
  });
  window.addEventListener('innocharge-export-result', event => {
    exportBusy = false;
    message(event.detail.message || '', event.detail.ok === false);
    render();
  });
  window.addEventListener('pagehide', () => { ++generation; cancelPending('Page closed.'); });
  el('sessionsTab').addEventListener('click', () => showSessions(true));
  el('chargingTab').addEventListener('click', () => showSessions(false));
  el('sessionRefresh').addEventListener('click', refresh);
  el('sessionLoadMore').addEventListener('click', () => { visibleCount += 20; render(); });
  for (const id of ['appSessionSort', 'appSessionFrom', 'appSessionTo', 'appSessionUser', 'appSessionStatus']) el(id).addEventListener('input', () => { visibleCount = 20; render(); });
  el('sessionSortDirection').addEventListener('click', () => {
    descending = !descending;
    el('sessionSortDirection').textContent = descending ? '↓' : '↑';
    el('sessionSortDirection').setAttribute('aria-label', descending ? 'Sort descending' : 'Sort ascending');
    render();
  });
  el('sessionClearFilters').addEventListener('click', () => {
    for (const id of ['appSessionFrom', 'appSessionTo', 'appSessionUser']) el(id).value = '';
    el('appSessionStatus').value = 'all';
    visibleCount = 20;
    render();
  });
  el('sessionExport').addEventListener('click', () => {
    if (!complete || exportBusy || window.InnoChargeExportSupported !== true) return;
    const selected = selection();
    if (!selected.length) return;
    const filters = [
      el('appSessionFrom').value && 'From ' + el('appSessionFrom').value,
      el('appSessionTo').value && 'To ' + el('appSessionTo').value,
      el('appSessionUser').value.trim() && 'User / RFID: ' + el('appSessionUser').value.trim(),
      el('appSessionStatus').value !== 'all' && 'Status: ' + el('appSessionStatus').value
    ].filter(Boolean);
    const sort = el('appSessionSort').selectedOptions[0].textContent + (descending ? ' descending' : ' ascending');
    exportBusy = true;
    render();
    window.InnoChargeHost.postMessage(JSON.stringify({
      action: 'exportSessions', wallboxName,
      selection: (filters.join(' | ') || 'All sessions') + ' | Sort: ' + sort,
      sessions: selected
    }));
  });
  render();
})();
