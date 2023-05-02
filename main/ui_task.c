#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>                 // memset()
#include "esp_log.h"

#include <ds18x20.h>                // oneWire temperature sensor
#include <encoder.h>                // rotary encoder

#include "defines.h"
#include "globals.h"
#include "types.h"
#include "ui_task.h"
#include "lcd.h"
#include "sensor_task.h"
#include "power.h"
#include "flash.h"


#define COL_1   0                   // dislay column positions
#define COL_2   5
#define COL_3   11
#define COL_4   16


// type definitions
// ----------------
enum ui_mode_t {
    UI_MODE_SPLASH = 0,
    UI_MODE_SLEEP,
    UI_MODE_STATUS,
    UI_MODE_SET_1,
    UI_MODE_SET_2,
    UI_MODE_SET_3,
    UI_MODE_SET_4,
    UI_MODE_SET_5,
    UI_MODE_SET_6,
    UI_MODE_SENSOR_1,
    UI_MODE_SENSOR_2,
    UI_MODE_SENSOR_3,
    UI_MODE_SENSOR_4,
    UI_MODE_SENSOR_5,
    UI_MODE_SENSOR_6
};

enum ui_event_t {
    UI_EVENT_BTN_PRESS,
    UI_EVENT_BTN_LONG_PRESS,
    UI_EVENT_VALUE_CHANGE,
    UI_EVENT_BLINK,
    UI_EVENT_TIMEOUT,
    UI_EVENT_SLEEP,
    UI_EVENT_NEW_TEMP_DATA
};

struct set_field_t {
    const char title[6];
    const int title_x;
    const int title_y;
    const int data_x;
    const int data_y;
    int value;
};


// module shared vars
// ------------------

static const enum ui_mode_t next_state_btn_press[] = {
    UI_MODE_STATUS,                 // splash -> status
    UI_MODE_STATUS,                 // sleep -> status
    UI_MODE_SET_1,                  // status -> set_1
    UI_MODE_SET_2,                  // set_1 -> set_2
    UI_MODE_SET_3,                  // set_2 -> set_3
    UI_MODE_SET_4,                  // set_3 -> set_4
    UI_MODE_SET_5,                  // set_4 -> set_5
    UI_MODE_SET_6,                  // set_5 -> set_6
    UI_MODE_SET_1,                  // set_6 -> set_1
    UI_MODE_SENSOR_2,               // sensor_1 -> sensor_2
    UI_MODE_SENSOR_3,               // sensor_2 -> sensor_3
    UI_MODE_SENSOR_4,               // sensor_3 -> sensor_4
    UI_MODE_SENSOR_5,               // sensor_4 -> sensor_5
    UI_MODE_SENSOR_6,               // sensor_1 -> sensor_6
    UI_MODE_SENSOR_1                // sensor_6 -> sensor_1
};

static const enum ui_mode_t next_state_long_press[] = {
    UI_MODE_STATUS,                 // splash -> status
    UI_MODE_STATUS,                 // sleep -> status
    UI_MODE_SENSOR_1,               // status -> sensor_1
    UI_MODE_STATUS,                 // set_1 -> status
    UI_MODE_STATUS,                 // set_2 -> status
    UI_MODE_STATUS,                 // set_3 -> status
    UI_MODE_STATUS,                 // set_4 -> status
    UI_MODE_STATUS,                 // set_5 -> status
    UI_MODE_STATUS,                 // set_6 -> status
    UI_MODE_STATUS,                 // sensor_1 -> status       // todo: save sensor addresses
    UI_MODE_STATUS,                 // sensor_2 -> status
    UI_MODE_STATUS,                 // sensor_3 -> status
    UI_MODE_STATUS,                 // sensor_4 -> status
    UI_MODE_STATUS,                 // sensor_5 -> status
    UI_MODE_STATUS                  // sensor_6 -> status
};

static const enum ui_mode_t next_state_timeout[] = {
    UI_MODE_STATUS,                 // splash -> status
    UI_MODE_SLEEP,                  // sleep -> sleep
    UI_MODE_STATUS,                 // set_1 -> status
    UI_MODE_STATUS,                 // set_2 -> status
    UI_MODE_STATUS,                 // set_3 -> status
    UI_MODE_STATUS,                 // set_4 -> status
    UI_MODE_STATUS,                 // set_5 -> status
    UI_MODE_STATUS,                 // set_6 -> status
    UI_MODE_STATUS,                 // sensor_1 -> status       // todo: save sensor addresses
    UI_MODE_STATUS,                 // sensor_2 -> status
    UI_MODE_STATUS,                 // sensor_3 -> status
    UI_MODE_STATUS,                 // sensor_4 -> status
    UI_MODE_STATUS,                 // sensor_5 -> status
    UI_MODE_STATUS                  // sensor_6 -> status
};

// screen positions of the temperature sensor fields
// 
//      01234567890123456789
//      |    |     |    |
// 0    FRIDGE *1  FRIDGE +2
// 1    beer 12.3  beer 12.3
// 2    air  10.0  air  10.0
// 3    heat 12.3  heat 12.3
#define F1_SENSOR_BEER  0
#define F1_SENSOR_AIR   1
#define F1_SENSOR_HEAT  2
#define F2_SENSOR_BEER  3
#define F2_SENSOR_AIR   4
#define F2_SENSOR_HEAT  5
static struct sensor_field_t sensor_field[] = {
    //  title[5],   title_x,    title_y,    data_x, data_y, addr,   value
    {   "beer",     COL_1,      1,          COL_2,  1,      0ull,   UNDEFINED_TEMP },   // F1_SENSOR_BEER
    {   "air",      COL_1,      2,          COL_2,  2,      0ull,   UNDEFINED_TEMP },   // F1_SENSOR_AIR
    {   "heat",     COL_1,      3,          COL_2,  3,      0ull,   UNDEFINED_TEMP },   // F1_SENSOR_HEAT
    {   "beer",     COL_3,      1,          COL_4,  1,      0ull,   UNDEFINED_TEMP },   // F2_SENSOR_BEER
    {   "air",      COL_3,      2,          COL_4,  2,      0ull,   UNDEFINED_TEMP },   // F2_SENSOR_AIR
    {   "heat",     COL_3,      3,          COL_4,  3,      0ull,   UNDEFINED_TEMP }    // F2_SENSOR_HEAT
};

// screen positions of the settings fields
// 
//      01234567890123456789
//      |    |     |    |
// 0    FRIDGE +1  FRIDGE *2
// 1    set	 12.3  set  12.3
// 2    cool- 4.5  cool- off
// 3    heat+ off  heat+ 2.0
#define F1_SET  0
#define F2_SET  1
#define F1_COOL 2
#define F2_COOL 3
#define F1_HEAT 4
#define F2_HEAT 5
static struct set_field_t set_field[] = {
    //  title[6],   title_x,    title_y,    data_x, data_y, value
    {   "set",      COL_1,      1,          COL_2,  1,      UNDEFINED_TEMP },   // F1_SET
    {   "set",      COL_3,      1,          COL_4,  1,      UNDEFINED_TEMP },   // F2_SET
    {   "cool-",    COL_1,      2,          COL_2,  2,      UNDEFINED_TEMP },   // F1_COOL
    {   "cool-",    COL_3,      2,          COL_4,  2,      UNDEFINED_TEMP },   // F2_COOL
    {   "heat+",    COL_1,      3,          COL_2,  3,      UNDEFINED_TEMP },   // F1_HEAT
    {   "heat+",    COL_3,      3,          COL_4,  3,      UNDEFINED_TEMP }    // F2_HEAT
};

static const char power_state_indicator[] = {
    ' ',    // pwr_off
    '-',    // pwr_cool_requested
    '*',    // pwr_cooling
    '.',    // pwr_cool_overrun
    '+',    // pwr_heat_requested
    '^'     // pwr_heating
};

static const int num_sensor_fields = sizeof(sensor_field) / sizeof(struct sensor_field_t);
static const int num_set_fields = sizeof(set_field) / sizeof(struct set_field_t);
static QueueHandle_t encoder_event_queue;
static rotary_encoder_t re;
static char buf[10];
static enum ui_mode_t mode;
static ds18x20_addr_t addr;
static int timeout_count;
static struct temp_data_t temp_data;
static int blink_x;
static int blink_y;
static bool blink_enabled;
static bool sensor_addresses_changed = false;


// function definitions
// --------------------


/// @brief Converts the internal integer representation of a temperature into a string. 
/// @param buf Where to store the string.
/// @param buflen Space available to store the string and terminating '\0'.
/// @param temp The internal integer representation of a temperature.
static void value_to_temp_str(char *buf, size_t buflen, int temp) {
    if (temp == UNDEFINED_TEMP) {
        snprintf(buf, buflen, " off");
    } else {
        snprintf(buf, buflen, "%4.1f", temp / 10.0);
    }
}


/// @brief Displays the current temperatures of all the sensors.
/// @param void
static void status_display_sensor_temps(void) {
    for (int i = 0; i < num_sensor_fields; i += 1) {
        lcd_gotoxy(sensor_field[i].data_x, sensor_field[i].data_y);
        if (sensor_field[i].temp == UNDEFINED_TEMP) {
            lcd_puts(" off");
        } else {
            snprintf(buf, sizeof(buf), "%4.1f", sensor_field[i].temp);
            lcd_puts(buf);
        }
    }
}


/// @brief Initialises the driver for the control knob (rotary encoder).
/// @param  void 
static void encoder_init(void) {
    encoder_event_queue = xQueueCreate(RE_EVENT_QUEUE_SIZE, sizeof(rotary_encoder_event_t));
    ESP_ERROR_CHECK(rotary_encoder_init(encoder_event_queue));

    // Add one encoder
    memset(&re, 0, sizeof(rotary_encoder_t));
    re.pin_a = RE_A_GPIO;
    re.pin_b = RE_B_GPIO;
    re.pin_btn = RE_BTN_GPIO;
    ESP_ERROR_CHECK(rotary_encoder_add(&re));
}


/// @brief Finds the index of a sensor from its address.
///
/// Used to highlight the current choice while the user is choosing a sensor.
///
/// @param addr the sensor address (64 bit romcode)
/// @return the index of the sensor in the array or zero if it wasn't found
static int find_sensor(ds18x20_addr_t addr) {
    int i;
    for (i = 0; i < temp_data.num_sensors; i += 1) {
        if (temp_data.addr[i] == addr) {
            break;
        }
    }
    if (temp_data.addr[i] == addr) {
        return i;
    } else {
        return 0;
    }
}


/// @brief Displays a list of connected sensors.
static void show_sensors(void) {
    bool addr_found = false;

    // show abbreviated romcodes of attached sensors
    for (int i = 0; i < temp_data.num_sensors; i += 1) {
        lcd_gotoxy((i % 4) * 5, (i / 4) + 1);
        if (i == 0) {
            lcd_puts(" off");
        } else {
            // show CRC and most significant address byte
            snprintf(buf, sizeof(buf), "%04x", (uint16_t)(temp_data.addr[i] >> (64 - 16)));
            lcd_puts(buf);
        }
        if (temp_data.addr[i] == addr) {
            addr_found = true;
            blink_x = (i % 4) * 5;
            blink_y = (i / 4) + 1;
        }
    }

    // erase any unused slots
    for (int i = temp_data.num_sensors; i < MAX_TEMP_SENSORS; i += 1) {
        lcd_gotoxy((i % 4) * 5, (i / 4) + 1);
        lcd_puts("    ");
    }

    // reset current address if sensor no longer available
    if (!addr_found) {
        addr = 0;
        blink_x = 0;
        blink_y = 1;
    }
}


/// @brief Sets up the display for the user to start changing a setting.
/// @param i the index of the new setting, eg. F1_SET
void new_mode_set(int i) {
    i -= 1;
    lcd_restore();
    blink_x = set_field[i].data_x;
    blink_y = set_field[i].data_y;
    blink_enabled = true;
    lcd_hide(blink_x, blink_y, 4);
    timeout_count = 0;
}


/// @brief Sets up the display for the user to start choosing the sensor for a field.
/// @param i the index of the sensor field, eg. F1_SENSOR_BEER
void new_mode_sensor(int i) {
    i -= 1;
    lcd_gotoxy(0, 0);

    lcd_puts(sensor_field[i].title);
    if (i < 3) {
        lcd_puts(" FRIDGE 1 ");
    } else {
        lcd_puts(" FRIDGE 2 ");
    }

    addr = sensor_field[i].addr;
    show_sensors();
    blink_enabled = true;
    lcd_hide(blink_x, blink_y, 4);
    timeout_count = 0;
}


/// @brief Updates the display and UI state for a new UI mode.
///
/// The new mode (eg. UI_MODE_SLEEP) is held in the global 'mode'.
///
static void new_mode(void) {
    blink_enabled = false;
    switch (mode) {
        case UI_MODE_SPLASH:
            lcd_clear();
            blink_enabled = false;
            lcd_gotoxy(5, 1);
            lcd_puts("TETB dual");
            lcd_gotoxy(2, 2);
            lcd_puts("fridge controller");
            break;

        case UI_MODE_SLEEP:
            lcd_switch_backlight(false);
            blink_enabled = false;
            break;

        case UI_MODE_STATUS:
            lcd_clear();
            lcd_switch_backlight(true);
            blink_enabled = false;
            //        01234567890123456789
            lcd_puts("FRIDGE  1  FRIDGE  2");
            for (int i = 0; i < num_sensor_fields; i++) {
                lcd_gotoxy(sensor_field[i].title_x, sensor_field[i].title_y);
                lcd_puts(sensor_field[i].title);
            }
            status_display_sensor_temps();
            addr = 0;
            if (sensor_addresses_changed) {     // write new sensor addresses to non-volatile storage
                write_sensor_addresses (sensor_field, num_sensor_fields);
                sensor_addresses_changed = false;
            }
            break;

        case UI_MODE_SET_1:
            lcd_clear();
            lcd_puts("FRIDGE  1  FRIDGE  2");
            for (int i = 0; i < num_set_fields; i += 1) {
                lcd_gotoxy(set_field[i].title_x, set_field[i].title_y);
                lcd_puts(set_field[i].title);
                lcd_gotoxy(set_field[i].data_x, set_field[i].data_y);
                value_to_temp_str(buf, sizeof(buf), set_field[i].value);
                lcd_puts(buf);
            }
            new_mode_set(1);        // highlight the first setting field
            break;

        case UI_MODE_SET_2:
            new_mode_set(2);
            break;

        case UI_MODE_SET_3:
            new_mode_set(3);
            break;

        case UI_MODE_SET_4:
            new_mode_set(4);
            break;

        case UI_MODE_SET_5:
            new_mode_set(5);
            break;

        case UI_MODE_SET_6:
            new_mode_set(6);
            break;

        case UI_MODE_SENSOR_1:
            lcd_clear();
            new_mode_sensor(1);     // display the title for the first field, show the list of
            break;                  // connected sensors and highlight the current choice

        case UI_MODE_SENSOR_2:
            new_mode_sensor(2);
            break;

        case UI_MODE_SENSOR_3:
            new_mode_sensor(3);
            break;

        case UI_MODE_SENSOR_4:
            new_mode_sensor(4);
            break;

        case UI_MODE_SENSOR_5:
            new_mode_sensor(5);
            break;

        case UI_MODE_SENSOR_6:
            new_mode_sensor(6);
            break;

        default:
            ESP_LOGE(TAG, "unrecognised mode");
            break;
    }
}


/// @brief Updates the temperature fields from a set of sensor readings.
///
/// Any fields without a reading are set to UNDEFINED_TEMP 
///
/// @param pTemp an array of sensor readings
static void update_sensor_temps(struct temp_data_t *pTemp) {
    for (int f = 0; f < num_sensor_fields; f += 1) {
        sensor_field[f].temp = UNDEFINED_TEMP;
        if (sensor_field[f].addr != 0) {
            for (int s = 0; s < pTemp->num_sensors; s += 1) {
                if (pTemp->addr[s] == sensor_field[f].addr) {
                    sensor_field[f].temp = pTemp->temp[s];
                }
            }
        }
    }
}


/// @brief Changes a setting by a certain amount.
///
/// This is called by ui_event_handler() in response to a RE_ET_CHANGED
/// event when the UI is in a 'set' mode, eg. UI_MODE_SET_1.
/// 
/// @param i the index of the setting to be changed, eg. F1_SET
/// @param diff the amount to change from the current value.
static void set_field_value_change(int i, int diff) {
    if (set_field[i].value == UNDEFINED_TEMP) {
        set_field[i].value = 0;
    }
    set_field[i].value += diff;
    if (set_field[i].value < 0) {
        set_field[i].value = UNDEFINED_TEMP;
    }
    lcd_gotoxy(set_field[i].data_x, set_field[i].data_y);
    value_to_temp_str(buf, sizeof(buf), set_field[i].value);
    lcd_puts(buf);
    timeout_count = 2;      // reset the inactivity timer
}


/// @brief Changes the sensor associated with a field, and flags the flash as needing an update.
///
/// This is called by ui_event_handler() in response to a RE_ET_CHANGED
/// event when the UI is in a 'sensor' mode, eg. UI_MODE_SENSOR_1.
///
/// @param i the index of the field being chosen, eg. F1_SENSOR_BEER
/// @param diff the number of positions to move forward or backward in the sensor list.
static void sensor_addr_change(int i, int diff) {
    i -= 1;
    int sensor_index = find_sensor(addr);
    sensor_index = (sensor_index + diff) % (int)temp_data.num_sensors;
    if (sensor_index < 0) {
        sensor_index += temp_data.num_sensors;  // user moved backwards - wrap around
    }
    addr = temp_data.addr[sensor_index];
    sensor_field[i].addr = addr;
    blink_x = (sensor_index % 4) * 5;
    blink_y = (sensor_index / 4) + 1;
    lcd_hide(blink_x, blink_y, 4);
    timeout_count = 0;
    sensor_addresses_changed = true;  // update the non-volatile storage when we return to MODE_STATUS
}


/// @brief Handles each different type of UI event.
/// @param event the event, eg. UI_EVENT_NEW_TEMP_DATA
/// @param value_change the parameter associated with the event (if any).
static void ui_event_handler(enum ui_event_t event, int value_change) {
    switch(event) {
        case UI_EVENT_BTN_PRESS:
            lcd_restore();                                  // unhide any previously-hidden field
            mode = next_state_btn_press[mode];
            new_mode();
            break;

        case UI_EVENT_BTN_LONG_PRESS:
            mode = next_state_long_press[mode];
            new_mode();
            break;

        case UI_EVENT_TIMEOUT:
            // don't timeout in sensor selection modes
            if (mode != next_state_timeout[mode] && mode < UI_MODE_SENSOR_1) {
                mode = next_state_timeout[mode];
                new_mode();
            }
            break;

        case UI_EVENT_VALUE_CHANGE:
            lcd_restore();
            switch (mode) {
                case UI_MODE_SET_1:
                    set_field_value_change(0, value_change);
                    break;

                case UI_MODE_SET_2:
                    set_field_value_change(1, value_change);
                    break;

                case UI_MODE_SET_3:
                    set_field_value_change(2, value_change);
                    break;

                case UI_MODE_SET_4:
                    set_field_value_change(3, value_change);
                    break;

                case UI_MODE_SET_5:
                    set_field_value_change(4, value_change);
                    break;

                case UI_MODE_SET_6:
                    set_field_value_change(5, value_change);
                    break;

                case UI_MODE_SENSOR_1:
                    sensor_addr_change(1, value_change);
                    break;

                case UI_MODE_SENSOR_2:
                    sensor_addr_change(2, value_change);
                    break;

                case UI_MODE_SENSOR_3:
                    sensor_addr_change(3, value_change);
                    break;

                case UI_MODE_SENSOR_4:
                    sensor_addr_change(4, value_change);
                    break;

                case UI_MODE_SENSOR_5:
                    sensor_addr_change(5, value_change);
                    break;

                case UI_MODE_SENSOR_6:
                    sensor_addr_change(6, value_change);
                    break;

                default:
                    break;
            }
            break;

        case UI_EVENT_BLINK:
            if (mode == UI_MODE_STATUS) {
                status_display_sensor_temps();
            } else if (blink_enabled) {
                if (timeout_count == 0) {
                    if (mode >= UI_MODE_SENSOR_1) {
                        // update list in sensor selection modes
                        show_sensors();
                    }
                    lcd_hide(blink_x, blink_y, 4);
                } else if (timeout_count == 1) {
                    lcd_restore();
                }
            }
            break;

        case UI_EVENT_SLEEP:
            mode = UI_MODE_SLEEP;
            lcd_switch_backlight(false);
            break;

        case UI_EVENT_NEW_TEMP_DATA:
            update_sensor_temps(&(temp_data));
            break;

        default:
            ESP_LOGE(TAG, "unrecognised event");
            break;
    }
}


/// @brief Prepares the UI and continually runs the event loop.
/// @param pParams the parameters passed by xTaskCreate(): not used.
void ui_task(void *pParams) {
    rotary_encoder_event_t e;
    static int long_timeout_count;
    static int sleep_timeout_count;

    // prepare the UI
    //
    lcd_init();
    encoder_init();
    read_sensor_addresses(sensor_field, num_sensor_fields);  // load 1-Wire addresses from flash
    new_mode();     // set up the first screen

    // repeat the event loop forever
    //
    for(;;) {
        // update the power state of the fridges
        //
        power_update (
            0,      // fridge 1 
            cooling_needed(
                set_field[F1_SET].value,
                set_field[F1_COOL].value,
                sensor_field[F1_SENSOR_BEER].temp,
                sensor_field[F1_SENSOR_AIR].temp),
            heating_needed(
                set_field[F1_SET].value,
                set_field[F1_HEAT].value,
                sensor_field[F1_SENSOR_BEER].temp,
                sensor_field[F1_SENSOR_HEAT].temp));

        power_update (
            1,      // fridge 2 
            cooling_needed(
                set_field[F2_SET].value,
                set_field[F2_COOL].value,
                sensor_field[F2_SENSOR_BEER].temp,
                sensor_field[F2_SENSOR_AIR].temp),
            heating_needed(
                set_field[F2_SET].value,
                set_field[F2_HEAT].value,
                sensor_field[F2_SENSOR_BEER].temp,
                sensor_field[F2_SENSOR_HEAT].temp));

        // display the fridge power state indicators, if not in SLEEP or a SENSOR mode
        if (mode >= UI_MODE_STATUS && mode < UI_MODE_SENSOR_1) {
                //      01234567890123456789
                //      FRIDGE *1  FRIDGE ^2
                lcd_gotoxy(7, 0);
                lcd_putc(power_state_indicator[power_state[0]]);
                lcd_gotoxy(18, 0);
                lcd_putc(power_state_indicator[power_state[1]]);
        }

        // wait up to `UI_BLINK_MS` for an event from the rotary encoder
        //
        if (xQueueReceive(encoder_event_queue, &e, pdMS_TO_TICKS(UI_BLINK_MS)) == pdTRUE) {

            switch (e.type) {                               // handle the encoder event
                case RE_ET_BTN_CLICKED:
                    ui_event_handler(UI_EVENT_BTN_PRESS, 0);
                    break;

                case RE_ET_BTN_LONG_PRESSED:
                    ui_event_handler(UI_EVENT_BTN_LONG_PRESS, 0);
                    break;

                case RE_ET_CHANGED:
                    ui_event_handler(UI_EVENT_VALUE_CHANGE, e.diff);
                    break;

                default:
                    break;
            }

            long_timeout_count = 0;                         // reset the inactivity timers
            sleep_timeout_count = 0;

        } else {
            // if there are no rotary encoder events then manage the periodic "housekeeping" events
            //
            timeout_count = (timeout_count + 1) % UI_BLINKS_PER_FLASH;
            ui_event_handler(UI_EVENT_BLINK, 0);

            long_timeout_count = (long_timeout_count + 1) % UI_BLINKS_PER_TIMEOUT;
            if (long_timeout_count == 0) {
                ui_event_handler(UI_EVENT_TIMEOUT, 0);
            }

            sleep_timeout_count = (sleep_timeout_count + 1) % UI_BLINKS_PER_SLEEP;
            if (sleep_timeout_count == 0) {
                ui_event_handler(UI_EVENT_SLEEP, 0);
            }
        }

        // always check for new temperature data
        //
        struct temp_data_t *pTemp_data;
        if (xQueuePeek(temperature_queue, &(pTemp_data), 0) == pdTRUE) {
            temp_data = *pTemp_data;                        // take local copy
            ui_event_handler(UI_EVENT_NEW_TEMP_DATA, 0);    // process local copy

            // un-block the queue so that the sending task can continue
            xQueueReceive(temperature_queue, &(pTemp_data), 0);
        }

    }
}
