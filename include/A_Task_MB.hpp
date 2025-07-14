void A_Task_MB(void *pvParameter);


extern uint16_t mbRegChargeCurrent;
extern uint16_t mbRegChargePower;

//void set_charging_current_mb(float current, int mbRegCurrent);
//void set_charging_power_mb(float power, int mbRegPower);
void mb_set_charging_current(int mbRegCurrent);
void mb_set_charging_power(int mbRegPower);
void mb_set_charging_curr_pwr(int mbRegCurrent, int mbRegPower);
