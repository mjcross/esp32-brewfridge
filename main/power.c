#include <freertos/FreeRTOS.h>
#include <freertos/task.h>      // for xTaskGetTickCount()
#include <driver/gpio.h>
#include "defines.h"
#include "types.h"

enum power_state_t power_state[2];     // shared

static const gpio_num_t gpio_fridge_relay[] = { F1_RELAY_GPIO, F2_RELAY_GPIO };
static const gpio_num_t gpio_heater_ssr[] = { F1_SSR_GPIO, F2_SSR_GPIO };


/// @brief Initialises the power states and GPIO pins for both fridges
void power_init() {
    for (int fridge_num = 0; fridge_num < 2; fridge_num += 1) {
        gpio_reset_pin(gpio_fridge_relay[fridge_num]);
        gpio_set_level(gpio_fridge_relay[fridge_num], 0);
        gpio_set_direction(gpio_fridge_relay[fridge_num], GPIO_MODE_DEF_OUTPUT);

        gpio_reset_pin(gpio_heater_ssr[fridge_num]);
        gpio_set_level(gpio_heater_ssr[fridge_num], 0);
        gpio_set_direction(gpio_heater_ssr[fridge_num], GPIO_MODE_DEF_OUTPUT);

        power_state[fridge_num] = PWR_OFF;
    }
}


/// @brief Updates the power state for a fridge and controls its relay/SSR GPIOs.
///
/// This function should be called frequently for each fridge, e.g. on every 
/// iteration of the UI event loop.
///
/// @param fridge_num the index of the fridge (0 or 1)
/// @param cool true to request cooling, otherwise false
/// @param heat true to request heating, otherwise false
void power_update(int fridge_num, bool cool, bool heat) {
    static TickType_t earliest_cooling_start[2];
    static TickType_t earliest_cooling_stop[2];
    static TickType_t latest_cooling_stop[2];
    static TickType_t earliest_heating_start[2];

    TickType_t now = xTaskGetTickCount();

    switch (power_state[fridge_num]) {
        case PWR_OFF:
            if (cool == false || heat == false) {
                if (cool) {
                    power_state[fridge_num] = PWR_COOL_REQUESTED;
                }
                if (heat) {
                    power_state[fridge_num] = PWR_HEAT_REQUESTED;
                }
            }
            break;

        case PWR_COOL_REQUESTED:
            if (cool == false) {
                // cancel request
                power_state[fridge_num] = PWR_OFF;
            } else if (now >= earliest_cooling_start[fridge_num]) {
                // start cooling
                earliest_cooling_stop[fridge_num] = now + MIN_COOLING_TIME;
                latest_cooling_stop[fridge_num] = now + MAX_COOLING_TIME;
                gpio_set_level (gpio_fridge_relay[fridge_num], 1);
                power_state[fridge_num] = PWR_COOLING;
            }
            break;

        case PWR_COOLING:
            if (cool == false) {
                power_state[fridge_num] = PWR_COOL_OVERRUN;
            } else if (now >= latest_cooling_stop[fridge_num]) {
                // reached MAX_COOLING_TIME
                earliest_cooling_start[fridge_num] = now + MIN_OFF_TIME;
                earliest_heating_start[fridge_num] = now + MIN_OFF_TIME;
                gpio_set_level (gpio_fridge_relay[fridge_num], 0);
                power_state[fridge_num] = PWR_OFF;
            }
            break;

        case PWR_COOL_OVERRUN:
            if (cool == true) {
                power_state[fridge_num] = PWR_COOLING;
            } else if (now >= earliest_cooling_stop[fridge_num]) {
                // stop cooling
                earliest_cooling_start[fridge_num] = now + MIN_OFF_TIME;
                earliest_heating_start[fridge_num] = now + MIN_OFF_TIME;
                gpio_set_level (gpio_fridge_relay[fridge_num], 0);
                power_state[fridge_num] = PWR_OFF;
            }
            break;

            case PWR_HEAT_REQUESTED:
            if (heat == false) {
                // cancel request
                power_state[fridge_num] = PWR_OFF;
            } else if (now >= earliest_heating_start[fridge_num]) {
                // start heating
                gpio_set_level (gpio_heater_ssr[fridge_num], 1);
                power_state[fridge_num] = PWR_HEATING;
            }
            break;

            case PWR_HEATING:
            if (heat == false) {
                // stop heating
                earliest_cooling_start[fridge_num] = now + MIN_OFF_TIME;
                gpio_set_level (gpio_heater_ssr[fridge_num], 0);
                power_state[fridge_num] = PWR_OFF;
            }
            break;
    }
}


bool cooling_needed (int set_value, int cool_offset_value, float beer_temp, float air_temp) {
    float set_temp = set_value / 10.0;  // the UI stores the settings values as integers
    float min_temp = beer_temp - cool_offset_value / 10.0;

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

    return false;
}


bool heating_needed (int set_value, int heat_offset_value, float beer_temp, float heater_temp) {
    float set_temp = set_value / 10.0;  // the UI stores the settings values as integers
    float max_temp = beer_temp + heat_offset_value / 10.0;

    bool set_temp_defined = (set_value != UNDEFINED_TEMP);
    bool max_temp_defined = (heat_offset_value != UNDEFINED_TEMP);
    bool heater_sensor_connected = (heater_temp != UNDEFINED_TEMP);
    bool beer_sensor_connected = (beer_temp != UNDEFINED_TEMP);

    if (set_temp_defined == false) {
        return false;                   // we can't do anything without a set_temp
    }

    if (max_temp_defined) {
        if (heater_sensor_connected == false || beer_sensor_connected == false) {
            return false;               // for max_temp we must have both a heater and a beer sensor
        }
        return (beer_temp < set_temp && heater_temp < max_temp);
    }

    if (heater_sensor_connected) {      // manage the heater temperature only
        return (heater_temp < set_temp);
    }

    if (beer_sensor_connected) {        // fall back to managing the beer temperature
        return (beer_temp < set_temp);
    }

    return false;
}
