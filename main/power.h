#include <stdbool.h>
#include "types.h"

void power_init(void);
void f1_power_on(void);
void f1_power_off(void);
void f2_power_on(void);
void f2_power_off(void);
bool cooling_needed(int set_value, int min_value, float air_temp, float keg_temp);

extern enum cooling_state_t f1_state, f2_state;
