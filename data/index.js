//Überprüfung ob der User angemeldet ist
/**if (sessionStorage.getItem("login") != "true") {
  location.replace('404.html');
}
**/



if (document.getElementById("namevalue1") != null) {
  function rfidwhitelistSendBack() {
    var msg = {
      rfid1_contr: document.getElementById("value1").value,
      rfid2_contr: document.getElementById("value2").value,
      rfid3_contr: document.getElementById("value3").value,
      rfid4_contr: document.getElementById("value4").value,
      rfid5_contr: document.getElementById("value5").value,
      rfid6_contr: document.getElementById("value6").value,
      rfid7_contr: document.getElementById("value7").value,
      rfid8_contr: document.getElementById("value8").value,
      rfid9_contr: document.getElementById("value9").value,
      rfid10_contr: document.getElementById("value10").value,
  /*    rfid11_contr: document.getElementById("value11").value,
      rfid12_contr: document.getElementById("value12").value,
      rfid13_contr: document.getElementById("value13").value,
      rfid14_contr: document.getElementById("value14").value,
      rfid15_contr: document.getElementById("value15").value,
      rfid16_contr: document.getElementById("value16").value,
      rfid17_contr: document.getElementById("value17").value,
      rfid18_contr: document.getElementById("value18").value,
      rfid19_contr: document.getElementById("value19").value,
      rfid20_contr: document.getElementById("value20").value, */


      rfid1Name_contr: document.getElementById("namevalue1").value,
      rfid2Name_contr: document.getElementById("namevalue2").value,
      rfid3Name_contr: document.getElementById("namevalue3").value,
      rfid4Name_contr: document.getElementById("namevalue4").value,
      rfid5Name_contr: document.getElementById("namevalue5").value,
      rfid6Name_contr: document.getElementById("namevalue6").value,
      rfid7Name_contr: document.getElementById("namevalue7").value,
      rfid8Name_contr: document.getElementById("namevalue8").value,
      rfid9Name_contr: document.getElementById("namevalue9").value,
      rfid10Name_contr: document.getElementById("namevalue10").value,
   /*   rfid11Name_contr: document.getElementById("namevalue11").value,
      rfid12Name_contr: document.getElementById("namevalue12").value,
      rfid13Name_contr: document.getElementById("namevalue13").value,
      rfid14Name_contr: document.getElementById("namevalue14").value,
      rfid15Name_contr: document.getElementById("namevalue15").value,
      rfid16Name_contr: document.getElementById("namevalue16").value,
      rfid17Name_contr: document.getElementById("namevalue17").value,
      rfid18Name_contr: document.getElementById("namevalue18").value,
      rfid19Name_contr: document.getElementById("namevalue19").value,
      rfid20Name_contr: document.getElementById("namevalue20").value, */

      rfid1Time_contr: Number(document.getElementById("timevalue1").value),
      rfid2Time_contr: Number(document.getElementById("timevalue2").value),
      rfid3Time_contr: Number(document.getElementById("timevalue3").value),
      rfid4Time_contr: Number(document.getElementById("timevalue4").value),
      rfid5Time_contr: Number(document.getElementById("timevalue5").value),
      rfid6Time_contr: Number(document.getElementById("timevalue6").value),
      rfid7Time_contr: Number(document.getElementById("timevalue7").value),
      rfid8Time_contr: Number(document.getElementById("timevalue8").value),
      rfid9Time_contr: Number(document.getElementById("timevalue9").value),
      rfid10Time_contr: Number(document.getElementById("timevalue10").value),
    /*  rfid11Time_contr: Number(document.getElementById("timevalue11").value),
      rfid12Time_contr: Number(document.getElementById("timevalue12").value),
      rfid13Time_contr: Number(document.getElementById("timevalue13").value),
      rfid14Time_contr: Number(document.getElementById("timevalue14").value),
      rfid15Time_contr: Number(document.getElementById("timevalue15").value),
      rfid16Time_contr: Number(document.getElementById("timevalue16").value),
      rfid17Time_contr: Number(document.getElementById("timevalue17").value),
      rfid18Time_contr: Number(document.getElementById("timevalue18").value),
      rfid19Time_contr: Number(document.getElementById("timevalue19").value),
      rfid20Time_contr: Number(document.getElementById("timevalue20").value), */

    }
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[18] = true; }, publicTimeout);

  }
}
//Globale Variablen

var Socket;
var publicTimeout = 2000;
var isCheckedBtnArr = [];
for (var i = 0; i < 25; i++) {
  isCheckedBtnArr.push(true);
}

function init() {
  Socket = new WebSocket('ws://' + window.location.hostname + ':81/');
  Socket.onmessage = function (event) {
    processCommand(event);
  };
}

//document.getElementById('BTN_SEND_BACK').addEventListener('click', button_send_back);


var reloadIntervarl = setInterval(() => {
  if (Socket != null) {
    if (Socket.readyState == 0) {
      document.getElementById("flexBox").style.display = "none";
      document.getElementById("loadingBox").style.display = "block";
    }
    else if (Socket.readyState == 1) {
      document.getElementById("flexBox").style.display = "block";
      document.getElementById("loadingBox").style.display = "none";

      clearInterval(reloadIntervarl);
    }
  } else {
    console.log("Error")
  }
}, 20);

document.getElementById("flexBox").style.display = "block";
document.getElementById("loadingBox").style.display = "none";

//Checkbox für Sliders , zum zurücksenden ans Backend
if (document.getElementById("stateRFID") !== null) {
  document.getElementById("stateRFID").addEventListener('change', function () {
    isCheckedBtnArr[19] = false;

    var msg = {
      RFID_contr: Number(document.getElementById("stateRFID").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[19] = true; }, publicTimeout);
  });
}

if (document.getElementById("ChCo4_en") !== null) {
  document.getElementById("ChCo4_en").addEventListener('change', function () {
    isCheckedBtnArr[0] = false;

    var msg = {
      ChContr4_contr: Number(document.getElementById("ChCo4_en").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[0] = true; }, publicTimeout);
  });
}

if (document.getElementById("ChCo5_en") !== null) {
  document.getElementById("ChCo5_en").addEventListener('change', function () {
    isCheckedBtnArr[1] = false;

    var msg = {
      ChContr5_contr: Number(document.getElementById("ChCo5_en").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[1] = true; }, publicTimeout);
  });

}


if (document.getElementById("buzzer") !== null) {
  document.getElementById("buzzer").addEventListener('change', function () {
    isCheckedBtnArr[3] = false;

    var msg = {
      rfidBuzzer_contr: Number(document.getElementById("buzzer").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[3] = true; }, publicTimeout);
  });
}



/**Maximum Grid zurücksenden */


if (document.getElementById("maxamp") != null) {
  document.getElementById("maxamp").addEventListener("focus", function () {
    isCheckedBtnArr[6] = false;
  });
  function looseMaxGridFocus() {
    isCheckedBtnArr[6] = true;
  }
  document.getElementById("maxamp").addEventListener("keypress", function (event) {
    // If the user presses the "Enter" key on the keyboard
    if (event.key === "Enter") {
      var msg = {
        maxGridCurrent_contr: Number(document.getElementById("maxamp").value)
      };//!Number.isNaN(Number(document.getElementById("maxamp").value)) == true &&
      if (document.getElementById("maxamp").value >= 0 && document.getElementById("maxamp").value != '' && document.getElementById("maxamp").value <= 999) {
        Socket.send(JSON.stringify(msg));
      } else {
        alert("Fehler! Die Eingabe ist nicht gültig (Die Zahl muss größer oder gleich 0 sein und kleiner als 1000)")
      }

    }
  });
}


if (document.getElementById("ladeStromValue") != null) {
  document.getElementById("ladeStromValue").addEventListener("focus", function () {
    isCheckedBtnArr[8] = false;
  });
  function looseMaxGridFocus() {
    isCheckedBtnArr[8] = true;
  }
  document.getElementById("ladeStromValue").addEventListener("keypress", function (event) {
    // If the user presses the "Enter" key on the keyboard
    if (event.key === "Enter") {
      var msg = {
        ChrgCurr_contr: Number(document.getElementById("ladeStromValue").value)
      };//!Number.isNaN(Number(document.getElementById("maxamp").value)) == true &&
      Socket.send(JSON.stringify(msg));

    }
  });
}

if (document.getElementById("ladeleistungValue") != null) {
  document.getElementById("ladeleistungValue").addEventListener("focus", function () {
    isCheckedBtnArr[8] = false;
  });
  function looseMaxGridFocus() {
    isCheckedBtnArr[8] = true;
  }
  document.getElementById("ladeleistungValue").addEventListener("keypress", function (event) {
    // If the user presses the "Enter" key on the keyboard
    if (event.key === "Enter") {
      var msg = {
        ChrgPowr_contr: Number(document.getElementById("ladeleistungValue").value)
      };//!Number.isNaN(Number(document.getElementById("maxamp").value)) == true &&
      Socket.send(JSON.stringify(msg));

    }
  });
}
if (document.getElementById("maxLadeStromValue") != null) {
  document.getElementById("maxLadeStromValue").addEventListener("focus", function () {
    isCheckedBtnArr[8] = false;
  });
  function looseMaxGridFocus() {
    isCheckedBtnArr[8] = true;
  }
  document.getElementById("maxLadeStromValue").addEventListener("keypress", function (event) {
    // If the user presses the "Enter" key on the keyboard
    if (event.key === "Enter") {
      var msg = {
        ChrgMaxCurr_contr: Number(document.getElementById("maxLadeStromValue").value)
      };//!Number.isNaN(Number(document.getElementById("maxamp").value)) == true &&
      Socket.send(JSON.stringify(msg));

    }
  });
}
if (document.getElementById("minLadeStromValue") != null) {
  document.getElementById("minLadeStromValue").addEventListener("focus", function () {
    isCheckedBtnArr[8] = false;
  });
  function looseMaxGridFocus() {
    isCheckedBtnArr[8] = true;
  }
  document.getElementById("minLadeStromValue").addEventListener("keypress", function (event) {
    // If the user presses the "Enter" key on the keyboard
    if (event.key === "Enter") {
      var msg = {
        ChrgMinCurr_contr: Number(document.getElementById("minLadeStromValue").value)
      };//!Number.isNaN(Number(document.getElementById("maxamp").value)) == true &&
      Socket.send(JSON.stringify(msg));

    }
  });
}
/**Throttled Current zurücksenden */
if (document.getElementById("ChContr6_input") != null) {
  document.getElementById("ChContr6_input").addEventListener("keypress", function (event) {
    if (event.key === "Enter") {

      var msg = {
        ChContr6_contr: Number(document.getElementById("ChContr6_input").value)
      };//!Number.isNaN(Number(document.getElementById("maxamp").value)) == true &&
      if (document.getElementById("ChContr6_input").value >= 0 && document.getElementById("ChContr6_input").value != '' && document.getElementById("ChContr6_input").value <= Number(document.getElementById("ChContr3").innerHTML)) {
        Socket.send(JSON.stringify(msg));
      } else {
        alert("Fehler! Die Eingabe ist nicht gültig (Die Zahl muss größer oder gleich 0  und kleiner bzw. gleich  " + document.getElementById("ChContr3").innerHTML + " sein)");
      }
    }

  });
}
/**WallBox select zurücksenden */
if (document.getElementById('selectWallbox') != null) {
  document.getElementById('selectWallbox').addEventListener('click', function () {
    var wallboxSel = {
      wallbox: document.getElementById("selectWallbox").value
    };
    Socket.send(JSON.stringify(wallboxSel));

  });

}
/**Wallboxen Status check*/


/**Save Buttons zurücksenden */
/**Wallbox Settings zurücksenden */
/**Wallbox 1 */
if (document.getElementById("state1") !== null) {
  document.getElementById("state1").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox1cc_contr: Number(document.getElementById("state1").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}
if (document.getElementById("state2") !== null) {
  document.getElementById("state2").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox1em_contr: Number(document.getElementById("state2").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}
if (document.getElementById("LC_Switch1") !== null) {
  document.getElementById("LC_Switch1").addEventListener('change', function () {
    isCheckedBtnArr[8] = false;

    var msg = {
      LcSwitch1_contr: Number(document.getElementById("LC_Switch1").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[8] = true; }, publicTimeout);
  });
}
if (document.getElementById("LC_Switch2") !== null) {
  document.getElementById("LC_Switch2").addEventListener('change', function () {
    isCheckedBtnArr[8] = false;

    var msg = {
      LcSwitch2_contr: Number(document.getElementById("LC_Switch2").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[8] = true; }, publicTimeout);
  });
}

if (document.getElementById("state3") !== null) {
  document.getElementById("state3").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox1rfid_contr: Number(document.getElementById("state3").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}
/**Wallbox 2 */
if (document.getElementById("state11") !== null) {
  document.getElementById("state11").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox2cc_contr: Number(document.getElementById("state11").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}
if (document.getElementById("state12") !== null) {
  document.getElementById("state12").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox2em_contr: Number(document.getElementById("state12").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}

if (document.getElementById("state13") !== null) {
  document.getElementById("state13").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox2rfid_contr: Number(document.getElementById("state13").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}
/**Wallbox 3 */

if(document.getElementById("rangeSlider") != null){
  rangeSlider.addEventListener('change', function(){
    isCheckedBtnArr[22] = false;
    let rangeSlider = document.getElementById("rangeSlider").value;
    let newRangeSlider;
    let ledBox =  document.getElementById("ledBox")

    if(rangeSlider == 2){
      newRangeSlider = 0

    }
    else if(rangeSlider == 1){
      newRangeSlider = 1

    }
    else if(rangeSlider == 3){
      newRangeSlider = 2

    }
    
    var msg = {
      rfidLed_contr: newRangeSlider
    };
        
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[22] = true; }, publicTimeout);
  });

  
  
  






}


if (document.getElementById("state21") !== null) {
  document.getElementById("state21").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox3cc_contr: Number(document.getElementById("state21").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}
if (document.getElementById("state22") !== null) {
  document.getElementById("state22").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox3em_contr: Number(document.getElementById("state22").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}

if (document.getElementById("state23") !== null) {
  document.getElementById("state23").addEventListener('change', function () {
    isCheckedBtnArr[7] = false;

    var msg = {
      wallbox3rfid_contr: Number(document.getElementById("state23").checked)
    };
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[7] = true; }, publicTimeout);
  });
}


/**Restart zurücksenden */
function restartSendBack() {
  var msg = {
    restart_contr: 1
  };
  Socket.send(JSON.stringify(msg));
}
/**IP Settings zurücksenden */

if (document.getElementById("ipSettingsSendBack") !== null) {
  function ipInputSendBackFunc() {
    var inputs = $(".ipinput");
    let checker = true;
    let inputsArray = [];
    for (var i = 0; i < inputs.length; i++) {
      inputsArray[i] = Number($(inputs[i]).val());
    }

    for (var i = 0; i < inputsArray.length; i++) {

      if (inputsArray[i] <= 255 && inputsArray[i] >= 0 && typeof (inputsArray[i]) === "number" && checker == true) {
        checker = true;
      } else {
        checker = false;
      }
    }
    if (checker == true) {
      var msg = {
        staticIp_contr: Number(document.getElementById("ipassign").checked),
        setIpSettings: 1,

        staticIpAdress1: Number(document.getElementById("IP_WebInf1").value),
        staticIpAdress2: Number(document.getElementById("IP_WebInf2").value),
        staticIpAdress3: Number(document.getElementById("IP_WebInf3").value),
        staticIpAdress4: Number(document.getElementById("IP_WebInf4").value),

        staticDNS1: Number(document.getElementById("DNS_WebInf1").value),
        staticDNS2: Number(document.getElementById("DNS_WebInf2").value),
        staticDNS3: Number(document.getElementById("DNS_WebInf3").value),
        staticDNS4: Number(document.getElementById("DNS_WebInf4").value),

        staticSubnet1: Number(document.getElementById("SUB_WebInf1").value),
        staticSubnet2: Number(document.getElementById("SUB_WebInf2").value),
        staticSubnet3: Number(document.getElementById("SUB_WebInf3").value),
        staticSubnet4: Number(document.getElementById("SUB_WebInf4").value),

        staticGateway1: Number(document.getElementById("GATE_WebInf1").value),
        staticGateway2: Number(document.getElementById("GATE_WebInf2").value),
        staticGateway3: Number(document.getElementById("GATE_WebInf3").value),
        staticGateway4: Number(document.getElementById("GATE_WebInf4").value)



      }
      Socket.send(JSON.stringify(msg));
    } else {
      alert("Fehler! mind. 1 Eingabe ist falsch")
    }



  }
  //

  //NTP Settings zurücksenden
  function ntpInputSendBack() {
    var msg = {
      setNTPSettings: 1,
      NTP_contr: Number(document.getElementById("stateNTP").checked),
      ntpServervalue_contr: document.getElementById("ntpServervalue").value,

    }
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[4] = true; }, publicTimeout);

  }
  //OCPP Settings zurücksenden

  function ocppInputSendBackFunc() {
    var msg = {
      setOCPPsettings: 1,
      OCPP_contr: Number(document.getElementById("stateOCPP").checked),
      OCPP_TLS_contr: Number(document.getElementById("StateOCPP_TLS").checked),
      ocppHostIp_contr: document.getElementById("ocppHostIp").value,
      ocppHostPort_contr: document.getElementById("ocppHostPort").value,
      ocppHostAuthId_contr: document.getElementById("ocppHostAuthId").value,
      ocppHostAuthKey_contr: document.getElementById("ocppHostAuthKey").value,
      ocppHostUrl_contr: document.getElementById("ocppHostUrl").value
    }
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[17] = true; }, publicTimeout);

  }

  //FTP Settings zurück senden
  function ftpInputSendBackFunc() {
    var msg = {
      setFTPsettings: 1,
      FTP_contr: Number(document.getElementById("stateFTP").checked),
      FTPServerUrl_contr: document.getElementById("ftpIpAddress").value,
      FTPServerUser_contr: document.getElementById("ftpUser").value,
      FTPServerPass_contr: document.getElementById("ftpPassword").value,
      FTPServerDir_contr: document.getElementById("ftpPath").value,

    }
    Socket.send(JSON.stringify(msg));
    setTimeout(() => { isCheckedBtnArr[21] = true; }, publicTimeout);

  }
}


function processCommand(event) {
  /**JSON Object konventieren */
  var obj = JSON.parse(event.data);
  console.log(obj);
  /**Wallbox  select*/
  /**
   * wallboxSelect = document.getElementById("selectWallbox");
  wallboxSelect.value = obj.wallbox_select;**/
  /*Dashboard Daten einlesen*/
  if (document.getElementById("ChContr1") !== null && document.getElementById("ChCo4_en") !== null) {
    if (isCheckedBtnArr[6] == true) {
      document.getElementById("maxamp").value = obj.maxGridCurrent_contr;
    }
    

    document.getElementById("ChContr1").innerHTML = obj.ChContr1_state;
    document.getElementById("ChContr2").innerHTML = obj.ChContr2_state;
    document.getElementById("ChContr3").innerHTML = obj.ChContr3_state;
    document.getElementById("ChContr4").innerHTML = obj.ChContr4_state;
    document.getElementById("ChContr5").innerHTML = obj.ChContr5_state;
    document.getElementById("ChContr6").innerHTML = obj.ChContr6_state;
    document.getElementById("EnMtr1").innerHTML = obj.EnMtr1_state;
    document.getElementById("EnMtr2").innerHTML = obj.EnMtr2_state;
    document.getElementById("EnMtr3").innerHTML = obj.EnMtr3_state;
    document.getElementById("EnMtr5").innerHTML = obj.EnMtr5_state;
    document.getElementById("EnMtr6").innerHTML = obj.EnMtr6_state;
    document.getElementById("EnMtr7").innerHTML = obj.EnMtr7_state;
    document.getElementById("EnMtr8").innerHTML = obj.EnMtr8_state;
    document.getElementById("EnMtr9").innerHTML = obj.EnMtr9_state;
    document.getElementById("EnMtr10").innerHTML = obj.EnMtr10_state;
    document.getElementById("EnMtr11").innerHTML = obj.EnMtr11_state;
    document.getElementById("EnMtr12").innerHTML = obj.EnMtr12_state;
    document.getElementById("EnMtr13").innerHTML = obj.EnMtr13_state;
    document.getElementById("EnMtr14").innerHTML = obj.EnMtr14_state;
    document.getElementById("cardSerial").innerHTML = obj.cardSerial_state;
    if(isCheckedBtnArr[22] == true){
    let rangeSlider =  document.getElementById("rangeSlider")
    let ledBox =  document.getElementById("ledBox")
   
         if (obj.RFIDLed_state == 0) {
             ledBox.style.backgroundColor = "#d7d7d7"
             rangeSlider.value = 2;
         }
         else if (obj.RFIDLed_state == 1) {
            rangeSlider.value = 1;

             ledBox.style.backgroundColor = "#28d142";

         }
         else if (obj.RFIDLed_state == 2) {
          rangeSlider.value = 3;

             ledBox.style.backgroundColor = "#ff5252";

         }
         else {
         }
        }
    if (isCheckedBtnArr[0] == true) {
      document.getElementById("ChCo4_en").checked = Boolean(Number(obj.ChContr4_state));
    }
    if (isCheckedBtnArr[1] == true) {
      document.getElementById("ChCo5_en").checked = Boolean(Number(obj.ChContr5_state));

    }
    if (isCheckedBtnArr[3] == true) {
      document.getElementById("buzzer").checked = Boolean(Number(obj.buzzer_state));

    }
    document.getElementById("ChContr6_input").max = obj.ChContr3_state;

    //Status Energymeter


   
     
    
    //document.getElementById("cardSerial").innerHTML = obj.RFID1 + ":" + obj.RFID2 + ":" + obj.RFID3 + ":" + obj.RFID4

  }

  //Einstellungen Daten einlesen

  if (document.getElementById("IP_WebInf2") !== null && document.getElementById("ipassign").checked == false && isCheckedBtnArr[5] == true) {


    document.getElementById("IP_WebInf1").value = obj.IP_WebInf1_state;
    document.getElementById("IP_WebInf2").value = obj.IP_WebInf2_state;
    document.getElementById("IP_WebInf3").value = obj.IP_WebInf3_state;
    document.getElementById("IP_WebInf4").value = obj.IP_WebInf4_state;
    //Subnet
    document.getElementById("DNS_WebInf1").value = obj.DNS_WebInf1_state;
    document.getElementById("DNS_WebInf2").value = obj.DNS_WebInf2_state;
    document.getElementById("DNS_WebInf3").value = obj.DNS_WebInf3_state;
    document.getElementById("DNS_WebInf4").value = obj.DNS_WebInf4_state;
    //
    document.getElementById("SUB_WebInf1").value = obj.SUB_WebInf1_state;
    document.getElementById("SUB_WebInf2").value = obj.SUB_WebInf2_state;
    document.getElementById("SUB_WebInf3").value = obj.SUB_WebInf3_state;
    document.getElementById("SUB_WebInf4").value = obj.SUB_WebInf4_state;
    //Gateway
    document.getElementById("GATE_WebInf1").value = obj.GATE_WebInf1_state;
    document.getElementById("GATE_WebInf2").value = obj.GATE_WebInf2_state;
    document.getElementById("GATE_WebInf3").value = obj.GATE_WebInf3_state;
    document.getElementById("GATE_WebInf4").value = obj.GATE_WebInf4_state;

    if (isCheckedBtnArr[5] == true) {
      document.getElementById("ipassign").checked = Boolean(Number(obj.staticIp_state));
    }
    isCheckedBtnArr[5] = false;
  }


  /***Wallboxen auslesen */
  if (isCheckedBtnArr[7] == true && document.getElementById("state1") != null) {
    document.getElementById("state1").checked = Boolean(Number(obj.wallbox1cc_state));
    document.getElementById("state2").checked = Boolean(Number(obj.wallbox1em_state));
    document.getElementById("state3").checked = Boolean(Number(obj.wallbox1rfid_state));

    document.getElementById("state11").checked = Boolean(Number(obj.wallbox2cc_state));
    document.getElementById("state12").checked = Boolean(Number(obj.wallbox2em_state));
    document.getElementById("state13").checked = Boolean(Number(obj.wallbox2rfid_state));

    document.getElementById("state21").checked = Boolean(Number(obj.wallbox3cc_state));
    document.getElementById("state22").checked = Boolean(Number(obj.wallbox3em_state));
    document.getElementById("state23").checked = Boolean(Number(obj.wallbox3rfid_state));

    var statusDivBoxArray = $(".wallboxErrorCheck");
    var statusStateBoxArray = [];

    var stateBoxArray = [];


    statusStateBoxArray.push(obj.wallbox1cc_io);
    statusStateBoxArray.push(obj.wallbox1em_io);
    statusStateBoxArray.push(obj.wallbox1rfid_io);
    statusStateBoxArray.push(obj.wallbox2cc_io);
    statusStateBoxArray.push(obj.wallbox2em_io);
    statusStateBoxArray.push(obj.wallbox2rfid_io);
    statusStateBoxArray.push(obj.wallbox3cc_io);
    statusStateBoxArray.push(obj.wallbox3em_io);
    statusStateBoxArray.push(obj.wallbox3rfid_io);

    stateBoxArray.push(obj.wallbox1cc_state);
    stateBoxArray.push(obj.wallbox1em_state);
    stateBoxArray.push(obj.wallbox1rfid_state);
    stateBoxArray.push(obj.wallbox2cc_state);
    stateBoxArray.push(obj.wallbox2em_state);
    stateBoxArray.push(obj.wallbox2rfid_state);
    stateBoxArray.push(obj.wallbox3cc_state);
    stateBoxArray.push(obj.wallbox3em_state);
    stateBoxArray.push(obj.wallbox3rfid_state);



    if (statusStateBoxArray.length == statusDivBoxArray.length) {
      for (let i = 0; i < statusStateBoxArray.length; i++) {
        if (stateBoxArray[i] == 1) {
          if (statusStateBoxArray[i] == 1) {
            statusDivBoxArray[i].innerText = 'OK';
            statusDivBoxArray[i].style.color = "green";
          }
          else if (statusStateBoxArray[i] == 0) {
            statusDivBoxArray[i].innerText = 'Fehler';
            statusDivBoxArray[i].style.color = "red";
          }
          else {
          }
        }
        else if (stateBoxArray[i] == 0) {
          statusDivBoxArray[i].innerText = '';

        }
      }
    }
    if (document.getElementById("item5LC") != null && isCheckedBtnArr[8] == true) {
      document.getElementById("cpSannung").innerText = obj.CpVolt_state;
      document.getElementById("ADCinput").innerText = obj.ADCVolt_state;

      document.getElementById("chargeState").innerText = obj.CpPilot_state;


      document.getElementById("ladeleistungValue").value = obj.ChrgPowr_state;
      document.getElementById("ladeStromValue").value = obj.ChrgCurr_state;
      document.getElementById("maxLadeStromValue").value = obj.ChrgMaxCurr_state;
      document.getElementById("minLadeStromValue").value = obj.ChrgMinCurr_state;


      document.getElementById("LC_Switch1").checked = Boolean(Number(obj.LcSwitch1_state));
      document.getElementById("LC_Switch2").checked = Boolean(Number(obj.LcSwitch2_state));



    }
    /**for (var i = 0; i < inputs.length; i++) {
      inputsArray[i] = Number($(inputs[i]).val());
    }**/

  }
  // if(    var statusDivBoxArray = $(".wallboxErrorCheck");
  //)

  //NTP Daten auslesen
  if (isCheckedBtnArr[4] == true && document.getElementById("ntpServervalue") != null) {
    document.getElementById("ntpServervalue").value = obj.ntpServervalue_state;
    document.getElementById("stateNTP").checked = Boolean(Number(obj.NTP_state));
    document.getElementById("localTime").innerHTML = obj.localtime_state;



    isCheckedBtnArr[4] = false;
  }

  //OCPP Daten auslesen

  if (isCheckedBtnArr[17] == true && document.getElementById("ocppHostIp") != null) {
    document.getElementById("stateOCPP").checked = Boolean(Number(obj.OCPP_state))
    document.getElementById("StateOCPP_TLS").checked = Boolean(Number(obj.OCPP_TLS_state))
    document.getElementById("ocppHostIp").value = obj.ocppHostIp_state;
    document.getElementById("ocppHostPort").value = obj.ocppHostPort_state;
    document.getElementById("ocppHostAuthId").value = obj.ocppHostAuthId_state;
    document.getElementById("ocppHostAuthKey").value = obj.ocppHostAuthKey_state;
    document.getElementById("ocppHostUrl").value = obj.ocppHostUrl_state

    isCheckedBtnArr[17] = false;
  }
  if (isCheckedBtnArr[21] == true && document.getElementById("ftpUser") != null) {
    
    document.getElementById("stateFTP").checked = Boolean(Number(obj.FTP_state))
    document.getElementById("ftpIpAddress").value = obj.FTPServerUrl_state;
    document.getElementById("ftpUser").value = obj.FTPServerUser_state;
    document.getElementById("ftpPassword").value = obj.FTPServerPass_state;
    document.getElementById("ftpPath").value = obj.FTPServerDir_state;
    document.getElementById("ftpLastTry").innerHTML = obj.FTPServer_state;

    isCheckedBtnArr[21] = false;
  }

  //Whitelist Daten auslesen
  if (document.getElementById("value1") != null && isCheckedBtnArr[18] == true) {
    document.getElementById("value1").value = obj.rfid1_state;
    document.getElementById("value2").value = obj.rfid2_state;
    document.getElementById("value3").value = obj.rfid3_state;
    document.getElementById("value4").value = obj.rfid4_state;
    document.getElementById("value5").value = obj.rfid5_state;
    document.getElementById("value6").value = obj.rfid6_state;
    document.getElementById("value7").value = obj.rfid7_state;
    document.getElementById("value8").value = obj.rfid8_state;
    document.getElementById("value9").value = obj.rfid9_state;
    document.getElementById("value10").value = obj.rfid10_state;
   /* document.getElementById("value11").value = obj.rfid11_state;
    document.getElementById("value12").value = obj.rfid12_state;
    document.getElementById("value13").value = obj.rfid13_state;
    document.getElementById("value14").value = obj.rfid14_state;
    document.getElementById("value15").value = obj.rfid15_state;
    document.getElementById("value16").value = obj.rfid16_state;
    document.getElementById("value17").value = obj.rfid17_state;
    document.getElementById("value18").value = obj.rfid18_state;
    document.getElementById("value19").value = obj.rfid19_state;
    document.getElementById("value20").value = obj.rfid20_state; */




    document.getElementById("namevalue1").value = obj.rfid1Name_state;
    document.getElementById("namevalue2").value = obj.rfid2Name_state;
    document.getElementById("namevalue3").value = obj.rfid3Name_state;
    document.getElementById("namevalue4").value = obj.rfid4Name_state;
    document.getElementById("namevalue5").value = obj.rfid5Name_state;
    document.getElementById("namevalue6").value = obj.rfid6Name_state;
    document.getElementById("namevalue7").value = obj.rfid7Name_state;
    document.getElementById("namevalue8").value = obj.rfid8Name_state;
    document.getElementById("namevalue9").value = obj.rfid9Name_state;
    document.getElementById("namevalue10").value = obj.rfid10Name_state;
 /*   document.getElementById("namevalue11").value = obj.rfid11Name_state;
    document.getElementById("namevalue12").value = obj.rfid12Name_state;
    document.getElementById("namevalue13").value = obj.rfid13Name_state;
    document.getElementById("namevalue14").value = obj.rfid14Name_state;
    document.getElementById("namevalue15").value = obj.rfid15Name_state;
    document.getElementById("namevalue16").value = obj.rfid16Name_state;
    document.getElementById("namevalue17").value = obj.rfid17Name_state;
    document.getElementById("namevalue18").value = obj.rfid18Name_state;
    document.getElementById("namevalue19").value = obj.rfid19Name_state;
    document.getElementById("namevalue20").value = obj.rfid20Name_state;*/

    document.getElementById("timevalue1").value = obj.rfid1Time_state;
    document.getElementById("timevalue2").value = obj.rfid2Time_state;
    document.getElementById("timevalue3").value = obj.rfid3Time_state;
    document.getElementById("timevalue4").value = obj.rfid4Time_state;
    document.getElementById("timevalue5").value = obj.rfid5Time_state;
    document.getElementById("timevalue6").value = obj.rfid6Time_state;
    document.getElementById("timevalue7").value = obj.rfid7Time_state;
    document.getElementById("timevalue8").value = obj.rfid8Time_state;
    document.getElementById("timevalue9").value = obj.rfid9Time_state;
    document.getElementById("timevalue10").value = obj.rfid10Time_state;
 /*   document.getElementById("timevalue11").value = obj.rfid11Time_state;
    document.getElementById("timevalue12").value = obj.rfid12Time_state;
    document.getElementById("timevalue13").value = obj.rfid13Time_state;
    document.getElementById("timevalue14").value = obj.rfid14Time_state;
    document.getElementById("timevalue15").value = obj.rfid15Time_state;
    document.getElementById("timevalue16").value = obj.rfid16Time_state;
    document.getElementById("timevalue17").value = obj.rfid17Time_state;
    document.getElementById("timevalue18").value = obj.rfid18Time_state;
    document.getElementById("timevalue19").value = obj.rfid19Time_state;
    document.getElementById("timevalue20").value = obj.rfid20Time_state; */
    isCheckedBtnArr[18] = false;
  }
  if (document.getElementById("namevalue1") != null && isCheckedBtnArr[19] == true) {
    document.getElementById("stateRFID").checked = Boolean(Number(obj.RFID_state));
  }

  if (isCheckedBtnArr[20] == true && document.getElementById("latestRFIDTag") != null) {
    document.getElementById("latestRFIDTag").value = obj.lastRFID_state
  }
}

window.onload = function (event) {
  init();
}
