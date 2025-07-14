#ifndef GLOBALS_H
#define GLOBALS_H
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <ethernet_manager.hpp>
#include <wifi_manager.hpp>
#include <Preferences.h>
#include <esp_wifi.h> 


extern Preferences preferences;

//############### Modbus RTU declaration
// SDM-630
typedef struct {
    volatile bool    enable;
    volatile bool    error;    
    float voltL1, voltL2, voltL3;
    float currL1, currL2, currL3;
    float pwrL1 , pwrL2 , pwrL3;
    float pwrTot;
    float enrImp, enrExp;
} sdm_data_t;
extern sdm_data_t sdm;

// RFID-Reader
typedef struct {
    volatile bool    enable;
    volatile bool    error;            
    uint8_t uid[7];           // rohe 7-Byte-UID
    String  uidStr;           // aktueller Tag “AA:…:GG”
    String  lastUidStr;       // letzter gültiger Tag ≠ "00:…"
} rfid_data_t;
extern rfid_data_t rfid;

//############### CP-Measurements declaration START
extern int cpState;
extern float highVoltage;
typedef enum {
    StateA_NotConnected,
    StateB_Connected,
    StateC_Charge,
    StateD_VentCharge,
    StateE_Error,
    StateF_Fault,
    StateCustom_InvalidValue,
    StateCustom_OutOffRange,
    StateCustom_CpRelayOff,
    StateCustom_DutyCycle_100,
    StateCustom_DutyCycle_0,
} charging_state_t;

extern charging_state_t currentCpState;
const char* cpStateToName(charging_state_t state);

extern volatile charging_state_t vCurrentCpState;
extern volatile uint32_t lastStateChangeTime;
extern volatile charging_state_t currentCpStateDelay;

//############### CP-Measurements declaration END

// Deklaration des NeoPixel-Strip-Objekts
extern NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip;
extern wifi_sta_start_config_t wifi_sta_config;
extern int16_t mbTcpRegRead09;          //LED-Status-Steuern
#endif 