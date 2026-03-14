#ifndef LCD_I2C_H
#define LCD_I2C_H

/* =========================================================
 * lcd_i2c.h  —  HD44780 16×2 LCD via PCF8574 I2C backpack
 * Hardware MSSP1 (RC0=SCL, RC1=SDA), I2C address 0x27
 * PIC16F18325 / XC8
 *
 * PCF8574 bit layout (P7..P0):
 *   P7 P6 P5 P4  P3  P2 P1 P0
 *   D7 D6 D5 D4  BL   E RW RS
 *   RW is always 0 (write mode)
 * ========================================================= */

#include <stdint.h>
#include <stdbool.h>

/**
 * lcd_init() — Initialise MSSP1 for 100 kHz I2C, then perform the
 *              full HD44780 4-bit initialisation sequence including
 *              function-set (2-line, 5×8 font), display on, clear,
 *              and entry mode set (increment, no shift).
 *              Backlight is turned on at the end of init.
 */
void lcd_init(void);

/**
 * lcd_backlight() — Turn the PCF8574 backlight bit on or off.
 *                   State is latched in a module-level variable and
 *                   included in every subsequent I2C write.
 */
void lcd_backlight(bool on);

/**
 * lcd_set_cursor() — Move the cursor to (col, row).
 *                    col : 0–15, row : 0 or 1.
 */
void lcd_set_cursor(uint8_t col, uint8_t row);

/**
 * lcd_print_str() — Write a null-terminated string to the display
 *                   at the current cursor position.
 */
void lcd_print_str(const char *s);

/**
 * lcd_print_char() — Write a single character to the display at
 *                    the current cursor position.
 */
void lcd_print_char(char c);

/**
 * lcd_clear() — Send the HD44780 clear-display command (0x01) and
 *               wait the required 2 ms execution time.
 */
void lcd_clear(void);

#endif /* LCD_I2C_H */
