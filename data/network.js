// network.


var currentSSID = '';

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
    Socket.send(JSON.stringify({ action: 'subscribeUpdates', page: 'network' }));
  };

  // Event handler for incoming messages from the server
  Socket.onmessage = function(event) {
    console.log('WebSocket messages received:', event.data);
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
    Socket.send(JSON.stringify({ action: 'unsubscribeUpdates', page: 'network'  }));
    Socket.close();
  };
}


// Function to fetch and update ETH network settings
function fetchEthSettings() {
    // Fetch Ethernet network info
    fetch("/get_EthNetwork_info")
      .then(response => response.json())
      .then(ethData => {
        console.log('Ethernet network information received:', ethData);
  
        // Set Ethernet information
        setEthernetInfo(ethData);
  
        // Fetch WiFi network info
       /* fetch("/get_WifiNetwork_info")
          .then(response => response.json())
          .then(wifiData => {
            console.log('WiFi network information received:', wifiData);
  
            // Set WiFi information
            setWifiInfo(wifiData);
  
            // Start WiFi scan if WiFi is enabled
            if (wifiData.wifi_enable) {
              startWifiScan();
            } else {
              hideWifiScanResults();
            }
          })
          .catch(error => console.error('Error fetching WiFi network information:', error));
      */
        })
      .catch(error => console.error('Error fetching Ethernet network information:', error));
  }
  
// Function to fetch and update WiFi network settings
function fetchWifiSettings() {
        // Fetch WiFi network info
        fetch("/get_WifiNetwork_info")
          .then(response => response.json())
          .then(wifiData => {
            console.log('WiFi network information received:', wifiData);
            // Set WiFi information
            currentSSID = wifiData.wifi_ssid;
            setWifiInfo(wifiData);

            // Start WiFi scan if WiFi is enabled
            if (wifiData.wifi_enable) {
              startWifiScan();
            } else {
              hideWifiScanResults();
            }
          })
          .catch(error => console.error('Error fetching WiFi network information:', error));
  }


  // Function to set Ethernet information
  function setEthernetInfo(data) {
    document.getElementById("eth_mac-address").value = data.eth_mac || '';
    document.getElementById("eth_ip-address").value = data.eth_ip || '';
    document.getElementById("eth_subnet-mask").value = data.eth_netmask || '';
    document.getElementById("eth_gateway").value = data.eth_gateway || '';
    document.getElementById("eth_dns1").value = data.eth_dns1 || '';
    document.getElementById("eth_dns2").value = data.eth_dns2 || '';
  
    // Set Ethernet DHCP toggle
    const ethDhcpEnabled = data.eth_static;
    document.getElementById('eth_dhcp-toggle').checked = ethDhcpEnabled;
    toggleETHInputFields(!ethDhcpEnabled);
  
    // Set WiFi Enable toggle
    const wifiEnabled = data.wifi_enable;
    document.getElementById('wifi_enable').checked = wifiEnabled;
    toggleWifiInputFields(wifiEnabled);
  }
  
  // Function to set WiFi information
  function setWifiInfo(data) {
    // Set WiFi Enable toggle
    const wifiEnabled = data.wifi_enable;
    document.getElementById('wifi_enable').checked = wifiEnabled;
    toggleWifiInputFields(wifiEnabled);
    document.getElementById("wifi_ssid").value = data.wifi_ssid || '';
    document.getElementById("wifi_password").value = data.wifi_pwd || '';
    document.getElementById("wifi_connected").innerText = data.wifi_connected ? "Verbunden" : "Getrennt";
    document.getElementById("wifi_ssid_selected").innerText = data.wifi_ssid || '';
    document.getElementById("wifi_mac-address").value = data.wifi_mac || '';
    document.getElementById("wifi_ip-address").value = data.wifi_ip || '';
    document.getElementById("wifi_subnet-mask").value = data.wifi_netmask || '';
    document.getElementById("wifi_gateway").value = data.wifi_gateway || '';
    document.getElementById("wifi_dns1").value = data.wifi_dns1 || '';
    document.getElementById("wifi_dns2").value = data.wifi_dns2 || '';
  
    // Set WiFi DHCP toggle
    const wifiDhcpEnabled = data.wifi_static;
    document.getElementById('wifi_dhcp-toggle').checked = wifiDhcpEnabled;
    toggleWiFiInputFieldsForDHCP(!wifiDhcpEnabled);
  }
  
  // Function to start WiFi scan
  function startWifiScan() {
    // Show the WiFi scan results container
    showWifiScanResults();
  
    // Display scanning message
    var scanMessage = document.getElementById('wifi_scan_message');
    scanMessage.style.display = 'block';
    scanMessage.innerText = 'Scan wird durchgeführt...';
    document.getElementById('wifi_rescan_btn').style.display = 'none';
    document.getElementById('wifi_scan_results').innerHTML = '';
  
    fetch('/wifi_scan')
      .then(response => response.json())
      .then(data => {
        console.log('WiFi scan data received:', data);
        scanMessage.style.display = 'none';
        displayWifiScanResults(data);
        populateWifiNetworks(data); // Populate the SSID dropdown with scan results
        document.getElementById('wifi_rescan_btn').style.display = 'inline-block';
      })
      .catch(error => {
        console.error('Error during WiFi scan:', error);
        scanMessage.style.display = 'none';
        document.getElementById('wifi_scan_results').innerHTML = 'Fehler beim Scannen der WiFi-Netzwerke.';
      });
  }
  
    function populateWifiNetworks(data) {
      console.log('Calling populateWifiNetworks', data);
      const ssidSelect = document.getElementById('wifi_ssid');
      ssidSelect.innerHTML = ''; // Clear existing options
    
      let ssidExists = false;
    
      if (data.networks && data.networks.length > 0) {
        data.networks.forEach(network => {
          const option = document.createElement('option');
          option.value = network.name;
          option.text = network.name;
    
          // Check if the current network is the connected one
          if (network.name === currentSSID) {
            option.selected = true;
            ssidExists = true;
          }
    
          ssidSelect.appendChild(option);
        });
      }
    
      // If the current SSID is not in the scan results, add it
      if (!ssidExists && currentSSID) {
        const option = document.createElement('option');
        option.value = currentSSID;
        option.text = currentSSID + ' (Currently Connected)';
        option.selected = true;
        ssidSelect.appendChild(option);
      }
    
      // If no networks are found, display a message
      if (ssidSelect.options.length === 0) {
        const option = document.createElement('option');
        option.value = '';
        option.text = 'Keine Netzwerke gefunden';
        ssidSelect.appendChild(option);
      }
    }
    
  // Function to hide WiFi scan results
  function hideWifiScanResults() {
    var container = document.getElementById('wifi_scan_results');
    if (container) {
      container.innerHTML = '';
    }
    document.getElementById('wifi_rescan_btn').style.display = 'none';
    document.getElementById('wifi_scan_message').style.display = 'none';
  
    var wifiOptionsResults = document.getElementById('wifi-options-results');
    if (wifiOptionsResults) {
      wifiOptionsResults.style.display = 'none';
    }
  }
  
  // Function to show WiFi scan results container
  function showWifiScanResults() {
    var wifiOptionsResults = document.getElementById('wifi-options-results');
    if (wifiOptionsResults) {
      wifiOptionsResults.style.display = 'block';
    }
  }
  
  // Function to display WiFi scan results
  function displayWifiScanResults(data) {
    var container = document.getElementById('wifi_scan_results');
    if (!container) {
      console.error('wifi_scan_results element not found');
      return;
    }
  
    container.innerHTML = '';
  
    if (!data.networks || data.networks.length === 0) {
      container.innerHTML = 'Keine WiFi-Netzwerke gefunden.';
      return;
    }
  
    var table = document.createElement('table');
    table.className = 'wifi-table';
  
    var headerRow = table.insertRow();
    var headers = ['Name', 'Signal', 'Verschlüsselung'];
    headers.forEach(function(headerText) {
      var header = document.createElement('th');
      header.appendChild(document.createTextNode(headerText));
      headerRow.appendChild(header);
    });
  
    data.networks.forEach(function(network) {
      var row = table.insertRow();
  
      var cellName = row.insertCell();
      cellName.appendChild(document.createTextNode(network.name));
  
      var cellSignal = row.insertCell();
      cellSignal.appendChild(document.createTextNode(getSignalQuality(network.signal)));
  
      var cellEncryption = row.insertCell();
      cellEncryption.appendChild(document.createTextNode(network.encryption));
    });
  
    container.appendChild(table);
  }
  
  // Function to interpret signal quality from RSSI value
  function getSignalQuality(rssi) {
    if (rssi >= -50) {
      return 'Sehr gut';
    } else if (rssi >= -60) {
      return 'Gut';
    } else if (rssi >= -70) {
      return 'Mittel';
    } else {
      return 'Schwach';
    }
  }
  
  // Function to save Ethernet network settings
  function saveETHNetworkSettings() {
    var msg = {
      action: 'resetTCP',
      setEthStatic: document.getElementById('eth_dhcp-toggle').checked,
      setEthIpAdr: document.getElementById('eth_ip-address').value.split('.').map(Number),
      setEthNetmask: document.getElementById('eth_subnet-mask').value.split('.').map(Number),
      setEthGw: document.getElementById('eth_gateway').value.split('.').map(Number),
      setEthDns1: document.getElementById('eth_dns1').value.split('.').map(Number),
      setEthDns2: document.getElementById('eth_dns2').value.split('.').map(Number)
    };
    
    console.log('Sending Ethernet settings to server:', JSON.stringify(msg));
    
    if (Socket.readyState === WebSocket.OPEN) {
      Socket.send(JSON.stringify(msg));
    } else {
      console.error('WebSocket is not open. ReadyState: ' + Socket.readyState);
    }
  }
  
  // Function to save WiFi network settings
  function saveWifiNetworkSettings() {
    var msg = {
      setWifiEnable: document.getElementById('wifi_enable').checked,
      setWifiStatic: document.getElementById('wifi_dhcp-toggle').checked,
      setWifiSSID: document.getElementById('wifi_ssid').value,
      setWifiPassword: document.getElementById('wifi_password').value,
      setWifiIpAdr: document.getElementById('wifi_ip-address').value.split('.').map(Number),
      setWifiNetmask: document.getElementById('wifi_subnet-mask').value.split('.').map(Number),
      setWifiGw: document.getElementById('wifi_gateway').value.split('.').map(Number),
      setWifiDns1: document.getElementById('wifi_dns1').value.split('.').map(Number),
      setWifiDns2: document.getElementById('wifi_dns2').value.split('.').map(Number)
    };
    console.log('Sending WiFi settings to server:', JSON.stringify(msg));
    if (Socket.readyState === WebSocket.OPEN) {
      Socket.send(JSON.stringify(msg));
    } else {
      console.error('WebSocket is not open. ReadyState: ' + Socket.readyState);
    }
  }
  
  // Function to toggle Ethernet input fields based on DHCP setting
  function toggleETHInputFields(enable) {
    var ipFields = document.querySelectorAll('#eth_ip-address, #eth_subnet-mask, #eth_gateway, #eth_dns1, #eth_dns2');
    ipFields.forEach(function(field) {
      field.disabled = enable;
    });
  }
  
  // Function to toggle WiFi input fields based on WiFi enable toggle
  function toggleWifiInputFields(enable) {
    var wifiFields = document.getElementById('wifi_input_fields');
    if (wifiFields) {
      wifiFields.style.display = enable ? 'table' : 'none';
    }
  }
  
  // Function to toggle WiFi IP input fields based on DHCP setting
  function toggleWiFiInputFieldsForDHCP(enable) {
    var ipFields = document.querySelectorAll('#wifi_ip-address, #wifi_subnet-mask, #wifi_gateway, #wifi_dns1, #wifi_dns2');
    ipFields.forEach(function(field) {
      field.disabled = enable;
    });
  }
  
  // DOMContentLoaded event listener for network.html
  document.addEventListener("DOMContentLoaded", function() {
    // Initialize WebSocket connection
    initWebSocket();
    // Fetch and update Ethernet and Wifi settings
    fetchEthSettings();
    fetchWifiSettings();

    // Variable to track the last clicked button
    let lastClickedButton = null;
    
    // Event listener for the Ethernet Save button
    document.getElementById('eth_btn_save').addEventListener('click', function() {
        lastClickedButton = 'eth';
        const popup = document.getElementById('confirmation-popup');
        popup.style.display = 'flex';
    });
    
    // Event listener for the WiFi Save button
    document.getElementById('wifi_btn_save').addEventListener('click', function() {
        lastClickedButton = 'wifi';
        const popup = document.getElementById('confirmation-popup');
        popup.style.display = 'flex';
    });
    
    // Event listeners for the Popup buttons
    document.getElementById('popup-yes').addEventListener('click', function() {
        const popup = document.getElementById('confirmation-popup');
        const loadingIndicator = document.getElementById('loading-indicator');
        popup.style.display = 'none';
        loadingIndicator.style.display = 'flex';

        // Execute the appropriate save function based on the last clicked button
        if (lastClickedButton === 'eth') {
            saveETHNetworkSettings();
        } else if (lastClickedButton === 'wifi') {
            saveWifiNetworkSettings();
        }
    
        // Reset the state after execution
        lastClickedButton = null;
        setTimeout(() => {
          // Weiterleitung zur Indexseite
          window.location.href = 'index.html';
      }, 5000);
    });
    
    document.getElementById('popup-no').addEventListener('click', function() {
        const popup = document.getElementById('confirmation-popup');
        popup.style.display = 'none';
    
        // Reset the state to prevent accidental execution
        lastClickedButton = null;
    });

    document.getElementById('eth_dhcp-toggle').addEventListener('change', function(event) {
      toggleETHInputFields(!event.target.checked);
    });
  
    // Event listener for WiFi Enable Toggle
    var wifiEnableCheckbox = document.getElementById('wifi_enable');
    if (wifiEnableCheckbox) {
      wifiEnableCheckbox.addEventListener('change', function(event) {
        var enabled = event.target.checked;
        toggleWifiInputFields(enabled);
        if (enabled) {
          startWifiScan();
        } else {
          hideWifiScanResults();
        }
      });
    } else {
      console.error('wifi_enable element not found');
    }
  
    // Event listener for WiFi DHCP Toggle
    document.getElementById('wifi_dhcp-toggle').addEventListener('change', function(event) {
      toggleWiFiInputFieldsForDHCP(!event.target.checked);
    });
  
    // Event listener for Rescan button
    document.getElementById('wifi_rescan_btn').addEventListener('click', function() {
      startWifiScan();
    });
  });
  