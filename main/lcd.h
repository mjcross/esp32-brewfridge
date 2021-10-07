#ifndef LCD_H
#define LCD_H

#include <hd44780.h>

extern hd44780_t lcd;
void lcd_init(void);
void lcd_reset(void);

#endif // LCD_H
