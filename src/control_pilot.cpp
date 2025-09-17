#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "control_pilot.hpp"
#include "AA_globals.h"
#include "ledEffect.hpp"
#include "math.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "stdbool.h"
#include "rom/ets_sys.h"
#include "proximity_pilot.hpp"
#include "ethernet_manager.hpp"
#include "A_Task_MB.hpp"

#define cp_gen_pin 8            //AO generate PWM
#define cp_feedback_pin 37      //DI
#define cp_measure_pin 4
#define cp_relay_pin 18
#define cp_gen_freq 1000
#define cp_gen_duty 50
#define cp_measure_channel ADC1_CHANNEL_3
#define cp_control_channel 0
#define RELAY_CH_L1_N      1   // PWM-Kanal 1 für L1/N
#define RELAY_CH_L2_L3     2   // PWM-Kanal 2 für L2/L3
#define RELAY_PIN_L1_N 36
#define RELAY_PIN_L2_L3 38
#define RELAY_DRIVE 41  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! To Delete Q11
//#define RESET_RCD 40
#define rcm_fault 9


#define RISING_EDGE 1
#define FALLING_EDGE 0

#define cp_scaling_factor_a 0.0053
#define cp_scaling_factor_b -9.3632

bool cp_relay_status = false; 
static portMUX_TYPE cp_adc_spinlock = portMUX_INITIALIZER_UNLOCKED;

const char *CP_LOG = "Control Pilot: ";
/**
    * @brief Initialize the control pilot
    * 
    * This function initializes the control pilot by setting up the PWM generator and the ADC channel
    '@param void
    * @return void
*/
void init_control_pilot(void){
    pinMode(cp_gen_pin, OUTPUT);
    ledcSetup(cp_control_channel, cp_gen_freq, 12);
    ledcAttachPin(cp_gen_pin, cp_control_channel);
     pinMode(cp_feedback_pin, INPUT);
    pinMode(cp_relay_pin, OUTPUT);
    digitalWrite(cp_relay_pin, LOW);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(cp_measure_channel, ADC_ATTEN_DB_11);
 
    ledcSetup(RELAY_CH_L1_N, 1000, 12);
    ledcAttachPin(RELAY_PIN_L1_N, RELAY_CH_L1_N);
    ledcSetup(RELAY_CH_L2_L3, 1000, 12);
    ledcAttachPin(RELAY_PIN_L2_L3, RELAY_CH_L2_L3);

 //   pinMode(RELAY_PIN_L1_N, OUTPUT);
 //   digitalWrite(RELAY_PIN_L1_N, LOW);
 //   pinMode(RELAY_PIN_L2_L3, OUTPUT);
 //   digitalWrite(RELAY_PIN_L2_L3, LOW);
 
 
 
    pinMode(RELAY_DRIVE, OUTPUT);   // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! To Delete Q11
    digitalWrite(RELAY_DRIVE, HIGH);// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! To Delete Q11
  //  pinMode(RESET_RCD, OUTPUT);
  //  digitalWrite(RESET_RCD, LOW);
    pinMode(rcm_fault, INPUT_PULLUP);
}



/**
 * @brief Return the current Control-Pilot state as a 16-bit integer.
 *
 * The value corresponds to the numeric representation of the
 * `charging_state_t` enumeration.  The current mapping is
 *
 *   Value | Enum constant                | IEC 61851-1 meaning
 *   ------|------------------------------|-----------------------------
 *     0   | StateA_NotConnected          | EV not connected
 *     1   | StateB_Connected             | Connected, no charging
 *     2   | StateC_Charge                | Charging active
 *     3   | StateD_VentCharge            | Charging, ventilation required
 *     4   | StateE_Error                 | Error / voltage window lost
 *     5   | StateF_Fault                 | Fault (short-circuit etc.)
 *     6   | StateCustom_CpRelayOff       | CP relay off
 *     7   | StateCustom_DutyCycle_100    | PWM = 100 %
 *     8   | StateCustom_DutyCycle_0      | PWM = 0 %
 *     9   | StateCustom_OutOffRange      | CP signal out of range
 *
 * @note  Extend this table if you add new enum values.
 *
 * @return int16_t  Current CP state (see table above).
 */
int16_t get_cp_state_int(void){
return static_cast<int16_t>(vCurrentCpState);
}


/**
    * @brief Set the control pilot duty cycle
    * 
    * This function sets the duty cycle of the control pilot PWM generator
    '@param float duty in percentage
    * @return void
*/
void set_control_pilot_duty(float duty){
    setCpDuty = duty;
    int i_duty = 4095 * duty / 100;
    ledcWrite(cp_control_channel, i_duty);
}

void set_control_pilot_duty_Error(float duty){
    int i_duty = 4095 * duty / 100;
    ledcWrite(cp_control_channel, i_duty);
}

/**
    * @brief Get the control pilot duty cycle
    * 
    * This function gets the duty cycle of the control pilot PWM generator
    '@param float duty in percentage
    * @return void
*/
float get_control_pilot_duty(){
    int i_duty = ledcRead(cp_control_channel);
    float duty = (float)i_duty / 4095 * 100;
    return duty;
}

/** 
    * @brief Get the duty cycle from the current
    * 
    * This function calculates the duty cycle from the current
    * @param float current
    * @return float duty
*/
float get_duty_from_current(float current) {
    if (current < 0) return 0.0;                    // Negative values -> 0A
    if (current < 5 && current > 0) current = 5;    // Below 5A -> use 5A
    if (current > 22) current = 22;                 // Above 22A -> use 22A
    // Calculate duty cycle based on current
    return (current <= 51) ? current / 0.6 : current / 2.5 + 64;
}

/**  ########################################## STATUS vom 230V schütz abfragen!!!!!!!!!!!!!!!!!!!!!!!!
 * @brief Get the power in 1/10 kW from the duty cycle
 * 
 * This function calculates the power (in 1/10 kW) based on the duty cycle.
 * The formula depends on the duty cycle range.
 * 
 * @param float duty_cycle Duty cycle as a percentage
 * @return float power_in_tenth_kw Power in 1/10 kW (e.g., 42 = 4.2 kW)
 */
float get_power_from_duty(float duty_cycle) { 
//ESP_LOGI(CP_LOG, "Duty Cycle: %f", duty_cycle);
    if (duty_cycle <= 0) return 0.0;                        // Duty cycle <= 0% -> 0 power
    float power_kw = 0.0;
    if (duty_cycle < -1) {                                 //########################################## STATUS vom 230V schütz abfragen!!!!!!!!!!!!!!!!!!!!!!!!
        power_kw = (duty_cycle * 0.6) * 230 / 1000;  
  //  ESP_LOGI(CP_LOG, "230 ");       // Reverse of (W / 230) / 0.6
    } else {                                                // For duty cycle > 26.7%
        power_kw = (duty_cycle * 0.6) * 692 / 1000;         // Reverse of (W / 692) / 0.6
  //  ESP_LOGI(CP_LOG, "400 ");       // Reverse of (W / 230) / 0.6
    }
  //  ESP_LOGI(CP_LOG, "Aktuel Power %f", power_kw * 10);       // Reverse of (W / 230) / 0.6
    return power_kw * 10;                                   // Convert kW to 1/10 kW

}


/**
 * @brief Get the duty cycle from the power input
 * 
 * This function calculates the duty cycle based on the power input.
 * The formula depends on the input range.
 * 
 * @param float power_in_tenth_kw Input power in 1/10 kW (e.g., 4.2 kW = 42)
 * @return float duty_cycle Duty cycle as a percentage
 */
float get_duty_from_power(float power_in_tenth_kw) {
    if (power_in_tenth_kw <= 0) return 0.0;             // Negative or zero power -> 0% duty cycle
    float power_kw = power_in_tenth_kw / 10.0;          // Convert 1/10 kW to kW
    if (power_kw <= 4.1) {                              // For power <= 4.1 kW   
        return (power_kw * 1000 / 230) / 0.6;           // Use (W / 230) / 0.6
    } else {                                            // For power > 4.2 kW
        return (power_kw * 1000 / 692) / 0.6;           // Use (W / 692) / 0.6
    }
}

/**
    * @brief Set the charging current
    * 
    * This function sets the charging current by setting the duty cycle of the control pilot PWM generator
    * @param float current
    * @return void
*/
void set_charging_current(float current){
    if (current == 0) {
        set_control_pilot_100();  // 12V DC ohne PWM → Status B (WAIT)
    } else {
        float duty = get_duty_from_current(current);
        set_control_pilot_duty(duty);
    }
}

/**
    * @brief Set the charging power
    * 
    * This function sets the charging power by setting the duty cycle of the control pilot PWM generator
    * @param float power
    * @return void
*/
void set_charging_power(float power){
    if (power == 0) {
        set_control_pilot_100();  // 12V DC ohne PWM → Status B (WAIT)
    } else {
        float duty = get_duty_from_power(power);
        set_control_pilot_duty(duty);
    }
}

/** 
    * @brief Calculate current from duty cycle
    * 
    * This function converts the duty cycle into the corresponding current.
    * @param float duty
    * @return float current
*/
float get_current_from_duty(float duty) {             
    return (duty <= 85) ? duty * 0.6 : (duty - 64) * 2.5;
}

/**
    * @brief Turn on the control pilot relay
    * @param void
    * @return void

*/
void turn_on_cp_relay(){
    digitalWrite(cp_relay_pin, HIGH);
    cp_relay_status = true;
}

/**
    * @brief Turn off the control pilot relay
    * @param void
    * @return void

*/
void turn_off_cp_relay(){
    digitalWrite(cp_relay_pin, LOW);
    cp_relay_status = false;
}

/**
    * @brief Set the control pilot to standby (100%)
    * @param void
    * @return void

*/
void set_control_pilot_100(){
    set_control_pilot_duty(100);
}

/**
    * @brief Set the control pilot off (0%)
    * @param void
    * @return void

*/
void set_control_pilot_0(){
    set_control_pilot_duty(0);
}

/**
    * @brief Get the status of the control pilot relay
    * @param void
    * @return bool cp_relay_status

*/
bool get_cp_relays_status(){
    bool cp_relay_status = digitalRead(cp_relay_pin);
    return cp_relay_status;
}

/**
    * @brief Get the status of the RCM
    * @param void
    * @return bool rcm_status if TRUE then FAULT

*/
bool get_rcm_status(){
    bool rcmFault = digitalRead(rcm_fault);
    return rcmFault;
}

/**
    * @brief Synchronize the control pilot edge
    * 
    * This function synchronizes the control pilot edge
    * @param bool edge
    * @return void
*/
void sync_cp_edge(bool edge){
    uint64_t start_time = micros();
    while(digitalRead(cp_feedback_pin) == edge){
        if( micros() - start_time > 2000){
         //   ESP_LOGI(CP_LOG, "CP Pulse Not Detected! Aborting");
            return;
        }
    }
    while(digitalRead(cp_feedback_pin) != edge){
        if( micros() - start_time > 2000){
         //   ESP_LOGI(CP_LOG, "CP Pulse Not Detected! Aborting");
            return;
        }
    }
    return;
}

/**
    * @brief Get the high voltage
    * 
    * This function gets the high voltage
    * @param void
    * @return float high_voltage
*/
float get_high_voltage(){
    float high_voltage=0;
    // taskENTER_CRITICAL(&cp_adc_spinlock);
    sync_cp_edge(RISING_EDGE);
    ets_delay_us(20);
    high_voltage= (adc1_get_raw(cp_measure_channel)+adc1_get_raw(cp_measure_channel))/2;
    // taskEXIT_CRITICAL(&cp_adc_spinlock);
    // ESP_LOGI(CP_LOG, "RAW ADC Value: %f", high_voltage);
    high_voltage= high_voltage*cp_scaling_factor_a+cp_scaling_factor_b;
    return high_voltage;
}

/**
    * @brief Get the low voltage
    * 
    * This function gets the low voltage
    * @param void
    * @return float low_voltage
*/
float get_low_voltage(){
    float low_voltage=0;
    // taskENTER_CRITICAL(&cp_adc_spinlock);
    sync_cp_edge(FALLING_EDGE);
    ets_delay_us(20);
    low_voltage= (adc1_get_raw(cp_measure_channel)+adc1_get_raw(cp_measure_channel)+adc1_get_raw(cp_measure_channel))/3;
    // taskEXIT_CRITICAL(&cp_adc_spinlock);
    low_voltage= low_voltage*cp_scaling_factor_a+cp_scaling_factor_b;
    // ESP_LOGI(CP_LOG, "Low Voltage: %f", low_voltage);
    if(low_voltage<0){
        low_voltage = low_voltage*1.5;
    }
    return low_voltage;
}

// Control AC-Relay

void turn_relay_on() {
    digitalWrite(RELAY_PIN_L1_N, HIGH);
    digitalWrite(RELAY_PIN_L2_L3, HIGH);
}

void turn_relay_off() {
    digitalWrite(RELAY_PIN_L1_N, LOW);
    digitalWrite(RELAY_PIN_L2_L3, LOW);
}

void turn_relay_pwm_L1N(float duty) {
    int i_duty = 4095 * duty / 100.0f;
    ledcWrite(RELAY_CH_L1_N, i_duty);
}

void turn_relay_pwm_L2L3(float duty) {
    int i_duty = 4095 * duty / 100.0f;
    ledcWrite(RELAY_CH_L2_L3, i_duty);
}