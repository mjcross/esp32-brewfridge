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
#include "ui_task.h"
#include "lcd.h"
#include "globals.h"

/*static const char* ui_mode_str[] = {
    "ui_mode_splash",
    "ui_mode_sleep",
    "ui_mode_status",
    "ui_mode_temp_item",
    "ui_mode_temp_edit",
    "ui_mode_temp_commit",
    "ui_mode_sensor_item",
    "ui_mode_sensor_edit",
    "ui_mode_sensor_commit"
};*/

enum ui_mode_t {
    UI_MODE_SPLASH = 0,
    UI_MODE_SLEEP,
    UI_MODE_STATUS,
    UI_MODE_TEMP_ITEM,
    UI_MODE_TEMP_EDIT,
    UI_MODE_SENSOR_ITEM,
    UI_MODE_SENSOR_EDIT
};

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

enum ui_event_t {
    UI_EVENT_BTN_PRESS,
    UI_EVENT_BTN_LONG_PRESS,
    UI_EVENT_VALUE_CHANGE,
    UI_EVENT_BLINK,
    UI_EVENT_TIMEOUT,
    UI_EVENT_SLEEP
};

struct ui_state_t {
    enum ui_mode_t mode;
    int item;
    int value;
    int timeout_count;
    bool blink_is_hidden;
};

#define COL_1   0
#define COL_2   5
#define COL_3   11
#define COL_4   16

struct sensor_field_t {
    const char title[5];
    const int title_x;
    const int title_y;
    const int data_x;
    const int data_y;
    ds18x20_addr_t addr;
};

static struct sensor_field_t sensor_field[] = {
    { "air",  COL_1, 1, COL_2, 1, 0 },
    { "keg1", COL_1, 2, COL_2, 2, 0 },
    { "keg2", COL_1, 3, COL_2, 3, 0 },
    { "air",  COL_3, 1, COL_4, 1, 0 },
    { "keg1", COL_3, 2, COL_4, 2, 0 },
    { "keg2", COL_3, 3, COL_4, 3, 0 }
};

const int num_sensor_fields = sizeof(sensor_field) / sizeof(struct sensor_field_t);

struct temp_field_t {
    const char title[5];
    const int title_x;
    const int title_y;
    const int data_x;
    const int data_y;
    int value;
};

static struct temp_field_t temp_field[] = {
    { "set", COL_1, 1, COL_2, 1, 0 },
    { "min", COL_1, 2, COL_2, 2, 0 },
    { "set", COL_3, 1, COL_4, 1, 0 },
    { "min", COL_3, 2, COL_4, 2, 0 }
};

const int num_temp_fields = sizeof(temp_field) / sizeof(struct temp_field_t);


static QueueHandle_t encoder_event_queue;
static rotary_encoder_t re;
static struct ui_state_t ui_state;
static char buf[10];


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


void ui_test() {
    for (int i = 0; i < num_sensor_fields; i++) {
        struct sensor_field_t sf = sensor_field[i];
        hd44780_gotoxy(&lcd, sf.title_x, sf.title_y);
        hd44780_puts(&lcd, sf.title);
        hd44780_gotoxy(&lcd, sf.data_x, sf.data_y);
        hd44780_puts(&lcd, "--.-");
    }
};


static void new_mode() {
    switch (ui_state.mode) {
        case UI_MODE_SPLASH:
            lcd_clear();
            //                  01234567890123456789
            hd44780_gotoxy(&lcd, 5, 1);
            hd44780_puts(&lcd, "TETB dual");
            hd44780_gotoxy(&lcd, 2, 2);
            hd44780_puts(&lcd, "fridge controller");
            break;

        case UI_MODE_SLEEP:
            lcd_clear();
            hd44780_puts(&lcd, "sleep");
            break;

        case UI_MODE_STATUS:
            lcd_clear();
            //                  01234567890123456789
            hd44780_puts(&lcd, "FRIDGE  1  FRIDGE  2");
            for (int i = 0; i < num_sensor_fields; i++) {
                struct sensor_field_t sf = sensor_field[i];
                hd44780_gotoxy(&lcd, sf.title_x, sf.title_y);
                hd44780_puts(&lcd, sf.title);
                hd44780_gotoxy(&lcd, sf.data_x, sf.data_y);
                hd44780_puts(&lcd, "--.-");
            }

            // reset selected item
            ui_state.item = 0;
            break;

        case UI_MODE_TEMP_ITEM:
            lcd_clear();
            //                  01234567890123456789
            hd44780_puts(&lcd, "TEMPERATURE SETTINGS");
            for (int i = 0; i < num_temp_fields; i++) {
                struct temp_field_t sf = temp_field[i];
                if (i != ui_state.item) {                               // blink first item title
                    hd44780_gotoxy(&lcd, sf.title_x, sf.title_y);
                    hd44780_puts(&lcd, sf.title);
                }
                hd44780_gotoxy(&lcd, sf.data_x, sf.data_y);
                snprintf(buf, sizeof(buf), "%4.1f", sf.value / 10.0);
                hd44780_puts(&lcd, buf);
            }
            ui_state.value = temp_field[ui_state.item].value;
            ui_state.blink_is_hidden = true;
            ui_state.timeout_count = 0;         // start new 'blink' cycle
            break;

        case UI_MODE_TEMP_EDIT:
            // NB: don't clear the screen

            if (ui_state.blink_is_hidden) {
                // un-hide selected item title
                hd44780_gotoxy(&lcd, temp_field[ui_state.item].title_x, temp_field[ui_state.item].title_y);
                hd44780_puts(&lcd, temp_field[ui_state.item].title);
            }
            break;

        case UI_MODE_SENSOR_ITEM:
            lcd_clear();
            hd44780_puts(&lcd, "sensor item");
            break;

        case UI_MODE_SENSOR_EDIT:
            lcd_clear();
            hd44780_puts(&lcd, "sensor edit");
            break;

        default:
            ESP_LOGE(TAG, "unrecognised mode");
            break;
    }
}


static void ui_event(enum ui_event_t event, int value_change) {

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
                if (ui_state.blink_is_hidden) {
                    // un-hide current (old) selected item
                    hd44780_gotoxy(&lcd, temp_field[ui_state.item].title_x, temp_field[ui_state.item].title_y);
                    hd44780_puts(&lcd, temp_field[ui_state.item].title);
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

                // hide new item immediately to give visual feedback
                hd44780_gotoxy(&lcd, temp_field[ui_state.item].title_x, temp_field[ui_state.item].title_y);
                hd44780_puts(&lcd, "    ");
                ui_state.blink_is_hidden = true;
                ui_state.timeout_count = 0;         // start new 'blink' cycle

            } else if (ui_state.mode == UI_MODE_TEMP_EDIT) {
                hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x, temp_field[ui_state.item].data_y);
                ui_state.value += value_change;     // update ui value (NB this is not yet committed)
                snprintf(buf, sizeof(buf), "%4.1f", ui_state.value / 10.0);
                hd44780_puts(&lcd, buf);
                ui_state.blink_is_hidden = false;
                ui_state.timeout_count = 2;         // start new blink cycle
            }

            break;

        case UI_EVENT_BLINK:
            if (ui_state.mode == UI_MODE_TEMP_ITEM) {
                // do blink
                if (ui_state.blink_is_hidden) {
                    if (ui_state.timeout_count != 0) {
                        // un-hide field
                        hd44780_gotoxy(&lcd, temp_field[ui_state.item].title_x, temp_field[ui_state.item].title_y);
                        hd44780_puts(&lcd, temp_field[ui_state.item].title);
                        ui_state.blink_is_hidden = false;
                    }
                } else {
                    // field is not hidden
                    if (ui_state.timeout_count == 0) {
                        // hide field
                        hd44780_gotoxy(&lcd, temp_field[ui_state.item].title_x, temp_field[ui_state.item].title_y);
                        hd44780_puts(&lcd, "     ");
                        ui_state.blink_is_hidden = true;
                    }
                }
            } else if (ui_state.mode == UI_MODE_TEMP_EDIT) {
                // do blink
                if (ui_state.blink_is_hidden) {
                    if (ui_state.timeout_count != 0) {
                        // un-hide field
                        hd44780_gotoxy(&lcd, temp_field[ui_state.item].data_x, temp_field[ui_state.item].data_y);
                        snprintf(buf, sizeof(buf), "%4.1f", ui_state.value / 10.0);
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
        if (xQueueReceive(encoder_event_queue, &e, pdMS_TO_TICKS(UI_BLINK_MS)) == pdTRUE) {

            switch (e.type) {                               // handle event
                case RE_ET_BTN_CLICKED:
                    ui_event(UI_EVENT_BTN_PRESS, 0);
                    break;

                case RE_ET_BTN_LONG_PRESSED:
                    ui_event(UI_EVENT_BTN_LONG_PRESS, 0);
                    break;

                case RE_ET_CHANGED:
                    ui_event(UI_EVENT_VALUE_CHANGE, e.diff);
                    break;

                default:
                    break;
            }

            long_timeout_count = 0;                         // reset inactivity timers
            sleep_timeout_count = 0;

        } else {
            ui_state.timeout_count = (ui_state.timeout_count + 1) % UI_BLINKS_PER_FLASH;
            ui_event(UI_EVENT_BLINK, 0);

            long_timeout_count = (long_timeout_count + 1) % UI_BLINKS_PER_TIMEOUT;
            if (long_timeout_count == 0) {
                ui_event(UI_EVENT_TIMEOUT, 0);
            }

            sleep_timeout_count = (sleep_timeout_count + 1) % UI_BLINKS_PER_SLEEP;
            if (sleep_timeout_count == 0) {
                ui_event(UI_EVENT_SLEEP, 0);
            }
        }
    }
}
