// interfaces.js

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
    Socket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'rfid' }));
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
    Socket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'rfid'  }));
    Socket.close();
  };
}



//--------------------------------DOMContentLoaded-----------------------------------//
//--------------------------------DOMContentLoaded-----------------------------------//
//--------------------------------DOMContentLoaded-----------------------------------//
// Event listener for buttons and page load
document.addEventListener('DOMContentLoaded', function() {
  // Initialize the WebSocket connection
  initWebSocket();



});




  