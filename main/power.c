#include <freertos/FreeRTOS.h>
#include <freertos/task.h>      // for xTaskGetTickCount()
#include <driver/gpio.h>
#include "defines.h"
#include "types.h"

static TickType_t f1_earliest_on, f1_earliest_off, f2_earliest_on, f2_earliest_off;

enum cooling_state_t f1_state, f2_state;     // shared


void power_init() {
    gpio_reset_pin(     F1_RELAY_GPIO                       );
    gpio_set_level(     F1_RELAY_GPIO, 0                    );
    gpio_set_direction( F1_RELAY_GPIO, GPIO_MODE_DEF_OUTPUT );

    gpio_reset_pin(     F2_RELAY_GPIO                       );
    gpio_set_level(     F2_RELAY_GPIO, 0                    );
    gpio_set_direction( F2_RELAY_GPIO, GPIO_MODE_DEF_OUTPUT );

    f1_state = PWR_OFF;
    f2_state = PWR_OFF;
}


void f1_power_on() {
    TickType_t now = xTaskGetTickCount();
    if (f1_state == PWR_OFF_PENDING) {
        f1_state = PWR_ON;  // cancel pending power off
    } else if (f1_state == PWR_OFF || f1_state == PWR_ON_PENDING) {
        //printf("REQUEST ON... now %d, earliest_on %d\n", now, f1_earliest_on);
        if (now >= f1_earliest_on) {
            gpio_set_level(F1_RELAY_GPIO, 1);
            f1_state = PWR_ON;
            f1_earliest_off = now + pdMS_TO_TICKS(RELAY_INTERVAL_MS);
        } else {
            f1_state = PWR_ON_PENDING;
        }
    }
};


void f2_power_on() {
    TickType_t now = xTaskGetTickCount();
    if (f2_state == PWR_OFF_PENDING) {
        f2_state = PWR_ON;  // cancel pending power off
    } else if (f2_state == PWR_OFF || f2_state == PWR_ON_PENDING) {
        if (now >= f2_earliest_on) {
            gpio_set_level(F2_RELAY_GPIO, 1);
            f2_state = PWR_ON;
            f2_earliest_off = now + pdMS_TO_TICKS(RELAY_INTERVAL_MS);
        } else {
            f2_state = PWR_ON_PENDING;
        }
    }
};


void f1_power_off() {
    TickType_t now = xTaskGetTickCount();
    if (f1_state == PWR_ON_PENDING) {
        f1_state = PWR_OFF;  // cancel pending power on
    } else if (f1_state == PWR_ON || f1_state == PWR_OFF_PENDING) {
        //printf("REQUEST OFF... now %d, earliest_off %d\n", now, f1_earliest_off);
        if (now >= f1_earliest_off) {
            gpio_set_level(F1_RELAY_GPIO, 0);
            f1_state = PWR_OFF;
            f1_earliest_on = now + pdMS_TO_TICKS(RELAY_INTERVAL_MS);
        } else {
            f1_state = PWR_OFF_PENDING;
        }
    }
};


void f2_power_off(){
    TickType_t now = xTaskGetTickCount();
    if (f2_state == PWR_ON_PENDING) {
        f2_state = PWR_OFF;  // cancel pending power on
    } else if (f2_state == PWR_ON || f2_state == PWR_OFF_PENDING) {
        if (now >= f2_earliest_off) {
            gpio_set_level(F2_RELAY_GPIO, 0);
            f2_state = PWR_OFF;
            f2_earliest_on = now + pdMS_TO_TICKS(RELAY_INTERVAL_MS);
        } else {
            f2_state = PWR_OFF_PENDING;
        }
    }};


bool cooling_needed(int set_value, int cool_offset_value, float beer_temp, float air_temp) {
    float set_temp = set_value / 10.0;  // the UI stores the settings values as integers
    float min_temp = set_temp - cool_offset_value / 10.0;

    bool set_temp_defined = (set_value != UNDEFINED_TEMP);
    bool min_temp_defined = (cool_offset_value != UNDEFINED_TEMP);
    bool air_sensor_connected = (air_temp != UNDEFINED_TEMP);
    bool beer_sensor_connected = (beer_temp != UNDEFINED_TEMP);

    if (set_temp_defined == false) {
        return false;                   // we can't do anything without a set_temp
    }

    if (min_temp_defined) {
        if (air_sensor_connected == false || beer_sensor_connected == false) {
            return false;               // for min_temp we must have both an air and a beer sensor
        }
        return (beer_temp > set_temp && air_temp > min_temp);
    }

    if (air_sensor_connected) {         // manage the air temperature only
        return (air_temp > set_temp);
    }

    if (beer_sensor_connected) {        // fall back to managing the beer temperature
        return (beer_temp > set_temp);
    }

    return false;                       // we don't have either sensor
}
