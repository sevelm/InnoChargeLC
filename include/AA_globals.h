#ifndef GLOBALS_H
#define GLOBALS_H
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include "ethernet_manager.hpp"
#include <Preferences.h>

extern Preferences preferences;

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


#endif 