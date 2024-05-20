#include "lock_ctrl.hpp"
#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define lock_pin_1 17
#define lock_pin_2 16
#define lock_feedback_pin 15

bool is_locked = false;
TaskHandle_t lock_monitor_task_handle;

void brake_lock(){
    digitalWrite(lock_pin_1, HIGH);
    digitalWrite(lock_pin_2, HIGH);
}

void init_lock(){
    pinMode(lock_pin_1, OUTPUT);
    pinMode(lock_pin_2, OUTPUT);
    pinMode(lock_feedback_pin, INPUT);
    brake_lock();
}

void release_lock(){
    digitalWrite(lock_pin_1, LOW);
    digitalWrite(lock_pin_2, HIGH);
    delay(2000);
    brake_lock();
    is_locked = false;

}

void lock_lock(){
    digitalWrite(lock_pin_1, HIGH);
    digitalWrite(lock_pin_2, LOW);
    delay(2000);
    brake_lock();
    is_locked = true;
}

bool compare_lock_state(){
    if(digitalRead(lock_feedback_pin) != is_locked){
        return false;
    }   
    return true;
}