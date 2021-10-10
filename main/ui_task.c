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


#define COL_1   0                   // dislay column positions
#define COL_2   5
#define COL_3   11
#define COL_4   16
#define FIELD_INDICATOR_CHR 0x7e    // 0x7e = right-pointing arrow


// type definitions
// ----------------
enum ui_mode_t {
    UI_MODE_SPLASH = 0,
    UI_MODE_SLEEP,
    UI_MODE_STATUS,
    UI_MODE_TEMP_ITEM,
    UI_MODE_TEMP_EDIT,
    UI_MODE_SENSOR_ITEM,
    UI_MODE_SENSOR_EDIT
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

struct sensor_field_t {
    const char title[5];
    const int title_x;
    const int title_y;
    const int data_x;
    const int data_y;
    ds18x20_addr_t addr;
    float temperature;
};

struct temp_field_t {
    const char title[5];
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
    UI_MODE_TEMP_ITEM,              // status -> temp item
    UI_MODE_TEMP_EDIT,              // temp item -> temp edit
    UI_MODE_TEMP_ITEM,              // temp edit -> temp item
    UI_MODE_SENSOR_EDIT,            // sensor item -> sensor edit
    UI_MODE_SENSOR_ITEM             // sensor edit -> sensor item
};

static const enum ui_mode_t next_state_long_press[] = {
    UI_MODE_STATUS,                 // splash -> status
    UI_MODE_STATUS,                 // sleep -> status
    UI_MODE_SENSOR_ITEM,            // status -> sensor item
    UI_MODE_STATUS,                 // temp item -> status
    UI_MODE_STATUS,                 // temp edit -> status
    UI_MODE_STATUS,                 // sensor item -> status
    UI_MODE_STATUS,                 // sensor edit -> status
};

static const enum ui_mode_t next_state_timeout[] = {
    UI_MODE_STATUS,                 // splash -> status
    UI_MODE_SLEEP,                  // sleep -> sleep
    UI_MODE_STATUS,                 // status -> status
    UI_MODE_STATUS,                 // temp item -> status
    UI_MODE_STATUS,                 // temp edit -> status
    UI_MODE_STATUS,                 // sensor item -> status
    UI_MODE_STATUS,                 // sensor edit -> status
};

//! NB: sensor addresses hard-coded for testing purposes
static struct sensor_field_t sensor_field[] = {
    { "air",  COL_1, 1, COL_2, 1, 0x1a3c01d0751d0c28, UNDEFINED_TEMP },
    { "keg1", COL_1, 2, COL_2, 2, 0xbf3c01d07515ea28, UNDEFINED_TEMP },
    { "keg2", COL_1, 3, COL_2, 3, 0xbb3c01d075e74628, UNDEFINED_TEMP },
    { "air",  COL_3, 1, COL_4, 1, 0xfc3c01d0758b1528, UNDEFINED_TEMP },
    { "keg1", COL_3, 2, COL_4, 2, 0x993c01d075ff3d28, UNDEFINED_TEMP },
    { "keg2", COL_3, 3, COL_4, 3, 0, UNDEFINED_TEMP }
};

static struct temp_field_t temp_field[] = {
    { "set", COL_1, 1, COL_2, 1, UNDEFINED_TEMP },
    { "min", COL_1, 2, COL_2, 2, UNDEFINED_TEMP },
    { "set", COL_3, 1, COL_4, 1, UNDEFINED_TEMP },
    { "min", COL_3, 2, COL_4, 2, UNDEFINED_TEMP }
};

static const int num_sensor_fields = sizeof(sensor_field) / sizeof(struct sensor_field_t);
static const int num_temp_fields = sizeof(temp_field) / sizeof(struct temp_field_t);
static QueueHandle_t encoder_event_queue;
static rotary_encoder_t re;
static char buf[10];
static enum ui_mode_t mode;
static int item;
static int value;
static ds18x20_addr_t addr;
static int timeout_count;
static struct temp_data_t temp_data;
static int blink_x;
static int blink_y;
bool blink_enabled;


// function definitions
// --------------------

static void temp_to_str(char *buf, size_t buflen, int temp) {
    if (temp == UNDEFINED_TEMP) {
        snprintf(buf, buflen, "--.-");
    } else {
        snprintf(buf, buflen, "%4.1f", temp / 10.0);
    }
}


static void display_sensor_temps(void) {
    for (int i = 0; i < num_sensor_fields; i += 1) {
        lcd_gotoxy(sensor_field[i].data_x, sensor_field[i].data_y);
        if (sensor_field[i].temperature == UNDEFINED_TEMP) {
            lcd_puts("--.-");
        } else {
            snprintf(buf, sizeof(buf), "%4.1f", sensor_field[i].temperature);
            lcd_puts(buf);
        }
    }
}


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


static void new_mode() {
    blink_enabled = false;
    switch (mode) {
        case UI_MODE_SPLASH:
            lcd_clear();
            lcd_gotoxy(5, 1);
            lcd_puts("TETB dual");
            lcd_gotoxy(2, 2);
            lcd_puts("fridge controller");
            break;

        case UI_MODE_SLEEP:
            lcd_clear();
            lcd_puts("sleep");
            break;

        case UI_MODE_STATUS:
            lcd_clear();
            //        01234567890123456789
            lcd_puts("FRIDGE  1  FRIDGE  2");
            for (int i = 0; i < num_sensor_fields; i++) {
                lcd_gotoxy(sensor_field[i].title_x, sensor_field[i].title_y);
                lcd_puts(sensor_field[i].title);
            }
            display_sensor_temps();

            // reset selected item
            item = 0;
            break;

        case UI_MODE_TEMP_ITEM:
            lcd_clear();
            //        01234567890123456789
            lcd_puts("TEMPERATURE SETTINGS");
            for (int i = 0; i < num_temp_fields; i += 1) {
                lcd_gotoxy(temp_field[i].title_x, temp_field[i].title_y);
                lcd_puts(temp_field[i].title);
                lcd_gotoxy(temp_field[i].data_x, temp_field[i].data_y);
                temp_to_str(buf, sizeof(buf), temp_field[i].value);
                lcd_puts(buf);
            }
            value = temp_field[item].value;
            blink_x = temp_field[item].title_x;
            blink_y = temp_field[item].title_y;
            blink_enabled = true;
            timeout_count = 2;                                 // start new 'blink' cycle
            break;

        case UI_MODE_TEMP_EDIT:
            // no need to clear screen
            lcd_restore();
            blink_x = temp_field[item].data_x;
            blink_y = temp_field[item].data_y;
            blink_enabled = true;
            timeout_count = 2;
            break;

        case UI_MODE_SENSOR_ITEM:
            // re-draw screen in case we are entering from UI_MODE_SENSOR_EDIT
            lcd_clear();
            //                  01234567890123456789
            lcd_puts("SELECT SENSORS");
            for (int i = 0; i < num_sensor_fields; i++) {
                lcd_gotoxy(sensor_field[i].title_x, sensor_field[i].title_y);
                lcd_puts(sensor_field[i].title);
            }
            display_sensor_temps();

            blink_x = COL_1;
            blink_y = 1;
            blink_enabled = true;
            timeout_count = 2;                                 // start new 'blink' cycle
            break;


        case UI_MODE_SENSOR_EDIT:
            lcd_clear();
            lcd_puts(sensor_field[item].title);
            for (int i = 0; i < temp_data.num_sensors; i += 1) {
                lcd_gotoxy(5 * (i % 4), 1 + (i / 4));
                if (i == 0) {
                    lcd_puts("--.-");
                } else {
                    snprintf(buf, sizeof(buf), "%4.1f", temp_data.temp[i]);
                    lcd_puts(buf);
                }
            }
            blink_x = COL_1;
            blink_y = 1;
            blink_enabled = true;
            break;

        default:
            ESP_LOGE(TAG, "unrecognised mode");
            break;
    }
}


static void update_sensor_temps(struct temp_data_t *pTemp) {
    for (int f = 0; f < num_sensor_fields; f += 1) {
        sensor_field[f].temperature = UNDEFINED_TEMP;
        if (sensor_field[f].addr != 0) {
            for (int s = 0; s < pTemp->num_sensors; s += 1) {
                if (pTemp->addr[s] == sensor_field[f].addr) {
                    sensor_field[f].temperature = pTemp->temp[s];
                }
            }
        }
    }
}


static void ui_event_handler(enum ui_event_t event, int value_change) {
    static int prev_num_sensors;

    switch(event) {
        case UI_EVENT_BTN_PRESS:
            lcd_restore();
            if (mode == UI_MODE_SLEEP) {
                lcd_switch_backlight(true);                 // wake from sleep
            } else if (mode == UI_MODE_TEMP_EDIT) {
                temp_field[item].value = value;             // 'commit' current UI value
                item = (item + 1) % num_temp_fields;        // advance to next setting
                blink_x = temp_field[item].title_x;
                blink_y = temp_field[item].title_y;
            }

            mode = next_state_btn_press[mode];
            new_mode();
            break;

        case UI_EVENT_BTN_LONG_PRESS:
            mode = next_state_long_press[mode];
            new_mode();
            break;

        case UI_EVENT_TIMEOUT:
            if (mode != next_state_timeout[mode]) {
                mode = next_state_timeout[mode];
                new_mode();
            }
            break;

        case UI_EVENT_VALUE_CHANGE:
            lcd_restore();
            if (mode == UI_MODE_TEMP_ITEM) {
                // update selected item
                item += value_change;
                while (item >= num_temp_fields) {
                    item -= num_temp_fields;
                }
                while (item < 0) {
                    item += num_temp_fields;
                }

                // update the current ui 'value'
                value = temp_field[item].value;

                // blink new item straight away
                blink_x = temp_field[item].title_x;
                blink_y = temp_field[item].title_y;
                lcd_hide(blink_x, blink_y, 4);
                timeout_count = 0;             // start new 'blink' cycle

            } else if (mode == UI_MODE_TEMP_EDIT) {
                lcd_gotoxy(blink_x, blink_y);

                if (value == UNDEFINED_TEMP) {          // update ui value (NB this is not yet committed)
                    value = 0;
                }
                value += value_change;

                temp_to_str(buf, sizeof(buf), value);
                lcd_puts(buf);
                timeout_count = 2;                      // start new blink cycle

            } else if (mode == UI_MODE_SENSOR_ITEM) {
                // update selected item
                item += value_change;
                while (item >= num_sensor_fields) {
                    item -= num_sensor_fields;
                }
                while (item < 0) {
                    item += num_sensor_fields;
                }

                // blink new item straight away
                blink_x = sensor_field[item].data_x;
                blink_y = sensor_field[item].data_y;
                lcd_hide(blink_x, blink_y, 4);
                timeout_count = 0;         // start new 'blink' cycle

                // update UI address
                addr = sensor_field[item].addr;
            }
            break;

        case UI_EVENT_BLINK:
            if (blink_enabled) {
                if (timeout_count == 0) {
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
            if (mode == UI_MODE_STATUS || mode == UI_MODE_SENSOR_ITEM) {
                display_sensor_temps();
            } else if (mode == UI_MODE_SENSOR_EDIT) {

                // erase any previous stale readings
                if (temp_data.num_sensors != prev_num_sensors) {
                    for (int i = temp_data.num_sensors; i < prev_num_sensors; i += 1) {
                        lcd_gotoxy((i % 4) * 5, 1 + (i /4));
                        lcd_puts("    ");
                    }
                    prev_num_sensors = temp_data.num_sensors;
                };

                // display new readings
                //
                for (int i = 0; i < temp_data.num_sensors; i += 1) {
                    lcd_gotoxy((i % 4) * 5, 1 + (i / 4));
                    if (i == 0) {
                        snprintf(buf, sizeof(buf), "--.-");
                    } else {
                        snprintf(buf, sizeof(buf), "%4.1f", temp_data.temp[i]);
                    }
                    if (addr == temp_data.addr[i]) {
                        blink_x = (i % 4) * 5;
                        blink_y = 1 + (i / 4);
                        lcd_hide(blink_x, blink_y, 4);
                        timeout_count = 0;
                    } else {
                        lcd_puts(buf);
                    }

                }
            } // UI_MODE_SENSOR_EDIT
            break;

        default:
            ESP_LOGE(TAG, "unrecognised event");
            break;
    }
}


void ui_task(void *pParams) {
    rotary_encoder_event_t e;

    lcd_init();
    encoder_init();
    new_mode();         // set up first screen

    static int long_timeout_count;
    static int sleep_timeout_count;

    for(;;) {
        // wait for encoder events
        //
        if (xQueueReceive(encoder_event_queue, &e, pdMS_TO_TICKS(UI_BLINK_MS)) == pdTRUE) {

            switch (e.type) {                               // handle event
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

            long_timeout_count = 0;                         // reset inactivity timers
            sleep_timeout_count = 0;

        } else {
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

        // check for new temperature data
        //
        struct temp_data_t *pTemp_data;
        if (xQueuePeek(temperature_queue, &(pTemp_data), 0) == pdTRUE) {
            temp_data = *pTemp_data;               // take local copy
            ui_event_handler(UI_EVENT_NEW_TEMP_DATA, 0);    // process local copy

            // un-block queue so sending task can continue
            xQueueReceive(temperature_queue, &(pTemp_data), 0);
        }

    }
}
