

void init_control_pilot(void);
void set_charging_current(float current);
void set_charging_power(float power);
void turn_on_cp_relay(void);
void turn_off_cp_relay(void);
void set_control_pilot_100(void);
void set_control_pilot_0(void);
float get_high_voltage(void);
float get_low_voltage(void);
bool get_cp_relays_status(void);
float get_control_pilot_duty(void);
int16_t get_cp_state_int(void);
void turn_relay_on(void);
void turn_relay_off(void);
float get_current_from_duty(float duty);
float get_power_from_duty(float duty);
float get_duty_from_current(float current);
float get_duty_from_power(float power);
bool get_rcm_status(void);
