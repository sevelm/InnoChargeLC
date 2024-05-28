

void A_Task_CP(void *pvParameter);

//typedef enum{
//    PROXIMITY_PILOT_STATE_DISCONNECTED,
//    PROXIMITY_PILOT_STATE_CONNECTED,
//    PROXIMITY_PILOT_STATE_INVALID
//} proximity_pilot_state_t;

// CP-State Delay
static uint32_t lastStateChangeTime = 0;
static charging_state_t currentCpStateDelay;

