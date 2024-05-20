#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "lock_ctrl.hpp"
#include "control_pilot.hpp"
#include "proximity_pilot.hpp"
#include "relay_ctrl.hpp"
#include "charging_manager.hpp"


#define MIN(a,b) ((a) < (b) ? (a) : (b))


const char *CM_LOGI = "Charging Manager : ";

float max_charger_current = 6.0;
float max_cable_current = 0;

TaskHandle_t charging_manager_task_handle;
TaskHandle_t session_reporting_task_handle;
TaskHandle_t pp_monitoring_task_handle;


    proximity_pilot_state_t current_pp_state = PROXIMITY_PILOT_STATE_DISCONNECTED;
    proximity_pilot_state_t last_pp_state = PROXIMITY_PILOT_STATE_DISCONNECTED;
    charging_state_t current_cp_state = charging_state_a;
    charging_state_t last_cp_state = charging_state_a;

    float high_voltage, low_voltage;

void session_reporting_task(void *pvParameter){
    while(1){
        ESP_LOGI(CM_LOGI, "Reporting Session: High Voltage: %f, Low Voltage: %f, CP State: %s, PP State: %s", high_voltage, low_voltage, cp_state_to_name(current_cp_state), pp_state_to_name(current_pp_state));
        vTaskDelay(5000/portTICK_PERIOD_MS);
    }
}

const char *cp_state_to_name(charging_state_t state){
    switch(state){
        case charging_state_a:
            return "Charging State A";
        case charging_state_b:
            return "Charging State B";
        case charging_state_c:
            return "Charging State C";
        case charging_state_d:
            return "Charging State D";
        case charging_state_e:
            return "Charging State E";
        case charging_state_f:
            return "Charging State F";
        default:
            return "Invalid State";
    }
}


void handle_pp_state_change(proximity_pilot_state_t last_state, proximity_pilot_state_t current_state){
    if(current_state == PROXIMITY_PILOT_STATE_INVALID){
        turn_relay_off();
        ESP_LOGE(CM_LOGI, "Invalid Cable Detected, Please Plug in Valid Cable");
        release_lock();
        pause_charging_manager();
        turn_off_cp_relay();
        set_control_pilot_standby();
    }
    else if(current_state == PROXIMITY_PILOT_STATE_DISCONNECTED && last_state == PROXIMITY_PILOT_STATE_CONNECTED){
        turn_relay_off();
        ESP_LOGI(CM_LOGI, "Cable Disconnected, Releasing Lock");
        release_lock();
        pause_charging_manager();
        turn_off_cp_relay();
        set_control_pilot_standby();
    }
    else if(current_state == PROXIMITY_PILOT_STATE_CONNECTED && last_state == PROXIMITY_PILOT_STATE_DISCONNECTED){
        ESP_LOGI(CM_LOGI, "Cable Connected, Acquiring Lock");
        // get max cable capacity
        // max_cable_current = get_max_cable_capacity();
        lock_lock();
        turn_on_cp_relay();
        last_cp_state = charging_state_a;
        resume_charging_manager();
    }else{
        ESP_LOGE(CM_LOGI, "Invalid State Transition, Last State: %d, Current State: %d", last_state, current_state);
    }

}



void handle_cp_state_change(charging_state_t last_state, charging_state_t current_state){
    switch(current_state){
        case charging_state_a:
            ESP_LOGI(CM_LOGI, "Charging State A transitioned from %s, Vehicle disconnected.", cp_state_to_name(last_state));
            set_control_pilot_standby();
            turn_relay_off();
            break;
        case charging_state_b:
            if(last_state == charging_state_a){
                ESP_LOGI(CM_LOGI, "Vehicle Connected, Broadcasting Charging Current");
                set_charging_current(MIN(max_charger_current, max_cable_current));
                turn_relay_off();
            }
            else{
                ESP_LOGI(CM_LOGI, "Vehicle turned off charging from %s, Probably Full", cp_state_to_name(last_state));
                turn_relay_off();
            }
            break;
        case charging_state_c:
            if(last_state == charging_state_b){
                ESP_LOGI(CM_LOGI, "Charging State C transitioned from B, Vehicle Ready to Charge.");
                // check authorization here
                turn_relay_on();
                // Also start metering and Stuffs
            }
            else{
                ESP_LOGE(CM_LOGI, "Invalid State Transition to C from %s", cp_state_to_name(last_state));
                turn_relay_off();
            }
            break;
        case charging_state_d:
            if(last_state == charging_state_b){
                ESP_LOGI(CM_LOGI, "Charging State D transitioned from B, Vehicle Ready to Charge.");
                // check authorization here
                turn_relay_on();
            }
            else{
                ESP_LOGE(CM_LOGI, "Invalid State Transition to D from %s", cp_state_to_name(last_state));
                turn_relay_off();
            }
            break;
        case charging_state_e:
            ESP_LOGE(CM_LOGI, "Charging State E transitioned from %s, Stopping Charging. Pilot Shorted to GND", cp_state_to_name(last_state));
            set_control_pilot_standby();
            turn_relay_off();
            pause_charging_manager();
            break;
        case charging_state_f:
            ESP_LOGE(CM_LOGI, "Charging State F transitioned from %s, Stopping Charging. Vehicle is Not Valid", cp_state_to_name(last_state));
            set_control_pilot_standby();
            turn_relay_off();
            pause_charging_manager();
            break;
        default:
            break;
    }
}

void charging_manager_task(void *args){
    ESP_LOGI(CM_LOGI, "Starting Charging Manager Task");
    // init CP , Relay, Lock
    init_relay_ctrl();

    init_control_pilot();
    set_control_pilot_standby();
    turn_on_cp_relay();
    
    init_lock();
    xTaskCreate(session_reporting_task, "Session Reporting Task", 4096, NULL, 5, NULL);

    ESP_LOGI("Charging Manager", "Charging Manager Initialized");
    while(1){
        high_voltage = get_high_voltage();
        low_voltage = get_low_voltage();
        high_voltage = round(high_voltage);
        low_voltage = round(low_voltage);
        // ESP_LOGI(CM_LOGI, "High Voltage: %f, Low Voltage: %f", high_voltage, low_voltage);

            // high voltage is 12V, 9V, 6V, 3V, rest are invalid
            if(high_voltage == 12){
                current_cp_state = charging_state_a;
            }
            else if(high_voltage == 9){
                current_cp_state = charging_state_b;
            }
            else if(high_voltage == 6){
                current_cp_state = charging_state_c;
            }
            else if(high_voltage == 3){
                current_cp_state = charging_state_d;
            }
            else{
                ESP_LOGE(CM_LOGI, "Invalid High Voltage Detected: %f", high_voltage);
                current_cp_state = charging_state_e;
            }
            //  if low voltage is not 12v or -12V, then it is invalid
        if(last_cp_state == charging_state_a){
           if(low_voltage != 12 && low_voltage != 9){
               current_cp_state = charging_state_f;
           }
        //    handle_cp_state_change(last_cp_state, current_cp_state);
        }
        else if(low_voltage!=-12){
            current_cp_state = charging_state_f;
            // handle_cp_state_change(last_cp_state, current_cp_state);
        }

        if(current_cp_state != last_cp_state){
            handle_cp_state_change(last_cp_state, current_cp_state);
        }
        last_cp_state = current_cp_state;

        // run every 200ms
        vTaskDelay(200/portTICK_PERIOD_MS);
    }


}

charging_state_t get_cp_state(){
    return current_cp_state;
}
void start_charging_manager(){
    xTaskCreate(charging_manager_task, "Charging Manager Task", 4096, NULL, 1, &charging_manager_task_handle);
}
void pause_charging_manager(){
    vTaskSuspend(charging_manager_task_handle);
}
void resume_charging_manager(){
    vTaskResume(charging_manager_task_handle);
}

void pp_monitoring_task(void *args){

    start_charging_manager();
    vTaskDelay(50/portTICK_PERIOD_MS);
    pause_charging_manager();
    while(1){
        float voltage = get_proximity_pilot_voltage();
        // ESP_LOGI(CM_LOGI, "Proximity Pilot Voltage: %f V", voltage);
        float resistance = calc_resistance_from_voltage(voltage);
        if(resistance < 0){
            resistance = 2000;
        }
        // ESP_LOGI(CM_LOGI, "Proximity Pilot Resistance: %f Ohm", resistance);
        if(resistance >1000){
            current_pp_state = PROXIMITY_PILOT_STATE_DISCONNECTED;
            max_cable_current = 0;
        }
        else{
            float capacity = get_cable_capacity_from_resistance(resistance);
            if(capacity > 0){
                current_pp_state = PROXIMITY_PILOT_STATE_CONNECTED;
                max_cable_current = capacity;

            }
            else{
                current_pp_state = PROXIMITY_PILOT_STATE_INVALID;
                max_cable_current = 0;
            }
        }
        if(current_pp_state != last_pp_state){
            handle_pp_state_change(last_pp_state, current_pp_state);
            ESP_LOGI(CM_LOGI, "Proximity Pilot State Changed: %s", pp_state_to_name(current_pp_state));
            ESP_LOGI(CM_LOGI, "Max Cable Capacity: %f A", max_cable_current);
        }
        last_pp_state = current_pp_state;
        vTaskDelay(200/portTICK_PERIOD_MS);
    }

}

proximity_pilot_state_t get_pp_state(){
    return current_pp_state;
}