#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "A_TaskLow.hpp"
#include "SPIFFS.h"


#include "AA_globals.h"
#include "esp_netif.h"
#include "ethernet_manager.hpp"


const char *CP_LOGI = "Task_Low: ";


const char *cpStateToName(charging_state_t state){
    switch(state){
        case StateA_NotConnected:
            return "Charging State A";
        case StateB_Connected:
            return "Charging State B";
        case StateC_Charge:
            return "Charging State C";
        case StateD_VentCharge:
            return "Charging State D";
        case StateE_Error:
            return "Charging State E";
        case StateF_Fault:
            return "Charging State F";
        case StateCustom_CpRelayOff:
            return "Charging State CP-Relay OFF";
        case StateCustom_DutyCycle_100:
            return "Charging State DutyCycle 100%";
        case StateCustom_DutyCycle_0:
            return "Charging State DutyCycle 0%";
        default:
            return "Invalid State";
    }
}






//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
void A_TaskLow(void *pvParameter){
 char eth_ip[16];

//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
    while(1){
        get_eth_ip(eth_ip);
        ESP_LOGI(CP_LOGI, "IP: %s", eth_ip);
        ESP_LOGI(CP_LOGI, "highVoltage: %f", highVoltage);
        ESP_LOGI(CP_LOGI, "CP: %s", cpStateToName(currentCpState));

        vTaskDelay(2000/portTICK_PERIOD_MS); // verzögere den Task um sekunden

    }
}