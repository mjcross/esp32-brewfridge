#ifndef TYPES_H
#define TYPES_H

#include <ds18x20.h>
#include "defines.h"

struct temp_data_t {
    size_t num_sensors;
    ds18x20_addr_t addr[MAX_TEMP_SENSORS];  // underlying type is uint64_t
    float temp[MAX_TEMP_SENSORS];
};

#define UNDEFINED_TEMP -999                 // displayed as " off"

enum power_state_t {
    PWR_OFF,
    PWR_COOL_REQUESTED,
    PWR_COOLING,
    PWR_COOL_OVERRUN,
    PWR_HEAT_REQUESTED,
    PWR_HEATING                             // no heating overrun required
};

struct sensor_field_t {                     // used by `ui_task` and `flash` modules
    const char title[5];
    const int title_x;
    const int title_y;
    const int data_x;
    const int data_y;
    ds18x20_addr_t addr;
    float temp;
};

#endif // TYPES_H
