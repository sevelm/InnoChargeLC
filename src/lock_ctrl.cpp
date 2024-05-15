#include "lock_ctrl.hpp"
#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define lock_pin_1 17
#define lock_pin_2 16
#define lock_feedback_pin 15

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

}

void lock_lock(){
    digitalWrite(lock_pin_1, HIGH);
    digitalWrite(lock_pin_2, LOW);
    delay(2000);
    brake_lock();
}

void lock_monitor_task(void *pvParameter){
    init_lock();
    while(1){
        if(digitalRead(lock_feedback_pin) == HIGH){
         //   ESP_LOGI("Lock Monitor : ", "Lock is locked");
        }else{
         //   ESP_LOGI("Lock Monitor : ", "Lock is not locked");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
