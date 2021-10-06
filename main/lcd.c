#include <string.h>         // for memset()
#include <hd44780.h>
#include <pcf8574.h>
#include "defines.h"

static i2c_dev_t pcf8574;   // see https://esp-idf-lib.readthedocs.io/en/latest/groups/i2cdev.html

static esp_err_t write_lcd_data(const hd44780_t *lcd, uint8_t data) {
    return pcf8574_port_write(&pcf8574, data);
}

hd44780_t lcd = {
        .write_cb = write_lcd_data, // use callback to send data to LCD via I2C GPIO expander
        .font = HD44780_FONT_5X8,
        .lines = 4,
        .pins = {
            .rs = 0,
            .e  = 2,
            .d4 = 4,
            .d5 = 5,
            .d6 = 6,
            .d7 = 7,
            .bl = 3
        }
    };


void lcd_reset() {
    ESP_ERROR_CHECK(hd44780_init(&lcd));
}


void lcd_init() {
    ESP_ERROR_CHECK(i2cdev_init());                     // initialise the I2C library
    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574,         // struct i2c_dev_t*
                                      0,                // i2c_port_t
                                      I2C_ADDR,         // from define.h
                                      SDA_GPIO,         // .. .. ..
                                      SCL_GPIO));       // .. .. ..

    //pcf8574.cfg.master.clk_speed = 50000;  // reduce I2C bus speed a bit to improve noise immunity

    lcd_reset();
    hd44780_switch_backlight(&lcd, true);
}


void lcd_clear() {
/*    for (int i = 0; i < 4; i++) {
        hd44780_gotoxy(&lcd, 0, i);
        hd44780_puts(&lcd, "                    ");     // print row full of spaces
    }
    hd44780_gotoxy(&lcd, 0, 0);*/
    hd44780_clear(&lcd);
}
