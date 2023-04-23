#include <stdbool.h>
#include "types.h"

void power_init (void);
void f1_power_on (void);
void f1_power_off (void);
void f2_power_on (void);
void f2_power_off (void);
bool cooling_needed (int set_value, int cool_offset_value, float beer_temp, float air_temp);
bool heating_needed (int set_value, int heat_offset_value, float beer_temp, float heater_temp);

extern enum cooling_state_t f1_state, f2_state;
