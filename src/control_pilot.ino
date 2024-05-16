#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "control_pilot.hpp"
#include "AA_globals.h"
#include "ledEffect.hpp"
#include "math.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "stdbool.h"

#define cp_gen_pin 8
#define cp_feedback_pin 37
#define cp_measure_pin 4
#define cp_relay_pin 18
#define cp_gen_freq 1000
#define cp_gen_duty 50
#define cp_measure_channel ADC1_CHANNEL_3
#define cp_control_channel 0

#define RISING_EDGE 1
#define FALLING_EDGE 0

#define cp_scaling_factor_a 0.005263
#define cp_scaling_factor_b -9.3632

bool cp_relay_state = false;

const char *CP_LOG = "Control Pilot: ";
/**
    * @brief Initialize the control pilot
    * 
    * This function initializes the control pilot by setting up the PWM generator and the ADC channel
    '@param void
    * @return void
*/
void init_control_pilot(void){
    pinMode(cp_gen_pin, OUTPUT);
    ledcSetup(cp_control_channel, cp_gen_freq, 12);
    ledcAttachPin(cp_gen_pin, cp_control_channel);
    pinMode(cp_feedback_pin, INPUT);
    pinMode(cp_relay_pin, OUTPUT);
    digitalWrite(cp_relay_pin, LOW);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(cp_measure_channel, ADC_ATTEN_DB_11);
}

/**
    * @brief Set the control pilot duty cycle
    * 
    * This function sets the duty cycle of the control pilot PWM generator
    '@param float duty
    * @return void
*/
void set_control_pilot_duty(float duty){
    int i_duty = 4095 * duty / 100;
    ledcWrite(cp_control_channel, i_duty);
}

/*
    * @brief Get the duty cycle from the current
    * 
    * This function calculates the duty cycle from the current
    '@param float current
    * @return float duty
*/
float get_duty_from_current(float current){

}

/**
    * @brief Turn on the control pilot relay
    * @param void
    * @return void

*/
void turn_on_cp_relay(){
    digitalWrite(cp_relay_pin, HIGH);
    cp_relay_status = true;
}

/**
    * @brief Turn off the control pilot relay
    * @param void
    * @return void

*/
void turn_off_cp_relay(){
    digitalWrite(cp_relay_pin, LOW);
    cp_relay_status = false;
}

/**
    * @brief Set the control pilot to standby
    * @param void
    * @return void

*/
void set_control_pilot_standby(){
    set_control_pilot_duty(100);
}


/**
    * @brief Get the status of the control pilot relay
    * @param void
    * @return bool cp_relay_status

*/
bool get_cp_relays_status(){
    return cp_relay_status;
}


/**
    * @brief Synchronize the control pilot edge
    * 
    * This function synchronizes the control pilot edge
    '@param bool edge
    * @return void
*/
void sync_cp_edge(bool edge){
    uint64_t start_time = micros();
    while(digitalRead(cp_feedback_pin) == edge){
        if( micros() - start_time > 2000){
            ESP_LOGI(CP_LOG, "CP Pulse Not Detected! Aborting");
            return;
        }
    }
    while(digitalRead(cp_feedback_pin) != edge){
        if( micros() - start_time > 2000){
            ESP_LOGI(CP_LOG, "CP Pulse Not Detected! Aborting");
            return;
        }
    }
    return;
}

/**
    * @brief Get the high voltage
    * 
    * This function gets the high voltage
    '@param void
    * @return float high_voltage
*/
float get_high_voltage(){
    float high_voltage;
    sync_cp_edge(RISING_EDGE);
    high_voltage= (adc1_get_raw(cp_measure_channel)+adc1_get_raw(cp_measure_channel))/2;
    high_voltage= high_voltage*cp_scaling_factor_a+cp_scaling_factor_b;
    return high_voltage;
}

/**
    * @brief Get the low voltage
    * 
    * This function gets the low voltage
    '@param void
    * @return float low_voltage
*/
float get_low_voltage(){
    float low_voltage;
    sync_cp_edge(FALLING_EDGE);
    low_voltage= (adc1_get_raw(cp_measure_channel)+adc1_get_raw(cp_measure_channel))/2;
    low_voltage= low_voltage*cp_scaling_factor_a+cp_scaling_factor_b;
    return low_voltage;
}
