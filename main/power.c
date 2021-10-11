#include <freertos/FreeRTOS.h>
#include <freertos/task.h>      // xTaskGetTickCount()
#include <driver/gpio.h>
#include "defines.h"
#include "types.h"

static TickType_t f1_earliest_on, f1_earliest_off, f2_earliest_on, f2_earliest_off;

enum cooling_state_t f1_state, f2_state;     // shared


void power_init() {
    gpio_reset_pin(F1_RELAY_GPIO);
    gpio_reset_pin(F2_RELAY_GPIO);
    gpio_set_level(F1_RELAY_GPIO, 0);
    gpio_set_level(F2_RELAY_GPIO, 0);
    gpio_set_direction(F1_RELAY_GPIO, GPIO_MODE_DEF_OUTPUT);
    gpio_set_direction(F2_RELAY_GPIO, GPIO_MODE_DEF_OUTPUT);
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


bool cooling_needed(int set_value, int min_value, float air_temp, float keg_temp) {
    float set_temp = set_value / 10.0;
    float min_temp = min_value / 10.0;

    bool set_valid = (set_value != UNDEFINED_TEMP);
    bool min_valid = (min_value != UNDEFINED_TEMP);
    bool air_valid = (air_temp != UNDEFINED_TEMP);
    bool keg_valid = (keg_temp != UNDEFINED_TEMP);

    if ((set_valid && !min_valid && keg_valid && keg_temp > set_temp)
        || (set_valid && min_valid && air_valid && keg_valid && keg_temp > set_temp && air_temp > min_temp)
        || (set_valid && air_valid && min_valid && air_temp > set_temp && air_temp > min_temp)
        || (set_valid && !min_valid && air_valid && air_temp > set_temp)
    ){
        return true;
    }

    return false;
}
