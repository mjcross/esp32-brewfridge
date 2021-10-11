#ifndef LCD_H
#define LCD_H

#include <stdbool.h>
#include <hd44780.h>

void lcd_init(void);
void lcd_reset(void);
void lcd_clear(void);
void lcd_gotoxy(int, int);
void lcd_puts(const char *);
void lcd_putc(const char);
void lcd_switch_backlight(bool);
void lcd_hide(int, int, int);
void lcd_restore(void);
void lcd_dump(void);

#endif // LCD_H
