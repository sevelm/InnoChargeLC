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
    volatile bool    invSign;    
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

// Struktur mit Status + Zusatzinformationen
typedef struct {
    charging_state_t state;         // eigentlicher CP-State
    bool vehicleConnected;          // Fahrzeug verbunden
    bool chargingActive;            // Ladevorgang aktiv
    bool threePhaseActive;          // Ladevorgang mit 400VAC
} charging_status_t;

// Schalten von 230VAC zu 400VAC
extern volatile float g_setChargingPower_kW;
extern volatile bool threePhaseActive;
extern volatile bool stateRelayL1N;
extern volatile bool stateRelayL2L3;
extern volatile bool switchToL1N;
extern volatile bool switchToL2L3;

extern charging_status_t currentCpState;
const char* cpStateToName(charging_state_t state);

extern volatile charging_status_t vCurrentCpState;
extern volatile uint32_t lastStateChangeTime;
extern volatile charging_status_t currentCpStateDelay;
extern float getCpDuty;
extern float setCpDuty;

//############### CP-Measurements declaration END

// Deklaration des NeoPixel-Strip-Objekts
extern NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip;
extern wifi_sta_start_config_t wifi_sta_config;
extern int16_t mbTcpRegRead09;          //LED-Status-Steuern

// Dipswitch 2 (rechts) 
constexpr gpio_num_t RESCUE_PIN = GPIO_NUM_2;   
extern bool rescueMode;                         // true ⇔ DIP „ON“
#endif 