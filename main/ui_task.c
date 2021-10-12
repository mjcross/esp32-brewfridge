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
    UI_MODE_SET_1,
    UI_MODE_SET_2,
    UI_MODE_SET_3,
    UI_MODE_SET_4,
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

struct sensor_field_t {
    const char title[5];
    const int title_x;
    const int title_y;
    const int data_x;
    const int data_y;
    ds18x20_addr_t addr;
    float temp;
};

struct set_field_t {
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
    UI_MODE_SET_1,                  // status -> set_1
    UI_MODE_SET_2,                  // set_1 -> set_2
    UI_MODE_SET_3,                  // set_2 -> set_3
    UI_MODE_SET_4,                  // set_3 -> set_4
    UI_MODE_SET_1,                  // set_4 -> set_1
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
    UI_MODE_STATUS,                 // sensor_1 -> status
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
    UI_MODE_STATUS,                 // sensor_1 -> status
    UI_MODE_STATUS,                 // sensor_2 -> status
    UI_MODE_STATUS,                 // sensor_3 -> status
    UI_MODE_STATUS,                 // sensor_4 -> status
    UI_MODE_STATUS,                 // sensor_5 -> status
    UI_MODE_STATUS                  // sensor_6 -> status
};

static struct sensor_field_t sensor_field[] = {
    { "air",  COL_1, 1, COL_2, 1, 0, UNDEFINED_TEMP },
    { "keg1", COL_1, 2, COL_2, 2, 0, UNDEFINED_TEMP },
    { "keg2", COL_1, 3, COL_2, 3, 0, UNDEFINED_TEMP },
    { "air",  COL_3, 1, COL_4, 1, 0, UNDEFINED_TEMP },
    { "keg1", COL_3, 2, COL_4, 2, 0, UNDEFINED_TEMP },
    { "keg2", COL_3, 3, COL_4, 3, 0, UNDEFINED_TEMP }
};

static struct set_field_t set_field[] = {
    { "set", COL_1, 1, COL_2, 1, UNDEFINED_TEMP },
    { "min", COL_1, 2, COL_2, 2, UNDEFINED_TEMP },
    { "set", COL_3, 1, COL_4, 1, UNDEFINED_TEMP },
    { "min", COL_3, 2, COL_4, 2, UNDEFINED_TEMP }
};

static const char power_state_indicator[] = {
    '*',    // pwr_on
    '-',    // pwr_on_pending
    ' ',    // pwr_off
    '+',    // pwr_off_pending
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
bool blink_enabled;


// function definitions
// --------------------

static void value_to_temp_str(char *buf, size_t buflen, int temp) {
    if (temp == UNDEFINED_TEMP) {
        snprintf(buf, buflen, "--.-");
    } else {
        snprintf(buf, buflen, "%4.1f", temp / 10.0);
    }
}


static void status_display_sensor_temps(void) {
    for (int i = 0; i < num_sensor_fields; i += 1) {
        lcd_gotoxy(sensor_field[i].data_x, sensor_field[i].data_y);
        if (sensor_field[i].temp == UNDEFINED_TEMP) {
            lcd_puts("--.-");
        } else {
            snprintf(buf, sizeof(buf), "%4.1f", sensor_field[i].temp);
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


static void show_sensors() {
    bool addr_found = false;

    // show temps for attached sensors
    for (int i = 0; i < temp_data.num_sensors; i += 1) {
        lcd_gotoxy((i % 4) * 5, (i / 4) + 1);
        if (i == 0) {
            lcd_puts("--.-");
        } else {
            snprintf(buf, sizeof(buf), "%4.1f", temp_data.temp[i]);
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


void new_mode_set(int i) {
    i -= 1;
    lcd_restore();
    blink_x = set_field[i].data_x;
    blink_y = set_field[i].data_y;
    blink_enabled = true;
    lcd_hide(blink_x, blink_y, 4);
    timeout_count = 0;
}


void new_mode_sensor(int i) {
    i -= 1;
    lcd_gotoxy(0, 0);

    lcd_puts(sensor_field[i].title);
    if (i < 3) {
        lcd_puts(" fridge 1 ");
    } else {
        lcd_puts(" fridge 2 ");
    }

    addr = sensor_field[i].addr;
    show_sensors();
    blink_enabled = true;
    lcd_hide(blink_x, blink_y, 4);
    timeout_count = 0;
}


static void new_mode() {
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
            new_mode_set(1);
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

        case UI_MODE_SENSOR_1:
            lcd_clear();
            new_mode_sensor(1);
            break;

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


static void set_field_value_change(int i, int diff) {
    if (set_field[i].value == UNDEFINED_TEMP) {
        set_field[i].value = 0;
    }
    set_field[i].value += diff;
    lcd_gotoxy(set_field[i].data_x, set_field[i].data_y);
    value_to_temp_str(buf, sizeof(buf), set_field[i].value);
    lcd_puts(buf);
    timeout_count = 2;
}


static void sensor_addr_change(int i, int diff) {
    i -= 1;
    int sensor_index = find_sensor(addr);
    sensor_index = (sensor_index + diff) % (int)temp_data.num_sensors;
    if (sensor_index < 0) {
        sensor_index += temp_data.num_sensors;
    }
    addr = temp_data.addr[sensor_index];
    sensor_field[i].addr = addr;
    blink_x = (sensor_index % 4) * 5;
    blink_y = (sensor_index / 4) + 1;
    lcd_hide(blink_x, blink_y, 4);
    timeout_count = 0;
}


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


static void control_fridges() {
    if (cooling_needed(set_field[0].value,          // fridge 1 set
                       set_field[1].value,          // fridge 1 min
                       sensor_field[0].temp,        // fridge 1 air
                       sensor_field[1].temp))       // fridge 1 keg1
    {
        f1_power_on();
    } else {
        f1_power_off();
    }

    if (cooling_needed(set_field[2].value,          // fridge 2 set
                       set_field[3].value,          // fridge 2 min
                       sensor_field[3].temp,        // fridge 2 air
                       sensor_field[4].temp))       // fridge 2 keg1
    {
        f2_power_on();
    } else {
        f2_power_off();
    }


    if (mode == UI_MODE_STATUS
        || mode == UI_MODE_SET_1
        || mode == UI_MODE_SET_2
        || mode == UI_MODE_SET_3
        || mode == UI_MODE_SET_4)
    {
        // 01234567890123456789
        // FRIDGE *1  FRIDGE *2
        lcd_gotoxy(7, 0);
        lcd_putc(power_state_indicator[f1_state]);
        lcd_gotoxy(18, 0);
        lcd_putc(power_state_indicator[f2_state]);
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
        // apply thermostat control
        control_fridges();

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
            temp_data = *pTemp_data;                        // take local copy
            ui_event_handler(UI_EVENT_NEW_TEMP_DATA, 0);    // process local copy

            // un-block queue so sending task can continue
            xQueueReceive(temperature_queue, &(pTemp_data), 0);
        }

    }
}
