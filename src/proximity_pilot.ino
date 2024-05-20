#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/adc.h"


#define pp_measure_pin 5


TaskHandle_t pp_monitoring_task_handle;

const char *PP_LOG = "PP-Task: ";

proximity_pilot_state_t last_state = PROXIMITY_PILOT_STATE_DISCONNECTED;
proximity_pilot_state_t current_state = PROXIMITY_PILOT_STATE_DISCONNECTED;
float max_cable_capacity = 0;

/**
    * @brief Convert the proximity pilot state to a human readable string
    * 
    * This function converts the proximity pilot state to a human readable string
    * @param proximity_pilot_state_t state
    * @param char *name buffer to return the name
    * @return void
*/
const char* pp_state_to_name(proximity_pilot_state_t state){
    switch(state){
        case PROXIMITY_PILOT_STATE_DISCONNECTED:
            // strcpy(name, "CABLE DISCONNECTED");
            // break;
            return "CABLE DISCONNECTED";

        case PROXIMITY_PILOT_STATE_CONNECTED:
            // strcpy(name, "CABLE CONNECTED");
            // break;
            return "CABLE CONNECTED";
        case PROXIMITY_PILOT_STATE_INVALID:
            // strcpy(name, "INVALID CABLE");
            // break;
            return "INVALID CABLE";
        default:
            // strcpy(name, "UNKNOWN");
            return "UNKNOWN";
            break;
    }
}

/**
    * @brief Initialize the proximity pilot
    * 
    * This function initializes the proximity pilot by setting up the ADC channel
    '@param void
    * @return void
*/
void init_proximity_pilot(void){
    pinMode(pp_measure_pin, INPUT);
}


/**
    * @brief Get the voltage measured by the proximity pilot
    * 
    * This function reads the voltage measured by the proximity pilot
    * @param void
    * @return float Voltage in Volts
*/
float get_proximity_pilot_voltage(){
    float voltage;
    for (int i = 0; i < 5; i++) {
        voltage += analogReadMilliVolts(pp_measure_pin);
    }
    voltage = voltage / 5000;
}

proximity_pilot_state_t get_proximity_pilot_state(){
    return current_state;
}

/*
    * @brief Calculate the Pilot resistance of the cable from ADC Readings
    * 
    * This function calculates the resistance of the cable based on the voltage measured by the proximity pilot
    * @param float voltage
    * @return float Resistance in KOhm

*/
float calc_resistance_from_voltage(float voltage){
    // resistor is connected to Midpoint of 2K resistor to 3.3V and GND
    int R1 = 1000;
    int R2 = 1000;
    float Vcc=3.3;
    // temp denotes equivalent resistance value of R2||R3
    float temp = R1* Vcc/(Vcc-voltage);
    // finally calculate R3 ie Cable resistance
    float R3 = R2*temp/(R2-temp);
    return R3;
}

/*
    * @brief Get the cable capacity from the resistance
    * 
    * This function calculates the cable capacity based on the resistance of the cable
    * @param float resistance
    * @return float Capacity in Amps

*/
float get_cable_capacity_from_resistance(float resistance){
    // 100 Ohm is 63 Amps
    // 220 Ohm is 32 Amps
    // 680 Ohm is 20 Amps
    // 1500 Ohm is 10 Amps
    // Rc tolerence is 3%
    // Reference IEC61851-1-2010 43

    if(resistance <= 100*1.03 && resistance >= 100*0.97){
        return 63;
    }
    else if(resistance <= 220*1.03 && resistance >= 220*0.97){
        return 32;
    }
    else if(resistance <= 680*1.03 && resistance >= 680*0.97){
        return 20;
    }
    else if(resistance <= 1500*1.03 && resistance >= 1500*0.97){
        return 10;
    }
    else{
        return 0;
    }
}

/*
    * @brief Proximity Pilot Monitoring Task
    * 
    * This function is the task that monitors the proximity pilot
    * @param void *args
    * @return void

*/
void pp_monitoring_task(void *args){
    init_proximity_pilot();

    while(1){
        float voltage = get_proximity_pilot_voltage();
        float resistance = calc_resistance_from_voltage(voltage);
        if(resistance >10000){
            current_state = PROXIMITY_PILOT_STATE_DISCONNECTED;
            max_cable_capacity = 0;
        }
        else{
            float capacity = get_cable_capacity_from_resistance(resistance);
            if(capacity > 0){
                current_state = PROXIMITY_PILOT_STATE_CONNECTED;
                max_cable_capacity = capacity;

            }
            else{
                current_state = PROXIMITY_PILOT_STATE_INVALID;
                max_cable_capacity = 0;
            }
        }
        if(current_state != last_state){
            // Handle state change mechanism here
            // Might want to add led color change, cable lock or other logic here
            char current_state_name[20];
            ESP_LOGI(PP_LOG, "Proximity Pilot State Changed: %s", pp_state_to_name(current_state));
            ESP_LOGI(PP_LOG, "Max Cable Capacity: %f A", max_cable_capacity);
        }
        last_state = current_state;
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}

/**
    * @brief Start the proximity pilot monitoring task
    * 
    * This function starts the proximity pilot monitoring task
    * @param void
    * @return void
*/
void start_proximity_pilot_monitoring(){
    xTaskCreate(pp_monitoring_task, "PP Monitoring Task", 2048, NULL, 5, &pp_monitoring_task_handle);
}

/**
    * @brief Stop the proximity pilot monitoring task
    * 
    * This function stops the proximity pilot monitoring task
    * @param void
    * @return void
*/
void stop_proximity_pilot_monitoring(){
    vTaskDelete(pp_monitoring_task_handle);
}
