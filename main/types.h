#include <ds18x20.h>

struct temp_data_t {
    size_t num_sensors;
    ds18x20_addr_t addr[MAX_TEMP_SENSORS];
    float temp[MAX_TEMP_SENSORS];
};

#define UNDEFINED_TEMP -999         // temperature to be displayed as --.-
