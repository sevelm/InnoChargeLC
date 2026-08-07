// common.js

// Global variable to track connection status
let isConnectionActive = true; // Assume connection is active by default

function getCookieValue(name) {
  const prefix = name + '=';
  const parts = document.cookie.split(';');
  for (let i = 0; i < parts.length; i++) {
    const part = parts[i].trim();
    if (part.indexOf(prefix) === 0) return part.substring(prefix.length);
  }
  return '';
}

// Fuegt bestehenden JSON-WebSocket-Nachrichten automatisch die Login-Session hinzu.
(function installWebSocketSessionInjector() {
  const originalSend = WebSocket.prototype.send;
  WebSocket.prototype.send = function(data) {
    if (typeof data !== 'string' || data.charAt(0) !== '{') {
      return originalSend.call(this, data);
    }

    try {
      const message = JSON.parse(data);
      if (!message.session) {
        message.session = getCookieValue('ICSESSION');
      }
      return originalSend.call(this, JSON.stringify(message));
    } catch (error) {
      return originalSend.call(this, data);
    }
  };
})();

// Function to load the navigation bar
function loadNavbar() {
    // Load the navigation bar content from 'navbar.html'
    fetch('navbar.html')
        .then(response => response.text())
        .then(data => {
            document.getElementById('navbar').innerHTML = data;
        })
        .catch(error => console.error('Error loading navigation:', error));
}

// Function to update the connection status
function updateConnectionStatus(isConnected) {
  const statusElement = document.getElementById("connection-status");
  isConnectionActive = isConnected; // Update the global variable
  if (statusElement) {
      statusElement.style.display = isConnected ? "none" : "block";
      if (!isConnected) {
          console.warn("No connection!"); // Log only if connection is lost
      }
  }
}

function checkNetworkConnection() {
  setInterval(() => {
      // Versuche, die Verbindung zum Server zu testen
      fetch(window.location.origin)
          .then(() => {
              updateConnectionStatus(true); // Verbindung aktiv
          })
          .catch(() => {
              updateConnectionStatus(false); // Verbindung verloren
          });
  }, 5000); // Alle 5 Sekunden prüfen
}

// Common DOMContentLoaded event listener
document.addEventListener('DOMContentLoaded', function () {
    loadNavbar();
    // Initial connection status check
    updateConnectionStatus(true); // Assume connected initially
    checkNetworkConnection();
});
