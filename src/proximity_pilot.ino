#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/adc.h"


#define pp_measure_pin 5



const char *PP_LOG = "PP-Task: ";

proximity_pilot_state_t last_state = PROXIMITY_PILOT_STATE_DISCONNECTED;
proximity_pilot_state_t current_state = PROXIMITY_PILOT_STATE_DISCONNECTED;
float max_cable_capacity = 0;
static portMUX_TYPE pp_adc_spinlock = portMUX_INITIALIZER_UNLOCKED;
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
    portENTER_CRITICAL(&pp_adc_spinlock);
    for (int i = 0; i < 5; i++) {
        voltage += analogReadMilliVolts(pp_measure_pin);
    }
    portEXIT_CRITICAL(&pp_adc_spinlock);
    voltage = voltage / 5000;
    return voltage;
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
