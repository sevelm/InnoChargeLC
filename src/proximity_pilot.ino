#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"


#define pp_measure_pin 5


const char *PP_LOG = "PP-Task: ";


bool cable_connected = false;


float get_proximity_pilot_voltage(){
    float voltage;
    for (int i = 0; i < 5; i++) {
        voltage += adc1_get_raw(pp_measure_pin);
    }
    voltage = voltage / 5000;
}

float get

