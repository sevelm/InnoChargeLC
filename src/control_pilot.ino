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
#include "rom/ets_sys.h"

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

#define cp_scaling_factor_a 0.0053
#define cp_scaling_factor_b -9.3632

bool cp_relay_status = false;
static portMUX_TYPE cp_adc_spinlock = portMUX_INITIALIZER_UNLOCKED;

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
    '@param float duty in percentage
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
    if(current >= 6 && current <= 51){
        return current/0.6;
    }else if(current > 51 && current <= 80){
        return current/2.5+64;
    }
    ESP_LOGE(CP_LOG, "Current out of range defaulting to 6A");
    return 10;
}

/**
    * @brief Set the charging current
    * 
    * This function sets the charging current by setting the duty cycle of the control pilot PWM generator
    '@param float current
    * @return void
*/
void set_charging_current(float current){
    float duty = get_duty_from_current(current);
    set_control_pilot_duty(duty);
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
    float high_voltage=0;
    // taskENTER_CRITICAL(&cp_adc_spinlock);
    sync_cp_edge(RISING_EDGE);
    ets_delay_us(20);
    high_voltage= (adc1_get_raw(cp_measure_channel)+adc1_get_raw(cp_measure_channel))/2;
    // taskEXIT_CRITICAL(&cp_adc_spinlock);
    // ESP_LOGI(CP_LOG, "RAW ADC Value: %f", high_voltage);
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
    float low_voltage=0;
    // taskENTER_CRITICAL(&cp_adc_spinlock);
    sync_cp_edge(FALLING_EDGE);
    ets_delay_us(20);
    low_voltage= (adc1_get_raw(cp_measure_channel)+adc1_get_raw(cp_measure_channel)+adc1_get_raw(cp_measure_channel))/3;
    // taskEXIT_CRITICAL(&cp_adc_spinlock);
    low_voltage= low_voltage*cp_scaling_factor_a+cp_scaling_factor_b;
    // ESP_LOGI(CP_LOG, "Low Voltage: %f", low_voltage);
    if(low_voltage<0){
        low_voltage = low_voltage*1.5;
    }
    return low_voltage;
}
