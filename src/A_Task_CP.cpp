


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
    Minimum     at 230V =    4.8A  =   1.1kW =    7.9% DutyCycle
    Min 6A      at 230V =    6.0A  =   1.4kW =   10.1% DutyCycle *according to standard minimum       
    Middle      at 230V =   13.0A  =   3.0kW =   21.7% DutyCycle
    Max 16A     at 230V =   16.0A  =   3.7kW =   26.8% DutyCycle
    Max 32A     at 230V =   32.0A  =   7.4kW =   53.6% DutyCycle

    -> DutyCycle = (W / 230) / 0.6
    
    Minimum     at 400V =    4.8A  =   3.3kW =    7.9% DutyCycle
    Min 6A      at 400V =    6.0A  =   4.2kW =   10.1% DutyCycle *according to standard minimum     
    Middle 9kw  at 400V =   13.0A  =   9.0kW =   21.6% DutyCycle    
    Middle 11kw at 400V =   16.0A  =  11.0kW =   26.4% DutyCycle
    Maximum     at 400V =   32.0A  =  22.0kW =   52.9% DutyCycle    

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
volatile uint32_t lastStateChangeTimeState = 0;
volatile uint32_t lastStateChangeTimeDuty = 0;
volatile charging_state_t currentCpStateDelay;



/**************************************************************************
 *  Aktualisierte State‑Funktion mit integriertem RCM‑Latch & CP‑Schnüffeln
 **************************************************************************/
charging_state_t actualCpState(float highVoltage, float /*lowVoltage*/)
{
    /* --------- Konstanten & Tabellen --------- */
    const int vA   = 123, vB = 93, vC = 63, vD = 36;
    const int off  = 15,  maxV = 130, minV = 19;
    const TickType_t sniffInt  = 1000 / portTICK_PERIOD_MS;   // 1 s
    const TickType_t sniffDur  =   30 / portTICK_PERIOD_MS;   // 30 ms
    const float hvResetThresh  = 10.8f;                       // +12 V ≈ Stecker ab

    /* --------- Statische Variablen (behalten Wert über Aufrufe) --------- */
    static bool         faultLatched   = false;               // Fault aktiv
    static enum { SNF_IDLE, SNF_WAIT } sniffStage = SNF_IDLE; // Schnüffel‑FSM
    static TickType_t   sniffNext    = 0;                     // 1‑s‑Timer
    static TickType_t   sniffStart   = 0;                     // Beginn Fenster

    TickType_t now = xTaskGetTickCount();
    int hvInt = round(highVoltage * 10);                      // 0.1 V‑Raster
    bool hvOutOfRange = (hvInt > maxV || hvInt < minV); 


    /**********************************************************************
     *  1) Fault‑Latch   (RCM löst aus → State F & CP -12 V)
     **********************************************************************/
    if (!faultLatched && get_rcm_status() != 1) {               // Trip erkannt
        faultLatched = true;
        set_control_pilot_duty_Error(0.0f);                       // Fault‑Pegel
        sniffStage = SNF_IDLE;                                // FSM resetten
    }

    /**********************************************************************
     *  2) Versuch zum Auto‑Reset, wenn RCM wieder OK
     **********************************************************************/
    if (faultLatched && get_rcm_status() == 1)
    {
        switch (sniffStage)
        {
        case SNF_IDLE:
            if (now >= sniffNext) {                           // nur alle 1 s
                set_control_pilot_duty_Error(100.0f);          // CP freigeben
                sniffStart = now;
                sniffStage = SNF_WAIT;                        // 30‑ms‑Fenster
            }
            break;

        case SNF_WAIT:
            if ((now - sniffStart) >= sniffDur) {
                float hv = get_high_voltage();                // erneut messen
                if (hv > hvResetThresh) {                     // +12 V ⇒ A
                    faultLatched = false;    
                    set_control_pilot_duty(setCpDuty);  
                    // CP bleibt in High‑Z → liefert automatisch +12 V
                } else {                                      // Stecker steckt
                    set_control_pilot_duty_Error(0.0f);        // Fault halten
                    sniffNext = now + sniffInt;               // nächster Test
                }
                sniffStage = SNF_IDLE;                        // Zyklus beendet
            }
            break;
        }
    }

    /**********************************************************************
     *  3) Wenn Fault gelatcht, sofort zurückmelden
     **********************************************************************/
    if (faultLatched) return StateF_Fault;

    /**********************************************************************
     *  4) Normale Spannungs‑ und PWM‑Auswertung
     **********************************************************************/
    charging_state_t state = StateCustom_InvalidValue;


    if (abs(hvInt - vB) <= off) state = StateB_Connected;
    else if (abs(hvInt - vC) <= off) state = StateC_Charge;
    else if (abs(hvInt - vD) <= off) state = StateD_VentCharge;
    else if (hvInt > maxV || hvInt < minV) state = StateE_Error;

    /* --- PWM‑Sonderfälle --- */
    int duty = round(get_control_pilot_duty());
    if (duty == 100) state = StateCustom_DutyCycle_100;
    else if (duty == 0) state = StateCustom_DutyCycle_0;

    if (abs(hvInt - vA) <= off) state = StateA_NotConnected;

    /* --- CP‑Relais OFF --- */
    if (get_cp_relays_status() != 1) state = StateCustom_CpRelayOff;

    return state;
}


const char *cpStateToName(charging_state_t state){
    switch(state){
        case StateA_NotConnected:
            return "State A";
        case StateB_Connected:
            return "State B";
        case StateC_Charge:
            return "State C";
        case StateD_VentCharge:
            return "State D";
        case StateE_Error:
            return "State E";
        case StateF_Fault:
            return "State F";
        case StateCustom_CpRelayOff:
            return "Control-Pilot OFF";
        case StateCustom_DutyCycle_100:
            return "State CP 100%";
        case StateCustom_DutyCycle_0:
            return "State CP 0%";
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
    vTaskDelay(pdMS_TO_TICKS(5000));


    while (1) {
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////
//////////////////////////////////////////////////// Loop ///////////////////////////////////////////////////


        highVoltage = get_high_voltage();
        currentCpStateDelay = actualCpState(highVoltage, 0);

        TickType_t now = xTaskGetTickCount();

        /* State‑Debounce 500 ms */
        if (vCurrentCpState == currentCpStateDelay) {
            lastStateChangeTimeState = now;                       // gleich → Timer neu
        } else if (now - lastStateChangeTimeState >= pdMS_TO_TICKS(500)) {
            vCurrentCpState      = currentCpStateDelay;           // Wechsel übernehmen
            lastStateChangeTimeDuty = now;                        // Duty erst 500 ms später
        }

        /* Duty nur alle 500 ms lesen */
        if (now - lastStateChangeTimeDuty >= pdMS_TO_TICKS(500)) {
            getCpDuty = get_control_pilot_duty();
            lastStateChangeTimeDuty = now;
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
