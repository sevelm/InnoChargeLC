#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "A_TaskLow.hpp"
#include "AA_globals.h"


const char *CP_LOGI = "Task_Low: ";



//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
void A_TaskLow(void *pvParameter){


//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
    while(1){
        ESP_LOGI(CP_LOGI, "High Voltage: %f, Low Voltage: %f", measurements.high_voltage, measurements.low_voltage);
     //   ESP_LOGI(CP_TAG, "PP Voltage: %fv", pp_val);

     /*   ESP_LOGI(CP_LOGI, "###################################"); 
        ESP_LOGI(CP_LOGI, "count %d", measurements.highVoltRawCount); 
        ESP_LOGI(CP_LOGI, "High 00 %f", measurements.highVoltRaw[0]); 
        ESP_LOGI(CP_LOGI, "High 01 %f", measurements.highVoltRaw[1]);  
        ESP_LOGI(CP_LOGI, "High 02 %f", measurements.highVoltRaw[2]);  
        ESP_LOGI(CP_LOGI, "High 03 %f", measurements.highVoltRaw[3]);  
        ESP_LOGI(CP_LOGI, "High 04 %f", measurements.highVoltRaw[4]);  
        ESP_LOGI(CP_LOGI, "High 05 %f", measurements.highVoltRaw[5]);                  
        ESP_LOGI(CP_LOGI, "High 06 %f", measurements.highVoltRaw[6]);  
        ESP_LOGI(CP_LOGI, "High 07 %f", measurements.highVoltRaw[7]);  
        ESP_LOGI(CP_LOGI, "High 08 %f", measurements.highVoltRaw[8]);  
        ESP_LOGI(CP_LOGI, "High 09 %f", measurements.highVoltRaw[9]);  
        ESP_LOGI(CP_LOGI, "High 10 %f", measurements.highVoltRaw[10]);  

        ESP_LOGI(CP_LOGI, "High Voltage: %f, Low Voltage: %f", measurements.high_voltage, measurements.low_voltage);
*/
        cpState++;

if (cpState > 10){
    cpState=0;
}
        ESP_LOGI(CP_LOGI, "cpState: %d", cpState);

        vTaskDelay(5000/portTICK_PERIOD_MS); // verzögere den Task um sekunden

    }
}