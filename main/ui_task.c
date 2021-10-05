#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>                 // memset()

#include <ds18x20.h>                // oneWire temperature sensor
#include <encoder.h>                // rotary encoder

#include "defines.h"
#include "ui_task.h"
#include "lcd.h"

static const char* ui_mode_str[] = {
    "ui_mode_splash",
    "ui_mode_sleep",
    "ui_mode_status",
    "ui_mode_temp_item",
    "ui_mode_temp_edit",
    "ui_mode_temp_commit",
    "ui_mode_sensor_item",
    "ui_mode_sensor_edit",
    "ui_mode_sensor_commit"
};

static const enum ui_mode_t next_state_btn_press[] = {
    UI_MODE_STATUS,                 // splash -> status
    UI_MODE_STATUS,                 // sleep -> status
    UI_MODE_TEMP_ITEM,              // status -> temp item
    UI_MODE_TEMP_EDIT,              // temp item -> temp edit
    UI_MODE_TEMP_COMMIT,            // temp edit -> temp commit
    UI_MODE_TEMP_ITEM,              // temp commit -> temp item
    UI_MODE_SENSOR_EDIT,            // sensor item -> sensor edit
    UI_MODE_SENSOR_COMMIT,          // sensor edit -> sensor commit
    UI_MODE_SENSOR_ITEM             // sensor commit -> sensor item
};

static const enum ui_mode_t next_state_long_press[] = {
    UI_MODE_STATUS,                 // splash -> status
    UI_MODE_STATUS,                 // sleep -> status
    UI_MODE_SENSOR_ITEM,            // status -> sensor item
    UI_MODE_STATUS,                 // temp item -> status
    UI_MODE_STATUS,                 // temp edit -> status
    UI_MODE_STATUS,                 // temp commit -> status
    UI_MODE_STATUS,                 // sensor item -> status
    UI_MODE_STATUS,                 // sensor edit -> status
    UI_MODE_STATUS                  // sensor commit -> status
};

static const enum ui_mode_t next_state_timeout[] = {
    UI_MODE_STATUS,                 // splash -> status
    UI_MODE_SLEEP,                  // sleep -> sleep
    UI_MODE_STATUS,                 // status -> status
    UI_MODE_STATUS,                 // temp item -> status
    UI_MODE_TEMP_ITEM,              // temp edit -> temp item
    UI_MODE_TEMP_EDIT,              // temp commit -> temp_edit
    UI_MODE_STATUS,                 // sensor item -> status
    UI_MODE_SENSOR_ITEM,            // sensor edit -> sensor item
    UI_MODE_SENSOR_EDIT             // sensor commit -> sensor edit
};

enum ui_event_t {
    UI_EVENT_BTN_PRESS,
    UI_EVENT_BTN_LONG_PRESS,
    UI_EVENT_VALUE_CHANGE,
    UI_EVENT_BLINK_SHOW,
    UI_EVENT_BLINK_HIDE,
    UI_EVENT_TIMEOUT,
    UI_EVENT_SLEEP
};

struct ui_state_t {
    enum ui_mode_t mode;
    int item;
    int value;
    bool blink_active;
    bool blink_hidden;
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

struct set_field_t {
    const char title[5];
    const int title_x;
    const int title_y;
    const int data_x;
    const int data_y;
    int value;
};

static struct set_field_t set_field[] = {
    { "set", COL_1, 1, COL_2, 1, 0 },
    { "min", COL_1, 2, COL_2, 2, 0 },
    { "set", COL_3, 1, COL_4, 1, 0 },
    { "min", COL_3, 2, COL_4, 2, 0 }
};

const int num_set_fields = sizeof(set_field) / sizeof(struct set_field_t);


static QueueHandle_t encoder_event_queue;
static rotary_encoder_t re;
static struct ui_state_t ui_state;


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


static void ui_event(enum ui_event_t event, int value_change) {
    printf("%s: ", ui_mode_str[ui_state.mode]);

    switch(event) {
        case UI_EVENT_BTN_PRESS:
            ui_state.mode = next_state_btn_press[ui_state.mode];
            printf("button press event -> ");
            break;

        case UI_EVENT_BTN_LONG_PRESS:
            ui_state.mode = next_state_long_press[ui_state.mode];
            printf("long press event -> ");
            break;

        case UI_EVENT_TIMEOUT:
            ui_state.mode = next_state_timeout[ui_state.mode];
            printf("timeout event -> ");
            break;

        default:
            printf("unrecognised event -> ");
            break;
    }

    printf("%s\n", ui_mode_str[ui_state.mode]);
}



void ui_task(void *pParams) {
    rotary_encoder_event_t e;

    encoder_init();
    for(;;) {
        if (xQueueReceive(encoder_event_queue, &e, pdMS_TO_TICKS(UI_TIMEOUT_MS)) == pdTRUE) {

            switch (e.type) {
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
        } else {
            ui_event(UI_EVENT_TIMEOUT, 0);
        }
    }
}
