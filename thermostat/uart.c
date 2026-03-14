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

    /* Map EUSART1 TX to RA0 via PPS
     * PPS output code 0x10 = TX1 / CK1              */
    RA0PPS = 0x10u;

    /* Baud rate: BRG16=1 (16-bit BRG), BRGH=1 (high speed)
     * SP1BRGH:SP1BRGL = 68                           */
    BAUD1CON  = 0x08u;   /* BRG16 = 1, all others 0  */
    SP1BRGH   = 0x00u;
    SP1BRGL   = 68u;

    /* TX1STA: TXEN=1, BRGH=1, SYNC=0 (async)
     * Bit layout: CSRC TX9 TXEN SYNC SENDB BRGH TRMT TX9D
     *             0    0   1   0    0     1    x   0
     * 0x24 = 0b00100100                              */
    TX1STA = 0x24u;

    /* RC1STA: SPEN=1, CREN=1 (enables the serial port)
     * Bit layout: SPEN RX9 SREN CREN ADDEN FERR OERR RX9D
     *             1    0   0   1    0     0    0   0
     * 0x90 = 0b10010000                              */
    RC1STA = 0x90u;
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
    /* Scale factors for 0..4 decimal places */
    static const uint32_t scale[5] = { 1UL, 10UL, 100UL, 1000UL, 10000UL };

    char     buf[12];
    uint8_t  pos;
    uint8_t  i;
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

    /* Clamp value — uint32_t scaled overflows above ~429,496 °C */
    if (val >  9999.9f) val =  9999.9f;
    if (val < -9999.9f) val = -9999.9f;

    /* Sign */
    if (val < 0.0f) {
        uart_putc('-');
        val = -val;
    }

    /* Round before splitting */
    val += 0.5f / (float)scale[decimals];

    scaled   = (uint32_t)(val * (float)scale[decimals]);
    intpart  = scaled / scale[decimals];
    fracpart = scaled % scale[decimals];

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
        /* Extract digits MSB-first into frac_digits[] */
        tmp = fracpart;
        for (i = decimals; i > 0u; i--) {
            d = scale[i - 1u];
            frac_digits[decimals - i] = (uint8_t)(tmp / d);
            tmp %= d;
        }
        for (i = 0u; i < decimals; i++) {
            uart_putc((char)('0' + frac_digits[i]));
        }
    }
}
