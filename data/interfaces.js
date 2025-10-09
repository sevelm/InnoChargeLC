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
    Socket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'interfaces' }));
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
    Socket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'interfaces'  }));
    Socket.close();
  };
}

// Function to process incoming WebSocket messages
function processCommand(event) {
  console.log('Received data:', event.data); // Display received data
  var obj = JSON.parse(event.data);

  // Initialize only once
  if (!inputsInitialized) {
    if (obj.energyMeterState !== undefined && document.getElementById('toggleEnergyMeter') !== null) {
      document.getElementById('toggleEnergyMeter').checked = obj.energyMeterState;
    }
    if (obj.rfidState !== undefined && document.getElementById('toggleRfid') !== null) {
      document.getElementById('toggleRfid').checked = obj.rfidState
    }
    if (obj.energySignState !== undefined && document.getElementById('invertEnergySign') !== null) {
      document.getElementById('invertEnergySign').checked = obj.energySignState
    }
    inputsInitialized = true; // Set the flag to true after initializing
  }

  // Update if the element exists on the page
  const map = {
    l1Voltage: {factor:10,  dec:1, id:"l1Voltage"},
    l2Voltage: {factor:10,  dec:1, id:"l2Voltage"},
    l3Voltage: {factor:10,  dec:1, id:"l3Voltage"},
    l1Current: {factor:10,  dec:1, id:"l1Current"},
    l2Current: {factor:10,  dec:1, id:"l2Current"},
    l3Current: {factor:10,  dec:1, id:"l3Current"},
    l1Power  : {factor:10,  dec:1, id:"l1Power"},
    l2Power  : {factor:10,  dec:1, id:"l2Power"},
    l3Power  : {factor:10,  dec:1, id:"l3Power"},
    totPower : {factor:10,  dec:1, id:"totPower"},
    impPower : {factor:100, dec:2, id:"impPower"},   // kWh → 2 NK
    expPower : {factor:100, dec:2, id:"expPower"}
  };
  
  for (const key in map) {
    if (obj[key] !== undefined) {
      const {factor, dec, id} = map[key];
      document.getElementById(id).textContent =
          (obj[key] / factor).toFixed(dec);
    }
  }


/* ---------- Status-Ampel ---------- */
function setStatus(id, enabled, err) {
  const el = document.getElementById(id);
  if (!el) return;

  if (!enabled) {                         // Schalter AUS
    el.textContent = 'deactivated';
    el.className   = 'status off';
  } else if (err) {                       // Fehler
    el.textContent = 'error';
    el.className   = 'status error';
  } else {                                // alles ok
    el.textContent = 'ready';
    el.className   = 'status ready';
  }
}

/* Aufruf im WebSocket-Handler */
if (obj.energyMeterState !== undefined && obj.energyMeterError !== undefined) {
  const on = obj.energyMeterState === true || obj.energyMeterState === "true";
  setStatus('energyMeterStatus', on, obj.energyMeterError);
}
if (obj.rfidState !== undefined && obj.rfidError !== undefined) {
  const on = obj.rfidState === true || obj.rfidState === "true";
  setStatus('rfidStatus', on, obj.rfidError);
}
//if (obj.energySignState !== undefined && obj.energyMeterError !== undefined) {
//  const on = obj.energySignState === true || obj.energySignState === "true";
//  setStatus('energyMeterStatus', on, obj.energyMeterError);
//}



 /* ---------- Aktual RFID-Tag ----------------------------------- */
  if (obj.rfidTag !== undefined) {
    document.getElementById('rfidTag').textContent = obj.rfidTag;
  }
 /* ---------- Last RFID-Tag ----------------------------------- */
 if (obj.lastRfidTag !== undefined) {
  document.getElementById('lastRfidTag').textContent = obj.lastRfidTag;
  }


  // Update eneryMeterState if the element exists on the page
  if (obj.energyMeterState !== undefined && document.getElementById('toggleEnergyMeterON') !== null) {
    var relayState = obj.energyMeterState ? 'EIN' : 'AUS';
    document.getElementById('toggleEnergyMeterON').textContent = relayState;
  }
  // Update rfidState if the element exists on the page
  if (obj.rfidState !== undefined && document.getElementById('toggleRfidON') !== null) {
    var relayState = obj.rfidState ? 'EIN' : 'AUS';
    document.getElementById('toggleRfidON').textContent = relayState;
  }
  // Update energySignState if the element exists on the page
  if (obj.energySignState !== undefined && document.getElementById('invertEnergySignON') !== null) {
    var relayState = obj.energySignState ? 'EIN' : 'AUS';
    document.getElementById('invertEnergySignON').textContent = relayState;
  }
}

//--------------------------------DOMContentLoaded-----------------------------------//
//--------------------------------DOMContentLoaded-----------------------------------//
//--------------------------------DOMContentLoaded-----------------------------------//
// Event listener for buttons and page load
document.addEventListener('DOMContentLoaded', function() {
  // Initialize the WebSocket connection
  initWebSocket();

  // Set up event listeners for buttons
  document.getElementById('toggleEnergyMeter')?.addEventListener('change', function(event) {
    toggle_EnergyMeter();
  });
  document.getElementById('toggleRfid')?.addEventListener('change', function(event) {
    toggle_Rfid();
  });
  document.getElementById('invertEnergySign')?.addEventListener('change', function(event) {
    toggle_invertEnergySign();
  });


});

// Function to handle toggling of OFF and ON
function toggle_EnergyMeter() {
  const isChecked = document.getElementById('toggleEnergyMeter').checked;
  const data = {
    action: 'setEnergyMeter',
    state: isChecked 
  };
  Socket.send(JSON.stringify(data));
  console.log('Sending toggleEnergyMeter values to server:', data);
}

// Function to handle toggling of OFF and ON
function toggle_Rfid() {
  const isChecked = document.getElementById('toggleRfid').checked;
  const data = {
    action: 'setRfid',
    state: isChecked 
  };
  Socket.send(JSON.stringify(data));
  console.log('Sending toggleRfid values to server:', data);
}

// Function to handle toggling of OFF and ON
function toggle_invertEnergySign() {
  const isChecked = document.getElementById('invertEnergySign').checked;
  const data = {
    action: 'setEnergySign',
    state: isChecked 
  };
  Socket.send(JSON.stringify(data));
  console.log('Sending InvertEnergySign values to server:', data);
}



  