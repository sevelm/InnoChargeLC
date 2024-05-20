


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

#define cp_gen_pin 8
#define cp_feedback_pin 37
#define cp_measure_pin 4
#define cp_relay_pin 18
#define pp_measure_pin 5
#define cp_gen_freq 1000
#define cp_gen_duty 50
#define cp_measure_channel ADC1_CHANNEL_3
#define cp_control_channel 0

// Voltage Scaling Part
#define cp_scaling_factor_a 0.005263
#define cp_scaling_factor_b -9.3632

const char *A_CP_LOG = "A-CP-Task: ";

void a_init_control_pilot(void){
    pinMode(cp_gen_pin, OUTPUT);
    ledcSetup(cp_control_channel, cp_gen_freq, 12);
    ledcAttachPin(cp_gen_pin, cp_control_channel);
    pinMode(cp_feedback_pin, INPUT);
    pinMode(cp_relay_pin, OUTPUT);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(cp_measure_channel, ADC_ATTEN_DB_11);
}

void set_control_pilot(float duty){
    int i_duty = 4095 * duty / 100;
    ledcWrite(cp_control_channel, i_duty);
}

cp_measurements_t measure_control_pilot(void) {
    cp_measurements_t measurements;
    uint64_t start_time = micros();

     // Wait for the switch to HIGH
    while (digitalRead(cp_feedback_pin) == LOW) {
        if (micros() - start_time > 2000) {
            break;
        }
    }

    // Short delay for stability
    delayMicroseconds(50);

    // Multiple measurements and averaging for HIGH voltage
    float high_voltage_sum = 0;
    for (int i = 0; i < 5; i++) {
        high_voltage_sum += adc1_get_raw(cp_measure_channel);
        delayMicroseconds(10);
    }
    measurements.high_voltage = high_voltage_sum / 5.0;

    start_time = micros();

    // Wait for the switch to LOW
    while (digitalRead(cp_feedback_pin) == HIGH) {
        if (micros() - start_time > 2000) {
            break;
        }
    }

    // Short delay for stability
    delayMicroseconds(50);

    // Multiple measurements and averaging for LOW voltage
    float low_voltage_sum = 0;
    for (int i = 0; i < 5; i++) {
        low_voltage_sum += adc1_get_raw(cp_measure_channel);
        delayMicroseconds(10);
    }
    measurements.low_voltage = low_voltage_sum / 5.0;

    measurements.high_voltage = measurements.high_voltage * cp_scaling_factor_a + cp_scaling_factor_b;
    measurements.low_voltage = measurements.low_voltage * cp_scaling_factor_a + cp_scaling_factor_b;

    return measurements;
}

void turn_cp_relay_on(void){
    digitalWrite(cp_relay_pin, HIGH);
}

void turn_cp_relay_off(void){
    digitalWrite(cp_relay_pin, LOW);
}

float get_pp_status(void){
    float pp_val = analogRead(pp_measure_pin);
    pp_val = pp_val * 3.3 / 4095;
    return pp_val;
}

void control_pilot_task(void *pvParameter){
    a_init_control_pilot();
    set_control_pilot(8);
    turn_cp_relay_on();

    while (1) {
        measurements = measure_control_pilot();
        float pp_val = get_pp_status();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Adjusted delay
    }
}
