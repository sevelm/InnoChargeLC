var Socket;

function loadNavbar() {
    fetch('navbar.html')
      .then(response => response.text())
      .then(data => {
        document.getElementById('navbar').innerHTML = data;
      })
      .catch(error => console.error('Error loading navigation:', error));
  }

function initWebSocket() {
    Socket = new WebSocket('ws://' + window.location.hostname + ':81/');

    Socket.onopen = function() {
        console.log('WebSocket connection opened');
    };

    Socket.onmessage = function(event) {
        processCommand(event);
    };

    Socket.onclose = function(event) {
        console.log('WebSocket connection closed:', event);
    };

    Socket.onerror = function(error) {
        console.log('WebSocket error:', error);
    };
}

function processCommand(event) {
    var obj = JSON.parse(event.data);

    if (obj.cpState !== undefined && document.getElementById('cpState') !== null) {
        document.getElementById('cpState').innerHTML = obj.cpState;
    }

    if (obj.cpRelayState !== undefined && document.getElementById('BTN_SET_RELAY') !== null) {
        var relayState = obj.cpRelayState ? 'EIN' : 'AUS';
        document.getElementById('BTN_SET_RELAY').textContent = relayState;
    }

    // Log data for debugging purposes
    console.log('Received data:', event.data);
}

function set_charge_current() {
    var msg = {
        setChargeCurrent: document.getElementById('setChargeCurrent').value
    };
    Socket.send(JSON.stringify(msg));
}

function toggle_cp_relay() {
    var button = document.getElementById('BTN_SET_RELAY');
    var newState = (button.textContent === 'AUS') ? 'EIN' : 'AUS';
    var msg = {
        setCpRelay: newState === 'EIN' ? true : false
    };
    Socket.send(JSON.stringify(msg));
}

function go_to_settings() {
    window.location.href = 'settings.html';
}

window.onload = function(event) {
    loadNavbar();
    initWebSocket();
}
