// index.js

// Initialize WebSocket variable
var Socket;
// Flag to check if input fields have been initialized
var inputsInitialized = false;


// Function to initialize the WebSocket connection
function initWebSocket() {
  // Create a new WebSocket connection to the specified endpoint
  Socket = new WebSocket('ws://' + window.location.hostname + ':81/');

  // Event handler when the WebSocket connection is opened
  Socket.onopen = function() {
    console.log('WebSocket connection opened');
    // Send a message to subscribe to updates
    Socket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'index' }));
  };

  // Event handler for incoming messages from the server
  Socket.onmessage = function(event) {
    processCommand(event);
  };

  // Event handler when the WebSocket connection is closed
  Socket.onclose = function(event) {
    console.log('WebSocket connection closed:', event);
  };

  // Event handler for any errors that occur with the WebSocket
  Socket.onerror = function(error) {
    console.log('WebSocket error:', error);
  };

  // Handle page unload to unsubscribe from updates and close the WebSocket
  window.onbeforeunload = function() {
    Socket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'index'  }));
    Socket.close();
  };
}

// Function to process incoming WebSocket messages
function processCommand(event) {
  console.log('Received data:', event.data); // Display received data
  var obj = JSON.parse(event.data);

  // Initialize only once
  if (!inputsInitialized) {
    if (obj.targetChargeCurrent !== undefined && document.getElementById('setChargeCurrent') !== null) {
      document.getElementById('setChargeCurrent').value = obj.targetChargeCurrent;
      inputsInitialized = true; // Set the flag to true after initializing
    }
    if (obj.targetChargePower !== undefined && document.getElementById('setChargePower') !== null) {
      document.getElementById('setChargePower').value = obj.targetChargePower;
      inputsInitialized = true; // Set the flag to true after initializing
    }
    if (obj.cpRelayState !== undefined && document.getElementById('toggleCpRelay') !== null) {
      document.getElementById('toggleCpRelay').checked = obj.cpRelayState;
      inputsInitialized = true; // Set the flag to true after initializing
    }
  }

  // Update if the element exists on the page
  if (obj.cpState !== undefined && document.getElementById('cpState') !== null) {
    document.getElementById('cpState').innerHTML = obj.cpState;
  }
  if (obj.cpVoltage !== undefined && document.getElementById('cpVoltage') !== null) {
    document.getElementById('cpVoltage').innerHTML = obj.cpVoltage;
  }
  if (obj.espTemp !== undefined && document.getElementById('espTemp') !== null) {
    document.getElementById('espTemp').innerHTML = obj.espTemp;
  }
  if (obj.targetChargeCurrent !== undefined && document.getElementById('targetChargeCurrent') !== null) {
    document.getElementById('targetChargeCurrent').innerHTML = obj.targetChargeCurrent;
  }
  if (obj.targetChargePower !== undefined && document.getElementById('targetChargePower') !== null) {
    document.getElementById('targetChargePower').innerHTML = obj.targetChargePower;
  }

  // Update cpRelayState if the element exists on the page
  if (obj.cpRelayState !== undefined && document.getElementById('BTN_SET_RELAY') !== null) {
    var relayState = obj.cpRelayState ? 'ON' : 'OFF';
    document.getElementById('BTN_SET_RELAY').textContent = relayState;
  }
}

//--------------------------------DOMContentLoaded-----------------------------------//
//--------------------------------DOMContentLoaded-----------------------------------//
//--------------------------------DOMContentLoaded-----------------------------------//
// Event listener for buttons and page load
document.addEventListener('DOMContentLoaded', function() {
  // Initialize the WebSocket connection
  initWebSocket();

  // Set up event listeners for inputs
  document.getElementById('setChargeCurrent')?.addEventListener('keydown', function(event) {
    if (event.key === 'Enter') {
      set_charge_current();
    }
  });
  document.getElementById('setChargePower')?.addEventListener('keydown', function(event) {
    if (event.key === 'Enter') {
      set_charge_power();
    }
  });

  // Set up event listeners for buttons
  document.getElementById('toggleCpRelay')?.addEventListener('change', function(event) {
    toggle_cp_relay();
  });

  document.getElementById('BTN_GO_TO_SETTINGS')?.addEventListener('click', go_to_settings);
  document.getElementById('BTN_SET_RELAY')?.addEventListener('click', toggle_cp_relay);
});

// Function to handle toggling of CP OFF and ON
function toggle_cp_relay() {
  const isChecked = document.getElementById('toggleCpRelay').checked;
  const data = {
    action: 'setCpRelayState',
    state: isChecked 
  };
  Socket.send(JSON.stringify(data));
  console.log('Sending CP-Relay values to server:', data);
}

// Function to set the charging current
function set_charge_current() {
  var currentValue = parseFloat(document.getElementById('setChargeCurrent').value);
  // Check if currentValue is between 0 and 32
  if (currentValue < 0 || currentValue > 32) {
    alert('Charging current must be between 0 and 32 amperes.');
    return;
  }
  var data = {
    action: 'setChargeParameters',
    current: currentValue,
  };
  Socket.send(JSON.stringify(data));
  console.log('Sending Charge values to server:', data);
}

// Function to set the charging power
function set_charge_power() {
  var powerValue = parseFloat(document.getElementById('setChargePower').value);  
  // Check if powerValue is between 0 and 22
  if (powerValue < 0 || powerValue > 22) {
    alert('Charging power must be between 0 and 22 kW.');
    return;
  }
  var data = {
    action: 'setChargeParameters',
    power: powerValue,
  };
  Socket.send(JSON.stringify(data));
  console.log('Sending Charge values to server:', data);
}

// Function to navigate to the settings page
function go_to_settings() {
  // Implement the logic to navigate to the settings page
  window.location.href = 'settings.html';
}

  