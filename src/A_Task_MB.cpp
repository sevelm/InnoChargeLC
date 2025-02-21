#include <ModbusTCP.h>
#include <ModbusRTU.h>
//#include "esp_modbus_common.h"
//#include "esp_modbus_slave.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Arduino.h"

#include "control_pilot.hpp"
#include "AA_globals.h"
#include "ledEffect.hpp"
#include "A_Task_MB.hpp"


// UART2: RX = GPIO 45, TX = GPIO 35, RTS = GPIO 42
#define RX_PIN 45
#define TX_PIN 35
#define RTS_PIN 42

// Modbus-Register
uint16_t mbRegChargeCurrent = 0;
uint16_t mbRegChargePower = 1;





// Instanzen für Modbus RTU und TCP
ModbusRTU mbRTU;
ModbusTCP mbTCP;

HardwareSerial ModbusSerial(2);  // UART2 für Modbus RTU


const char *MB_LOGI = "Task_MB: ";




/**
    * @brief Set the charging current Modbus-Register -> control_pilot.cpp
    * 
    * @param float current
    * @return void
*/
void set_charging_current_mb(float current, int mbRegCurrent){
    mbTCP.Hreg(mbRegCurrent, current);
}

/**
    * @brief Set the charging power Modbus-Register -> control_pilot.cpp
    * 
    * @param float power
    * @return void
*/
void set_charging_power_mb(float power, int mbRegPower){
    mbTCP.Hreg(mbRegPower, round(power));
}

/**
    * @brief Modbus-Register set the charging current
    * 
    * @param float current
    * @return void
*/
void mb_set_charging_current(int mbRegCurrent){
    float duty = get_control_pilot_duty();
    if (mbTCP.Hreg(mbRegCurrent) != round(get_current_from_duty(duty))) {
        set_charging_current(mbTCP.Hreg(mbRegCurrent));
    }
}

/**
    * @brief Modbus-Register set the charging power
    * 
    * @param float power
    * @return void
*/
void mb_set_charging_power(int mbRegPower){
    float duty = get_control_pilot_duty();
    if (mbTCP.Hreg(mbRegPower) != round(get_power_from_duty(duty)/10)) {
        set_charging_power(mbTCP.Hreg(mbRegPower));
    }
}



void A_Task_MB(void *pvParameter) {
    //////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
    


    // 2. Modbus TCP starten
    mbTCP.server(); // Modbus TCP Server auf Standard-Port 502

    // 3. Modbus RTU starten
    //Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    ModbusSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN); // UART-Pins initialisieren
    ModbusSerial.setPins(RX_PIN, TX_PIN, RTS_PIN);  // RTS aktivieren
    mbRTU.begin(&ModbusSerial); // RTU mit UART verbinden
    mbRTU.slave(1); // RTU Slave-ID festlegen

    // 4. Register für TCP und RTU hinzufügen (gleiche Basis)
    for (uint16_t i = 0; i < 10; i++) {
        mbTCP.addHreg(i, 0); // Modbus TCP Register
    }

    for (uint16_t i = 11; i < 20; i++) {
        mbRTU.addHreg(i, 0); // Modbus RTU Register
    }



 //   Serial.println("Modbus RTU und TCP erfolgreich gestartet.");

    //////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
    while (1) {
        mbTCP.task(); // Modbus TCP abarbeiten


//        set_charging_current(16);
//        set_charging_power(power*10);

  //              doc["targetChargeCurrent"] = );
  



        mbRTU.task(); // Modbus RTU abarbeiten

        // Beispiel: Register 0 inkrementieren
        static uint32_t lastMillis = 0;
        if (millis() - lastMillis > 1000) { // Alle 1 Sekunde
            lastMillis = millis();

            mb_set_charging_current(mbRegChargeCurrent);
          //  mb_set_charging_power(mbRegChargePower);

mbTCP.Hreg(1, 100);

mbRTU.Hreg(11, 100);

           // float duty = get_control_pilot_duty();
          //  if (mbTCP.Hreg(0) != round(get_current_from_duty(duty))) {
          //      set_charging_current(mbTCP.Hreg(0));



        }




        vTaskDelay(100 / portTICK_PERIOD_MS); // Kleine Pause
    }
}

