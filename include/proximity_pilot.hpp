
typedef enum{
    PROXIMITY_PILOT_STATE_DISCONNECTED,
    PROXIMITY_PILOT_STATE_CONNECTED,
    PROXIMITY_PILOT_STATE_INVALID
} proximity_pilot_state_t;

void start_proximity_pilot_monitoring();
void stop_proximity_pilot_monitoring();
proximity_pilot_state_t get_proximity_pilot_state();
const char* pp_state_to_name(proximity_pilot_state_t state);