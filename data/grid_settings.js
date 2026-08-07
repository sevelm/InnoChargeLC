let gridSocket = null;
let gridUnlocked = false;
let saveStatusTimer = null;

const GRID_DEFAULTS = {
  profile: 0,
  nominalVoltage: 230,
  undervoltagePercent: 80,
  undervoltageTripSeconds: 3,
  reconnectDelaySeconds: 60
};

function initGridSocket() {
  gridSocket = new WebSocket('ws://' + location.hostname + ':81/');

  gridSocket.onopen = () => {
    setGridAuthStatus('Enter PIN', '#333');
  };

  gridSocket.onmessage = ev => processGridMessage(ev.data);
  gridSocket.onclose = () => {
    gridUnlocked = false;
    showGridLocked();
  };
  gridSocket.onerror = () => setGridAuthStatus('WebSocket error', 'red');

  window.addEventListener('beforeunload', () => {
    if (gridSocket?.readyState === WebSocket.OPEN) {
      gridSocket.send(JSON.stringify({ action: 'lockGridSettings' }));
      gridSocket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'grid_settings' }));
      gridSocket.close();
    }
  });
}

function processGridMessage(text) {
  let data;
  try { data = JSON.parse(text); } catch { return; }

  if (data.type === 'gridSettingsAuth') {
    if (!data.ok) {
      gridUnlocked = false;
      showGridLocked();
      setGridAuthStatus(data.message || 'Wrong PIN', 'red');
      return;
    }
    gridUnlocked = true;
    showGridUnlocked();
    gridSocket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'grid_settings' }));
    return;
  }

  if (data.type === 'gridSettingsSaveStatus') {
    setGridSaveStatus(data.message || '', data.ok ? 'green' : 'red');
    return;
  }

  if (data.gridProfile !== undefined) {
    updateGridSettings(data);
    updateGridMeterValues(data);
  }
}

function showGridUnlocked() {
  document.getElementById('gridPinPanel').style.display = 'none';
  document.getElementById('gridSettingsPanel').style.display = 'block';
  document.getElementById('gridPin').value = '';
  updateCalculatedTripVoltage();
}

function showGridLocked() {
  document.getElementById('gridPinPanel').style.display = 'block';
  document.getElementById('gridSettingsPanel').style.display = 'none';
}

function setGridAuthStatus(message, color) {
  const el = document.getElementById('gridAuthStatus');
  if (!el) return;
  el.textContent = message;
  el.style.color = color || '#333';
}

function setGridSaveStatus(message, color) {
  const el = document.getElementById('gridSaveStatus');
  if (!el) return;
  el.textContent = message;
  el.style.color = color || '#333';
  if (saveStatusTimer) clearTimeout(saveStatusTimer);
  if (message) {
    saveStatusTimer = setTimeout(() => {
      el.textContent = '';
    }, 3000);
  }
}

function unlockGridSettings() {
  const pin = document.getElementById('gridPin').value.trim();
  if (!pin) {
    setGridAuthStatus('PIN required', 'red');
    return;
  }
  if (gridSocket?.readyState !== WebSocket.OPEN) {
    setGridAuthStatus('WebSocket not connected', 'red');
    return;
  }
  setGridAuthStatus('Checking...', '#333');
  gridSocket.send(JSON.stringify({ action: 'unlockGridSettings', pin }));
}

const GRID_SAVE_FIELDS = {
  gridProfile: { key: 'profile', min: 0, max: 1, fallback: 0 },
  gridUndervoltagePercent: { key: 'undervoltagePercent', min: 70, max: 90, fallback: 80 },
  gridUndervoltageTripSeconds: { key: 'undervoltageTripSeconds', min: 1, max: 30, fallback: 3 },
  gridReconnectDelaySeconds: { key: 'reconnectDelaySeconds', min: 0, max: 300, fallback: 60 }
};

function readNumber(id, fallback) {
  const el = document.getElementById(id);
  let value = parseInt(el.value, 10);
  if (Number.isNaN(value)) value = fallback;
  return value;
}

function clampField(id, min, max, fallback) {
  const el = document.getElementById(id);
  let value = readNumber(id, fallback);
  if (value < min) value = min;
  if (value > max) value = max;
  el.value = value;
  return value;
}

function updateCalculatedTripVoltage() {
  const nominal = readNumber('gridNominalVoltage', 230);
  const percent = readNumber('gridUndervoltagePercent', 80);
  document.getElementById('gridUndervoltageVolts').textContent = (nominal * percent / 100).toFixed(1) + ' V';
}

function updateReconnectDelayRemaining(seconds) {
  const el = document.getElementById('gridReconnectDelayRemainingSeconds');
  if (!el) return;
  el.textContent = seconds ?? 0;
}

function updateGridProtectionStatus(data) {
  const status = document.getElementById('gridProtectionStatus');
  if (!status) return;

  const value = data.gridProtectionStatus ?? 0;
  let text = 'Idle';

  if (value === 1) {
    text = `Reconnect delay, ${data.gridReconnectDelayRemainingSeconds ?? 0} s remaining`;
  } else if (value === 2) {
    text = `Ramp active, limit ${(data.gridReconnectRampLimitPower ?? 0) / 10} kW`;
  } else if (value === 3) {
    text = `Phase imbalance detected, reducing in ${data.gridPhaseImbalanceLimitRemainingSeconds ?? 0} s`;
  } else if (value === 4) {
    text = 'Phase imbalance limit active, limited to 16 A';
  }

  status.textContent = `${value} - ${text}`;
  status.style.color = value === 0 ? '#333' : 'red';
}

function updateGridMeterWarning(energyMeterEnabled) {
  const warning = document.getElementById('gridMeterWarning');
  const profile = document.getElementById('gridProfile');
  if (!warning || !profile) return;
  const meterEnabled = energyMeterEnabled === true;
  warning.style.display = Number(profile.value) === 1 && !meterEnabled ? 'table-row' : 'none';
}

function setGridDefaults() {
  document.getElementById('gridProfile').value = GRID_DEFAULTS.profile;
  document.getElementById('gridNominalVoltage').value = GRID_DEFAULTS.nominalVoltage;
  document.getElementById('gridUndervoltagePercent').value = GRID_DEFAULTS.undervoltagePercent;
  document.getElementById('gridUndervoltageTripSeconds').value = GRID_DEFAULTS.undervoltageTripSeconds;
  document.getElementById('gridReconnectDelaySeconds').value = GRID_DEFAULTS.reconnectDelaySeconds;
  document.getElementById('gridNominalVoltage').readOnly = true;
  updateCalculatedTripVoltage();
  updateGridMeterWarning(false);
}

function setValueIfIdle(id, value) {
  const el = document.getElementById(id);
  if (!el || document.activeElement === el) return;
  el.value = value;
}

function updateGridSettings(data) {
  setValueIfIdle('gridProfile', data.gridProfile ?? 0);
  setValueIfIdle('gridNominalVoltage', data.gridNominalVoltage ?? 230);
  setValueIfIdle('gridUndervoltagePercent', data.gridUndervoltagePercent ?? 80);
  setValueIfIdle('gridUndervoltageTripSeconds', data.gridUndervoltageTripSeconds ?? 3);
  setValueIfIdle('gridReconnectDelaySeconds', data.gridReconnectDelaySeconds ?? 60);
  document.getElementById('gridNominalVoltage').readOnly = true;
  updateGridMeterWarning(data.energyMeterState === true);
  updateReconnectDelayRemaining(data.gridReconnectDelayRemainingSeconds);
  updateGridProtectionStatus(data);
  if (document.activeElement?.id !== 'gridUndervoltagePercent') {
    updateCalculatedTripVoltage();
  }
}

function energyMeterName(type) {
  if (type === 1) return 'YT-DTS353F-2';
  return 'Eastron SDM630';
}

function voltageText(rawTenths) {
  if (typeof rawTenths !== 'number' || rawTenths <= 0) return '';
  return (rawTenths / 10).toFixed(1);
}

function frequencyText(rawHundredths) {
  if (typeof rawHundredths !== 'number' || rawHundredths <= 0) return '';
  return (rawHundredths / 100).toFixed(2);
}

function updateGridMeterValues(data) {
  document.getElementById('gridEnergyMeterInfo').textContent =
    `${energyMeterName(data.energyMeterType)} / Modbus ${data.energyMeterModbusId ?? ''}`;

  const state = document.getElementById('gridEnergyMeterState');
  if (data.energyMeterState && !data.energyMeterError) {
    state.textContent = 'Ready';
    state.className = 'status ready';
  } else if (data.energyMeterError) {
    state.textContent = 'Error';
    state.className = 'status error';
  } else {
    state.textContent = 'Disabled';
    state.className = 'status off';
  }

  document.getElementById('gridL1Voltage').textContent = voltageText(data.l1Voltage);
  document.getElementById('gridL2Voltage').textContent = voltageText(data.l2Voltage);
  document.getElementById('gridL3Voltage').textContent = voltageText(data.l3Voltage);
  document.getElementById('gridFrequency').textContent = frequencyText(data.frequency);
  document.getElementById('gridMinVoltage').textContent = voltageText(data.minGridVoltage);
}

function saveGridField(id) {
  if (!gridUnlocked || gridSocket?.readyState !== WebSocket.OPEN) {
    setGridSaveStatus('PIN required', 'red');
    return;
  }

  const field = GRID_SAVE_FIELDS[id];
  if (!field) return;

  const value = clampField(id, field.min, field.max, field.fallback);
  updateCalculatedTripVoltage();
  updateGridMeterWarning(document.getElementById('gridEnergyMeterState')?.textContent === 'Ready');
  setGridSaveStatus('Saving...', '#333');
  gridSocket.send(JSON.stringify({ action: 'saveGridSetting', key: field.key, value }));
}

function applyTorDefaults() {
  document.getElementById('gridUndervoltagePercent').value = GRID_DEFAULTS.undervoltagePercent;
  document.getElementById('gridUndervoltageTripSeconds').value = GRID_DEFAULTS.undervoltageTripSeconds;
  document.getElementById('gridReconnectDelaySeconds').value = GRID_DEFAULTS.reconnectDelaySeconds;
  updateCalculatedTripVoltage();

  saveGridField('gridUndervoltagePercent');
  saveGridField('gridUndervoltageTripSeconds');
  saveGridField('gridReconnectDelaySeconds');
}

document.addEventListener('DOMContentLoaded', () => {
  setGridDefaults();
  initGridSocket();
  showGridLocked();

  document.getElementById('unlockGridSettings').addEventListener('click', unlockGridSettings);
  document.getElementById('gridPin').addEventListener('keydown', ev => {
    if (ev.key === 'Enter') unlockGridSettings();
  });

  ['gridProfile', 'gridUndervoltagePercent', 'gridUndervoltageTripSeconds', 'gridReconnectDelaySeconds'].forEach(id => {
    const field = document.getElementById(id);
    field.addEventListener('change', () => {
      saveGridField(id);
      if (id === 'gridProfile' && Number(field.value) === 1) {
        applyTorDefaults();
      }
    });
    field.addEventListener('keydown', ev => {
      if (ev.key === 'Enter') {
        ev.preventDefault();
        saveGridField(id);
      }
    });
  });

  ['gridUndervoltagePercent', 'gridNominalVoltage'].forEach(id => {
    document.getElementById(id).addEventListener('input', updateCalculatedTripVoltage);
  });
});
