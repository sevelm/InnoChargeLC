(() => {
  'use strict';
  let state = 'C';
  let maximum = 11;
  let power = 5.5;
  let phase = 'Three-phase';
  const sockets = new Set();
  const visuals = {
    A: ['Ready to connect', '#0000ff', 'solid'],
    B: ['Vehicle connected', '#ffc300', 'solid'],
    C: ['Charging', '#00ff00', 'charge'],
    D: ['Charging - ventilation', '#00ff00', 'charge'],
    E: ['Charging error', '#ff0000', 'solid'],
    F: ['Charger fault', '#ff0000', 'blink'],
    paused: ['Charging paused', '#ff00ff', 'solid'],
    authorization: ['Waiting for authorization', '#ffc300', 'wave']
  };

  function snapshot() {
    const paused = power === 0 && ['C', 'D'].includes(state);
    const [label, color, animation] = visuals[paused ? 'paused' : state];
    return {
      wallboxName: 'InnoCharge',
      cpState: 'State ' + ({paused: 'B', authorization: 'B'}[state] || state),
      phaseMode: phase,
      vehicleConnected: !['A', 'E', 'F'].includes(state),
      chargingActive: ['C', 'D'].includes(state) && !paused,
      targetChargePower: power === 0 ? 0 : Math.max(1.4, power),
      maxChargePower: maximum,
      ledStatus: {
        label, color, waveColor: animation === 'wave' ? '#ffffff' : color,
        animation, periodMs: animation === 'blink' ? 500 : 2800
      }
    };
  }

  function publish() {
    const data = JSON.stringify(snapshot());
    for (const socket of sockets) {
      if (socket.readyState === 1 && socket.subscribed) {
        socket.onmessage?.({data});
      }
    }
    const connection = document.getElementById('connection');
    if (connection) connection.textContent = 'simulation';
  }

  // Never instantiate or retain the platform WebSocket. CSP also denies network I/O.
  class DemoSocket {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;
    readyState = 0;
    subscribed = false;

    constructor() {
      sockets.add(this);
      setTimeout(() => {
        if (this.readyState !== 0) return;
        this.readyState = 1;
        this.onopen?.();
      }, 0);
    }

    send(text) {
      if (this.readyState !== 1) return;
      const command = JSON.parse(text);
      if (command.action === 'subscribeUpdates') this.subscribed = true;
      if (command.action === 'unsubscribeUpdates') this.subscribed = false;
      if (command.action === 'setChargeParameters' &&
          Number.isFinite(command.power) && command.power >= 0 && command.power <= maximum) {
        power = Math.round(command.power * 10) / 10;
      }
      setTimeout(publish, 0);
    }

    close() {
      if (this.readyState === 3) return;
      this.readyState = 3;
      sockets.delete(this);
      this.onclose?.();
    }
  }
  Object.defineProperty(window, 'WebSocket', {value: DemoSocket, writable: false, configurable: false});

  let heartbeat;
  window.addEventListener('DOMContentLoaded', () => {
    const style = document.createElement('style');
    style.textContent = `
      .demo-settings { margin: 0 0 16px; padding: 12px 0; border: 0; border-bottom: 1px solid var(--line); min-width: 0; }
      .demo-settings legend { padding: 0; color: var(--blue); font-size: 13px; font-weight: 700; }
      .demo-options { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); gap: 10px; }
      .demo-options label:first-child { grid-column: 1 / -1; }
      .demo-options label { display: grid; gap: 5px; min-width: 0; font-size: 12px; color: var(--muted); }
      .demo-options select { width: 100%; min-width: 0; height: 44px; padding: 0 8px; border: 1px solid var(--line); border-radius: 6px; background: var(--surface); color: var(--text); font: inherit; font-size: 14px; }
    `;
    document.head.append(style);
    const settings = document.createElement('fieldset');
    settings.className = 'demo-settings';
    settings.innerHTML = `
      <legend>DEMO / Simulated data</legend>
      <div class="demo-options">
        <label>Simulated state<select id="demoState">
          <option value="A">A - Ready to connect</option>
          <option value="B">B - Vehicle connected</option>
          <option value="C" selected>C - Charging</option>
          <option value="D">D - Charging with ventilation</option>
          <option value="paused">Charging paused</option>
          <option value="authorization">Waiting for authorization</option>
          <option value="E">E - Charging error</option>
          <option value="F">F - Charger fault</option>
        </select></label>
        <label>Wallbox limit<select id="demoMaximum"><option value="11">11 kW</option><option value="22">22 kW</option></select></label>
        <label>Phase mode<select id="demoPhase"><option>Three-phase</option><option>Single-phase</option></select></label>
      </div>
    `;
    document.querySelector('header').after(settings);
    document.getElementById('demoState').addEventListener('change', event => {
      state = event.target.value;
      publish();
    });
    document.getElementById('demoMaximum').addEventListener('change', event => {
      maximum = Number(event.target.value);
      power = Math.min(power, maximum);
      publish();
    });
    document.getElementById('demoPhase').addEventListener('change', event => {
      phase = event.target.value;
      publish();
    });
    heartbeat = setInterval(publish, 4000);
  });
  window.addEventListener('pagehide', () => clearInterval(heartbeat));
  window.addEventListener('pageshow', event => {
    if (event.persisted) heartbeat = setInterval(publish, 4000);
  });
})();
