#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "control_pilot.hpp"

#define cp_gen_pin 8
#define cp_feedback_pin 37
#define cp_measure_pin 4
#define cp_relay_pin 18
#define pp_measure_pin 5
#define cp_gen_freq 1000
#define cp_gen_duty 50


#define cp_control_channel 0

// Voltage Scaling Part
#define r1 137.7
#define r2 56
#define r3 33
#define cp_scaling_factor_a (r1*r2+r2*r3+r1*r3)/(r2*r3)
#define cp_scaling_factor_b (5*r1*r3)/(r2*r3)

static const char *CP_TAG = "Control Pilot: ";

void init_control_pilot(void){
    pinMode(cp_gen_pin, OUTPUT);
    ledcSetup(cp_control_channel, cp_gen_freq, 10);
    ledcAttachPin(cp_gen_pin, cp_control_channel);
    pinMode(cp_feedback_pin, INPUT);
    pinMode(cp_relay_pin, OUTPUT);
}

void set_control_pilot(float duty){
    // Duty is in %, so we need to convert it to 0-1023
    int i_duty=1023*duty/100;
    ledcWrite(cp_control_channel, i_duty);
}

cp_measurements_t measure_control_pilot(void){
    cp_measurements_t measurements;
    uint64_t start_time=micros();
    while(digitalRead(cp_feedback_pin)==LOW){
        // Wait for the pin to go high atleast 3 ms
        if(micros()-start_time>2000){
            break;
        }
    }
    measurements.high_voltage=analogRead(cp_measure_pin)*3.1/4095;
    start_time=micros();
    while(digitalRead(cp_feedback_pin)==HIGH){
        // Wait for the pin to go low
        if(micros()-start_time>2000){
            break;
        }
    }
    measurements.low_voltage=analogRead(cp_measure_pin)*3.1/4095;



    measurements.high_voltage=measurements.high_voltage*cp_scaling_factor_a-cp_scaling_factor_b;
    measurements.low_voltage=measurements.low_voltage*cp_scaling_factor_a-cp_scaling_factor_b;

    return measurements;
}

void turn_cp_relay_on(void){
    digitalWrite(cp_relay_pin, HIGH);
}

void turn_cp_relay_off(void){
    digitalWrite(cp_relay_pin, LOW);
}
float get_pp_status(void){
    float pp_val=analogRead(pp_measure_pin);
    pp_val=pp_val*3.3/4095;
    return pp_val;
}

void control_pilot_task(void *pvParameter){
    init_control_pilot();
    cp_measurements_t measurements;
    ESP_LOGI(CP_TAG, "Control Pilot Task Started");
    set_control_pilot(50);
    turn_cp_relay_on();

    while(1){
        ESP_LOGI(CP_TAG, "Control Pilot Task Running");
        measurements=measure_control_pilot();
        ESP_LOGI(CP_TAG, "High Voltage: %f, Low Voltage: %f", measurements.high_voltage, measurements.low_voltage);
        float pp_val=get_pp_status();
        ESP_LOGI(CP_TAG, "PP Voltage: %fv", pp_val);
        vTaskDelay(3000/portTICK_PERIOD_MS);
    }
}