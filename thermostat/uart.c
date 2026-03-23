/* =========================================================
 * uart.c  —  TX-only debug UART (EUSART1) on RA0
 * PIC16F18325 / XC8
 *
 * Baud rate calculation (BRG16=1, BRGH=1, async):
 *   BRG = Fosc / (4 * Baud) - 1
 *       = 32 000 000 / (4 * 115200) - 1
 *       = 69.44 - 1  ≈  68  (0x44)
 *   Actual baud = 32 000 000 / (4 * (68+1)) = 115942 baud
 *   Error = (115942 - 115200) / 115200 ≈ +0.64%  (well within 2%)
 * ========================================================= */

#include "config.h"
#include "uart.h"

/* =========================================================
 * uart_init
 * ========================================================= */
void uart_init(void)
{
    /* RA0 = TX output, digital */
    UART_TX_ANSEL = 0;
    UART_TX_TRIS  = 0;

    /* Baud rate: BRG16=1 (16-bit BRG), BRGH=1 (high speed)
     * SP1BRGH:SP1BRGL = 68                           */
    BAUD1CON  = 0x08u;   /* BRG16 = 1, all others 0  */
    SP1BRGH   = 0x00u;
    SP1BRGL   = 68u;

    /* Enable serial port FIRST (SPEN=1), THEN enable transmitter (TXEN=1).
     * PIC16 EUSART: setting TXEN before SPEN latches TX line LOW.
     * CREN=0: receiver disabled (TX-only).
     * RC1STA: SPEN=1 only → 0x80 = 0b10000000                     */
    RC1STA = 0x80u;

    /* TX1STA: TXEN=1, BRGH=1, SYNC=0 (async)
     * Bit layout: CSRC TX9 TXEN SYNC SENDB BRGH TRMT TX9D
     *             0    0   1   0    0     1    x   0
     * 0x24 = 0b00100100                              */
    TX1STA = 0x24u;

    /* Map EUSART1 TX to RA0 via PPS — done LAST, after SPEN+TXEN are set.
     * A disabled peripheral PPS output drives LOW (0); mapping before
     * the peripheral is enabled latches the pin LOW permanently.
     * PPS output code 0x14 (20) = TX/CK  (from DFP edc:ppsval="20") */
    RA0PPS = 0x14u;
}

/* =========================================================
 * uart_putc
 * ========================================================= */
void uart_putc(char c)
{
    /* Wait until the transmit shift register is empty */
    while (!TX1STAbits.TRMT) {
        /* spin */
    }
    TX1REG = (uint8_t)c;
}

/* =========================================================
 * uart_puts
 * ========================================================= */
void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

/* =========================================================
 * uart_print_float
 *
 * Strategy:
 *   1. Handle sign.
 *   2. Scale to integer by multiplying by 10^decimals.
 *   3. Print integer part with leading digits.
 *   4. Print decimal point and fractional digits.
 *
 * Supports decimals = 0..4.
 * Uses a small local char array to reverse the digits.
 * ========================================================= */
void uart_print_float(float val, uint8_t decimals)
{
    char     buf[12];
    uint8_t  pos;
    uint8_t  i;
    uint32_t scale_val;
    uint32_t scaled;
    uint32_t intpart;
    uint32_t fracpart;
    uint32_t tmp;
    uint32_t d;
    uint8_t  frac_digits[4];

    pos = 0;

    /* Clamp decimals */
    if (decimals > 4u) {
        decimals = 4u;
    }

    /* Compute 10^decimals inline — avoids static const array in RAM
     * (XC8 free places const arrays via init_ram, not __flash) */
    scale_val = 1UL;
    for (i = 0u; i < decimals; i++) {
        scale_val *= 10UL;
    }

    /* Clamp value — uint32_t scaled overflows above ~429,496 °C */
    if (val >  9999.9f) val =  9999.9f;
    if (val < -9999.9f) val = -9999.9f;

    /* Sign */
    if (val < 0.0f) {
        uart_putc('-');
        val = -val;
    }

    /* Round before splitting */
    val += 0.5f / (float)scale_val;

    scaled   = (uint32_t)(val * (float)scale_val);
    intpart  = scaled / scale_val;
    fracpart = scaled % scale_val;

    /* Build integer part string (reversed into buf) */
    if (intpart == 0UL) {
        buf[pos++] = '0';
    } else {
        tmp = intpart;
        while (tmp > 0UL) {
            buf[pos++] = (char)('0' + (uint8_t)(tmp % 10UL));
            tmp /= 10UL;
        }
    }
    /* Reverse and print integer part */
    for (i = pos; i > 0u; i--) {
        uart_putc(buf[i - 1u]);
    }

    /* Fractional part */
    if (decimals > 0u) {
        uart_putc('.');
        /* Extract digits MSB-first: d steps down from 10^(decimals-1) to 1 */
        tmp = fracpart;
        d   = scale_val / 10UL;
        for (i = 0u; i < decimals; i++) {
            frac_digits[i] = (uint8_t)(tmp / d);
            tmp %= d;
            if (d >= 10UL) { d /= 10UL; } else { d = 1UL; }
        }
        for (i = 0u; i < decimals; i++) {
            uart_putc((char)('0' + frac_digits[i]));
        }
    }
}
