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

//! debug code
#include <inttypes.h>
//! end of debug code

#define COL_1   0                   // dislay column positions
#define COL_2   5
#define COL_3   11
#define COL_4   16
#define FIELD_INDICATOR_CHR 0x7e    // 0x7e = right-pointing arrow
#define UNDEFINED_TEMP -999         // temperature to be displayed as --.-


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

struct ui_state_t {
    enum ui_mode_t mode;
    int item;
    int value;
    int timeout_count;
    bool blink_is_hidden;
    struct temp_data_t temp_data;
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
static QueueHandle_t encoder_event_queue;
static rotary_encoder_t re;
static struct ui_state_t ui_state;
static char buf[10];

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

static const int num_sensor_fields = sizeof(sensor_field) / sizeof(struct sensor_field_t);

static struct temp_field_t temp_field[] = {
    { "set", COL_1, 1, COL_2, 1, UNDEFINED_TEMP },
    { "min", COL_1, 2, COL_2, 2, UNDEFINED_TEMP },
    { "set", COL_3, 1, COL_4, 1, UNDEFINED_TEMP },
    { "min", COL_3, 2, COL_4, 2, UNDEFINED_TEMP }
};

static const int num_temp_fields = sizeof(temp_field) / sizeof(struct temp_field_t);


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
        hd44780_gotoxy(&lcd, sensor_field[i].data_x, sensor_field[i].data_y);
        if (sensor_field[i].temperature == UNDEFINED_TEMP) {
            hd44780_puts(&lcd, "--.-");
        } else {
            snprintf(buf, sizeof(buf), "%4.1f", sensor_field[i].temperature);
            hd44780_puts(&lcd, buf);
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
    switch (ui_state.mode) {
        case UI_MODE_SPLASH:
            hd44780_clear(&lcd);
            hd44780_gotoxy(&lcd, 5, 1);
            hd44780_puts(&lcd, "TETB dual");
            hd44780_gotoxy(&lcd, 2, 2);
            hd44780_puts(&lcd, "fridge controller");
            break;

        case UI_MODE_SLEEP:
            hd44780_clear(&lcd);
            hd44780_puts(&lcd, "sleep");
            break;

        case UI_MODE_STATUS:
            hd44780_clear(&lcd);
            //                  01234567890123456789
            hd44780_puts(&lcd, "FRIDGE  1  FRIDGE  2");
            for (int i = 0; i < num_sensor_fields; i++) {
                hd44780_gotoxy(&lcd, sensor_field[i].title_x, sensor_field[i].title_y);
                hd44780_puts(&lcd, sensor_field[i].title);
            }
            display_sensor_temps();

            // reset selected item
            ui_state.item = 0;
            break;

        case UI_MODE_TEMP_ITEM:
            hd44780_clear(&lcd);
            //                  01234567890123456789
            hd44780_puts(&lcd, "TEMPERATURE SETTINGS");
            for (int i = 0; i < num_temp_fields; i++) {
                hd44780_gotoxy(&lcd, temp_field[i].title_x, temp_field[i].title_y);
                hd44780_puts(&lcd, temp_field[i].title);
                hd44780_gotoxy(&lcd, temp_field[i].data_x, temp_field[i].data_y);
                temp_to_str(buf, sizeof(buf), temp_field[i].value);
                hd44780_puts(&lcd, buf);
            }
            ui_state.value = temp_field[ui_state.item].value;
            hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x - 1, temp_field[ui_state.item].data_y);
            hd44780_putc(&lcd, FIELD_INDICATOR_CHR);
            ui_state.blink_is_hidden = false;
            ui_state.timeout_count = 2;                                 // start new 'blink' cycle
            break;

        case UI_MODE_SENSOR_ITEM:
            // no need to clear screen
            hd44780_gotoxy(&lcd, 0, 0);
            //                  01234567890123456789
            hd44780_puts(&lcd, "SELECT SENSORS      ");
            hd44780_gotoxy(&lcd, sensor_field[ui_state.item].data_x - 1, sensor_field[ui_state.item].data_y);
            hd44780_putc(&lcd, FIELD_INDICATOR_CHR);
            ui_state.blink_is_hidden = false;
            ui_state.timeout_count = 2;                                 // start new 'blink' cycle
            break;

        case UI_MODE_TEMP_EDIT:
            // no need to clear screen

            if (ui_state.blink_is_hidden) {
                // redraw selected item indicator
                hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x - 1, temp_field[ui_state.item].data_y);
                hd44780_putc(&lcd, FIELD_INDICATOR_CHR);
            }
            break;

        case UI_MODE_SENSOR_EDIT:
            hd44780_clear(&lcd);
            hd44780_puts(&lcd, "sensor edit");
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

    switch(event) {
        case UI_EVENT_BTN_PRESS:
            if (ui_state.mode == UI_MODE_SLEEP) {
                hd44780_switch_backlight(&lcd, true);                   // wake from sleep
            } else if (ui_state.mode == UI_MODE_TEMP_EDIT) {
                temp_field[ui_state.item].value = ui_state.value;       // 'commit' current UI value
                ui_state.item = (ui_state.item + 1) % num_temp_fields;  // advance to next setting
            }

            ui_state.mode = next_state_btn_press[ui_state.mode];
            new_mode();
            break;

        case UI_EVENT_BTN_LONG_PRESS:
            ui_state.mode = next_state_long_press[ui_state.mode];
            new_mode();
            break;

        case UI_EVENT_TIMEOUT:
            if (ui_state.mode != next_state_timeout[ui_state.mode]) {
                ui_state.mode = next_state_timeout[ui_state.mode];
                new_mode();
            }
            break;

        case UI_EVENT_VALUE_CHANGE:
            if (ui_state.mode == UI_MODE_TEMP_ITEM) {
                if (!ui_state.blink_is_hidden) {
                    // remove current selection indicator
                    hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x - 1, temp_field[ui_state.item].data_y);
                    hd44780_putc(&lcd, 0x20);
                }

                // update selected item
                ui_state.item += value_change;
                while (ui_state.item >= num_temp_fields) {
                    ui_state.item -= num_temp_fields;
                }
                while (ui_state.item < 0) {
                    ui_state.item += num_temp_fields;
                }

                // update the current ui 'value'
                ui_state.value = temp_field[ui_state.item].value;

                // show new item indicator
                hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x - 1, temp_field[ui_state.item].data_y);
                hd44780_putc(&lcd, FIELD_INDICATOR_CHR);
                ui_state.blink_is_hidden = false;
                ui_state.timeout_count = 2;         // start new 'blink' cycle
            } else if (ui_state.mode == UI_MODE_TEMP_EDIT) {
                hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x, temp_field[ui_state.item].data_y);

                if (ui_state.value == UNDEFINED_TEMP) {    // update ui value (NB this is not yet committed)
                    ui_state.value = 0;
                }
                ui_state.value += value_change;

                temp_to_str(buf, sizeof(buf), ui_state.value);
                hd44780_puts(&lcd, buf);
                ui_state.blink_is_hidden = false;
                ui_state.timeout_count = 2;         // start new blink cycle

            } else if (ui_state.mode == UI_MODE_SENSOR_ITEM) {
                if (!ui_state.blink_is_hidden) {
                    // remove current selection indicator
                    hd44780_gotoxy(&lcd, sensor_field[ui_state.item].data_x - 1, sensor_field[ui_state.item].data_y);
                    hd44780_putc(&lcd, 0x20);
                }

                // update selected item
                ui_state.item += value_change;
                while (ui_state.item >= num_sensor_fields) {
                    ui_state.item -= num_sensor_fields;
                }
                while (ui_state.item < 0) {
                    ui_state.item += num_sensor_fields;
                }

                // show new item indicator
                hd44780_gotoxy(&lcd, sensor_field[ui_state.item].data_x - 1, sensor_field[ui_state.item].data_y);
                hd44780_putc(&lcd, FIELD_INDICATOR_CHR);
                ui_state.blink_is_hidden = false;
                ui_state.timeout_count = 2;         // start new 'blink' cycle
            }
            break;

        case UI_EVENT_BLINK:
            if (ui_state.mode == UI_MODE_TEMP_ITEM) {
                // do blink
                if (ui_state.blink_is_hidden) {
                    if (ui_state.timeout_count != 0) {
                        // redraw item indicator
                        hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x - 1, temp_field[ui_state.item].data_y);
                        hd44780_putc(&lcd, FIELD_INDICATOR_CHR);
                        ui_state.blink_is_hidden = false;
                    }
                } else {
                    // field is not hidden
                    if (ui_state.timeout_count == 0) {
                        // hide item indicator
                        hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x - 1, temp_field[ui_state.item].data_y);
                        hd44780_putc(&lcd, 0x20);
                        ui_state.blink_is_hidden = true;
                    }
                }
            } else if (ui_state.mode == UI_MODE_TEMP_EDIT) {
                // do blink
                if (ui_state.blink_is_hidden) {
                    if (ui_state.timeout_count != 0) {
                        // un-hide field
                        hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x, temp_field[ui_state.item].data_y);
                        temp_to_str(buf, sizeof(buf), ui_state.value);
                        hd44780_puts(&lcd, buf);
                        ui_state.blink_is_hidden = false;
                    }
                } else {
                    // field is not hidden
                    if (ui_state.timeout_count == 0) {
                        // hide field
                        hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x, temp_field[ui_state.item].data_y);
                        hd44780_puts(&lcd, "    ");
                        ui_state.blink_is_hidden = true;
                    }
                }
            }
            break;

        case UI_EVENT_SLEEP:
            ui_state.mode = UI_MODE_SLEEP;
            hd44780_switch_backlight(&lcd, false);
            break;

        case UI_EVENT_NEW_TEMP_DATA:
            update_sensor_temps(&(ui_state.temp_data));
            if (ui_state.mode == UI_MODE_STATUS || ui_state.mode == UI_MODE_SENSOR_ITEM) {
                display_sensor_temps();
            }
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
            ui_state.timeout_count = (ui_state.timeout_count + 1) % UI_BLINKS_PER_FLASH;
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
            // take local copy of referenced data
            ui_state.temp_data = *pTemp_data;

            // process local copy of data
            ui_event_handler(UI_EVENT_NEW_TEMP_DATA, 0);

            // un-block queue so sending task can continue
            xQueueReceive(temperature_queue, &(pTemp_data), 0);
        }

    }
}
