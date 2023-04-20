#include <string.h>         // for memset()
#include <hd44780.h>
#include <pcf8574.h>
#include "defines.h"

static i2c_dev_t pcf8574;   // see https://esp-idf-lib.readthedocs.io/en/latest/groups/i2cdev.html

static unsigned char lcd_buffer[5 * 20];
static int lcd_row;
static int lcd_col;
static int hidden_row;
static int hidden_col;
static char buf[21];


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


void lcd_dump() {
    for (int row = 0; row < 4; row ++) {
        printf("\n");
        for (int col = 0; col < 20; col ++) {
            printf("%c", lcd_buffer[row * 20 + col]);
        }
    }
    printf("\nhidden: '%s' at (%d, %d)\n", buf, hidden_col, hidden_row);
}


void lcd_clear() {
    hd44780_clear(&lcd);
    memset(lcd_buffer, 0x20, 5 * 20);
    lcd_row = 0;
    lcd_col = 0;
    buf[0] = '\0';
}


void lcd_reset() {
    ESP_ERROR_CHECK(hd44780_init(&lcd));
    memset(lcd_buffer, 0x20, 5 * 20);
}


void lcd_init() {
    ESP_ERROR_CHECK(i2cdev_init());                     // initialise the I2C library
    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574,         // struct i2c_dev_t*
                                      I2C_ADDR,         // from define.h
                                      0,                // i2c_port_t
                                      SDA_GPIO,         // .. .. ..
                                      SCL_GPIO));       // .. .. ..

    //pcf8574.cfg.master.clk_speed = 50000;  // reduce I2C bus speed a bit to improve noise immunity

    lcd_reset();
    hd44780_switch_backlight(&lcd, true);
}


void lcd_gotoxy(int x, int y) {
    hd44780_gotoxy(&lcd, x, y);
    lcd_col = x;
    lcd_row = y;
}


void lcd_switch_backlight(bool state) {
    hd44780_switch_backlight(&lcd, state);
}


void lcd_putc(const char c) {
    hd44780_putc(&lcd, c);
    lcd_col += 1;
    if (lcd_col > 19) {
        lcd_col = 19;
    }
}


void lcd_puts(const char *buf) {
    hd44780_puts(&lcd, buf);
    int len = (int)strnlen(buf, 20);
    if (len > (20 - lcd_col)) {
        len = 20 - lcd_col;
    }
    memcpy(lcd_buffer + lcd_row * 20 + lcd_col, buf, len);
    lcd_col += len;
}


void lcd_hide(int x, int y, int num_chars) {
    if (num_chars >= sizeof(buf)) {
        num_chars = sizeof(buf) - 1;
    }

    memset(buf, ' ', num_chars);
    buf[num_chars] = '\0';
    hd44780_gotoxy(&lcd, x, y);
    hd44780_puts(&lcd, buf);

    memcpy(buf, lcd_buffer + y * 20 + x, num_chars);
    buf[num_chars] = '\0';
    hidden_row = y;
    hidden_col = x;
}


void lcd_restore(void) {
    if (buf[0] != '\0') {
        hd44780_gotoxy(&lcd, hidden_col, hidden_row);
        hd44780_puts(&lcd, buf);
        buf[0] = '\0';
    }
}
