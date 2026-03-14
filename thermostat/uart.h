#ifndef UART_H
#define UART_H

/* =========================================================
 * uart.h  —  TX-only debug UART (EUSART1) on RA0
 * PIC16F18325 / XC8
 *
 * 115200 baud, 8N1, no flow control.
 * TX mapped to RA0 via PPS (RA0PPS = 0x10).
 * ========================================================= */

#include <stdint.h>

/**
 * uart_init() — Configure EUSART1 for 115200 baud @ 32 MHz.
 *   PPS:     RA0PPS  = 0x10  (TX1 output)
 *   Baud:    BAUD1CON |= 0x08 (BRG16 = 1)
 *            SP1BRGH  = 0
 *            SP1BRGL  = 68    (115200 baud, ~0.6% error)
 *   TX1STA   = 0x24  (BRGH=1, TXEN=1)
 *   RC1STA   = 0x90  (SPEN=1, CREN=1 — enables serial port)
 */
void uart_init(void);

/**
 * uart_putc() — Transmit one character.  Polls TX1STAbits.TRMT
 *               (transmit shift register empty) before writing
 *               the byte to TX1REG.
 */
void uart_putc(char c);

/**
 * uart_puts() — Transmit a null-terminated string.
 */
void uart_puts(const char *s);

/**
 * uart_puts_P() — Identical to uart_puts().  Provided for source
 *                 compatibility with AVR/Arduino code that uses
 *                 F() / PROGMEM strings.  On XC8 for PIC16 there
 *                 is no Harvard-architecture string separation, so
 *                 this is a straight alias.
 */
void uart_puts_P(const char *s);

/**
 * uart_print_float() — Print a float value with `decimals` digits
 *                       after the decimal point.  Uses integer
 *                       decomposition — no printf/sprintf.
 *
 * val      : value to print (supports negative values)
 * decimals : number of fractional digits (0–4 supported)
 */
void uart_print_float(float val, uint8_t decimals);

#endif /* UART_H */
