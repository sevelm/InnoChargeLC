


/*###################### Duty Cycles according to IEC 61851-1
  Available current (in A) = Duty cycle (in%) · 0.6 A
  Duty cycle (in%) = Available current (in A) ÷ 0.6 A

   97 %                        80 A   (EU)
   95 %                        77.5 A (EU)
   90 %                        65 A   (EU)
   85 %                        51 A   (EU)
   80 %                        48 A   (EU)
   70 %                        42 A   (EU)
   60 %                        36 A   (EU)
   50 %  30 A cont 36 A peak   30 A   (EU)
   40 %  24 A cont 30 A peak   24 A   (EU)
   30 %  18 A cont 22 A peak   18 A   (EU)
   27 %                        16 A   (EU)
   25 %  15 A cont 20 A peak   15 A   (EU)
   16 %                        10 A   (EU)
   10 %                         6 A   (EU)
    8 %                         5 A   (EU)

State:   Pilot Voltage:  EV Resistance:  Description:       Analog theoretic: (if pwm is on 1khz)
  State A       12V            N/A         Not Connected           
  State B        9V           2.7K         Connected               
  State C        6V          882 Ohm       Charging                
  State D        3V          246 Ohm       Ventilation Required    
  State E        0V            N/A         No Power                
  State F      -12V            N/A         EVSE Error  
######################## DutyCycles lt. IEC 61851-1

############################  For input in kW from 230VAC  - 400VAC

              Voltage  |  Current  |  Power  |  DutyCycle
  -----------------------------------------------------------------  
    Minimum    at 230V =    4.8A  =   1.1kW =      8% DutyCycle
    Min 6A     at 230V =    6.0A  =   1.4kW =   10.0% DutyCycle *according to standard minimum       
    Middle     at 230V =   13.0A  =   3.0kW =   21.8% DutyCycle
    Max 16A    at 230V =   16.0A  =   3.7kW =   26.7% DutyCycle
    Max 32A    at 230V =   32.0A  =   7.4kW =   33.3% DutyCycle

    -> DutyCycle = (W / 230) / 0.6
    
    Minimum    at 400V =    4.8A  =   3.3kW =      8% DutyCycle
    Min 6A     at 400V =    6.0A  =   4.2kW =   10.0% DutyCycle *according to standard minimum     
    Middle 9kw at 400V =   13.0A  =   9.0kW =   21.8% DutyCycle    
    Middle 11kw at 400V =   16.0A  =  11.0kW =   26.7% DutyCycle
    Maximum    at 400V =   32.0A  =  22.0kW =   53.4% DutyCycle    

    -> DutyCycle = (W / 692) / 0.6
 */


//######################### CP-State with on-off delay

 //     fixedValues[4] -> Highest values       -> State A (Standby)
 //     fixedValues[3] -> Second highest values -> State B (Connected)
 //     fixedValues[2] -> Third highest values -> State C (Charge)
 //     fixedValues[1] -> Fourth highest values -> State D (Ventilation)
 //     fixedValues[0] -> Fifth highest values -> State E (Error)
 //     cpState  0=A -> NotConnected; 1=B -> Connected; 2=C -> ChargingActive; 3=D -> Ventilation ChargingActive; 4=E Fault, 5=F Fault, 6=Switch is off, 7=DutyCycle 100%, 8=DutyCycle 0%,

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Arduino.h"

#include "control_pilot.hpp"
#include "AA_globals.h"
#include "ledEffect.hpp"
#include "A_Task_CP.hpp"

volatile charging_state_t vCurrentCpState;
volatile uint32_t lastStateChangeTime = 0;
volatile charging_state_t currentCpStateDelay;

// Function to determine charging state based on voltage, duty cycle and switch
charging_state_t actualCpState(float highVoltage, float lowVoltage) {
    charging_state_t state;
    if (get_cp_relays_status() != 1) {
        state = StateCustom_CpRelayOff;
    } else {
        if (highVoltage == 12 || highVoltage == 11) { 
            state = StateA_NotConnected;
        } else if (highVoltage == 9 || highVoltage == 8) { 
            state = StateB_Connected; 
        } else if (highVoltage == 6 || highVoltage == 5) { 
            state = StateC_Charge; 
        } else if (highVoltage == 3 || highVoltage == 2) { 
            state = StateD_VentCharge; 
        } else if (highVoltage > 12 || highVoltage < 2) {
            state = StateE_Error; 
        } 
        if (round(get_control_pilot_duty()) == 100) { 
            state = StateCustom_DutyCycle_100; }
        if (round(get_control_pilot_duty()) == 0) { 
            state = StateCustom_DutyCycle_0; }
    }    
    return state;
}


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


void A_Task_CP(void *pvParameter){
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Setup ///////////////////////////////////////////////////
    init_control_pilot();
    set_charging_current(16);
    turn_on_cp_relay();

    while (1) {
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////


        highVoltage = get_high_voltage();
        highVoltage = round(highVoltage);

        //set_control_pilot_0();
        currentCpStateDelay = actualCpState(highVoltage,0);

        // CP-Delay Timer to debounce the signals
        if (vCurrentCpState == currentCpStateDelay) {
        lastStateChangeTime = xTaskGetTickCount();  
        } else if ((xTaskGetTickCount() - lastStateChangeTime) >= 500) {
            vCurrentCpState = currentCpStateDelay;
        }

        if (vCurrentCpState == StateC_Charge || vCurrentCpState == StateD_VentCharge) {
            turn_relay_on();
        } else {
            turn_relay_off();
        }

        currentCpState = vCurrentCpState;
        vTaskDelay(5 / portTICK_PERIOD_MS); // Adjusted delay
    }
}
