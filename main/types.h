#ifndef TYPES_H
#define TYPES_H

#include <ds18x20.h>

struct temp_data_t {
    size_t num_sensors;
    ds18x20_addr_t addr[MAX_TEMP_SENSORS];  // underlying type is uint64_t
    float temp[MAX_TEMP_SENSORS];
};

#define UNDEFINED_TEMP -999                 // displayed as ' off'

enum power_state_t {
    PWR_OFF,
    PWR_COOL_REQUESTED,
    PWR_COOLING,
    PWR_COOL_OVERRUN,
    PWR_HEAT_REQUESTED,
    PWR_HEATING                             // no heating overrun required
};

#endif // TYPES_H
