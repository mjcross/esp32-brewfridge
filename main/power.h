#include <stdbool.h>
#include "types.h"

void power_init (void);
void power_update(int fridge_num, bool cool, bool heat);
bool cooling_needed (int set_value, int cool_offset_value, float beer_temp, float air_temp);
bool heating_needed (int set_value, int heat_offset_value, float beer_temp, float heater_temp);

extern enum power_state_t power_state[];
