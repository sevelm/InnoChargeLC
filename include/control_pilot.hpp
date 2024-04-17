#pragma once
struct cp_measurements{
    float high_voltage;
    float low_voltage;
};
typedef struct cp_measurements cp_measurements_t;
void control_pilot_task(void *pvParameter);