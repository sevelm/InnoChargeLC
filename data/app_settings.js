(() => {
  'use strict';
  const el = id => document.getElementById(id);
  const overlay = el('appSettings');
  const input = el('appDeviceDateTime');
  let timer = null;
  let clockTimer = null;
  let ready = false;
  let saving = false;
  let inputDirty = false;
  let deviceEpoch = null;
  let receivedAt = 0;

  function message(text, error = false) {
    el('appTimeMessage').textContent = text;
    el('appTimeMessage').classList.toggle('error', error);
  }

  function busy(value) {
    saving = value;
    el('appSaveTime').disabled = !ready || value;
    el('appUsePhoneTime').disabled = !ready || value;
    input.disabled = value;
  }

  function drawClock() {
    el('appDeviceTime').textContent = deviceEpoch === null ? '–' :
      new Date((deviceEpoch + (performance.now() - receivedAt) / 1000) * 1000)
        .toLocaleString('de-AT', { timeZone: 'Europe/Vienna' });
  }

  function requestTime() {
    clearTimeout(timer);
    ready = false;
    busy(false);
    if (!sendMessage({ action: 'requestAppTime' })) {
      message('Connection lost. Please try again.', true);
      return;
    }
    message('Loading...');
    timer = setTimeout(() => message('Firmware update required for time settings.', true), 8000);
  }

  function close() {
    overlay.hidden = true;
    document.body.classList.remove('settings-open');
    el('nativeMenu').setAttribute('aria-expanded', 'false');
    clearInterval(clockTimer);
    el('nativeMenu').focus();
  }

  function open() {
    overlay.hidden = false;
    document.body.classList.add('settings-open');
    el('nativeMenu').setAttribute('aria-expanded', 'true');
    el('appSwitchWallbox').hidden = !window.InnoChargeHost;
    inputDirty = false;
    if (!saving) requestTime();
    drawClock();
    clearInterval(clockTimer);
    clockTimer = setInterval(drawClock, 1000);
    el('closeAppSettings').focus();
  }

  function save(usePhoneTime) {
    if (!ready || saving) return;
    const payload = { action: 'setDeviceTime' };
    if (usePhoneTime) payload.epoch = Math.floor(Date.now() / 1000);
    else {
      if (!input.reportValidity()) return;
      payload.localDateTime = input.value.length === 16 ? input.value + ':00' : input.value;
    }
    if (!sendMessage(payload)) {
      ready = false;
      busy(false);
      message('Connection lost. Please try again.', true);
      return;
    }
    if (usePhoneTime) inputDirty = false;
    busy(true);
    message('Saving...');
    clearTimeout(timer);
    timer = setTimeout(() => { busy(false); message('No response. Please try again.', true); }, 8000);
  }

  window.addEventListener('innocharge-time', event => {
    const data = event.detail;
    if (data.type === 'appTime') {
      if (!Number.isFinite(data.epoch) || typeof data.localDateTime !== 'string') return;
      clearTimeout(timer);
      ready = true;
      deviceEpoch = data.epoch;
      receivedAt = performance.now();
      if (!inputDirty) input.value = data.localDateTime;
      drawClock();
      busy(false);
      // Preserve a successful save acknowledgement while refreshing the clock.
      if (el('appTimeMessage').textContent === 'Loading...') message('');
    } else if (data.type === 'deviceTimeStatus') {
      if (!saving) return;
      clearTimeout(timer);
      busy(false);
      message(data.message || (data.ok ? 'Time updated.' : 'Failed to set time.'), !data.ok);
      if (data.ok) sendMessage({ action: 'requestAppTime' });
    }
  });
  window.addEventListener('innocharge-connection', event => {
    if (!event.detail) {
      clearTimeout(timer);
      ready = false;
      busy(false);
      if (!overlay.hidden) message('Connection lost. Please try again.', true);
    } else if (!overlay.hidden) requestTime();
  });
  window.addEventListener('pagehide', () => { clearTimeout(timer); clearInterval(clockTimer); });
  el('nativeMenu').addEventListener('click', open);
  el('closeAppSettings').addEventListener('click', close);
  overlay.addEventListener('click', event => { if (event.target === overlay) close(); });
  document.addEventListener('keydown', event => {
    if (overlay.hidden) return;
    if (event.key === 'Escape') { event.preventDefault(); close(); }
    if (event.key !== 'Tab') return;
    const items = [...overlay.querySelectorAll('button:not(:disabled), input:not(:disabled)')].filter(item => !item.hidden);
    const first = items[0], last = items[items.length - 1];
    if (!overlay.contains(document.activeElement)) { event.preventDefault(); first.focus(); }
    else if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
    else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
  });
  input.addEventListener('input', () => { inputDirty = true; });
  el('appSaveTime').addEventListener('click', () => save(false));
  el('appUsePhoneTime').addEventListener('click', () => save(true));
  el('appReload').addEventListener('click', () => {
    if (typeof window.InnoChargeDemoReset === 'function') {
      close();
      window.InnoChargeDemoReset();
    } else window.location.reload();
  });
  el('appSwitchWallbox').addEventListener('click', () => {
    close();
    window.InnoChargeHost?.postMessage(window.InnoChargeNavigationSupported ? 'back' : 'menu');
  });
})();
