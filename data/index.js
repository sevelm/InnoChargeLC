// index.js

// Initialize WebSocket variable
var Socket;

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

  // Update cpState if the element exists on the page
  if (obj.cpState !== undefined && document.getElementById('cpState') !== null) {
    document.getElementById('cpState').innerHTML = obj.cpState;
  }

  // Update cpRelayState if the element exists on the page
  if (obj.cpRelayState !== undefined && document.getElementById('BTN_SET_RELAY') !== null) {
    var relayState = obj.cpRelayState ? 'EIN' : 'AUS';
    document.getElementById('BTN_SET_RELAY').textContent = relayState;
  }
}

// Event listener for buttons and page load
document.addEventListener('DOMContentLoaded', function() {
  // Initialize the WebSocket connection
  initWebSocket();

  // Set up event listeners for buttons
  document.getElementById('BTN_SET_CURRENT')?.addEventListener('click', set_charge_current);
  document.getElementById('BTN_GO_TO_SETTINGS')?.addEventListener('click', go_to_settings);
  document.getElementById('BTN_SET_RELAY')?.addEventListener('click', toggle_cp_relay);
});

// Function to set the charging current
function set_charge_current() {
  // Implement the logic to set the charging current
}

// Function to navigate to the settings page
function go_to_settings() {
  // Implement the logic to navigate to the settings page
  window.location.href = 'settings.html';
}

// Function to toggle the CP relay state
function toggle_cp_relay() {
  // Implement the logic to toggle the CP relay
  // For example, send a WebSocket message to the server
  Socket.send(JSON.stringify({ action: 'toggleCpRelay' }));
}


  