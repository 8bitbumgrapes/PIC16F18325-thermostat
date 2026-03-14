/* =========================================================
 * lcd_i2c.c  —  HD44780 16×2 LCD via PCF8574 I2C backpack
 * Hardware MSSP1 (RC0=SCL, RC1=SDA), 100 kHz, I2C addr 0x27
 * PIC16F18325 / XC8
 *
 * PCF8574 pin mapping
 *   P0 = RS   (register select: 0=command, 1=data)
 *   P1 = RW   (always 0 — write mode)
 *   P2 = E    (enable pulse)
 *   P3 = BL   (backlight)
 *   P4 = D4
 *   P5 = D5
 *   P6 = D6
 *   P7 = D7
 * ========================================================= */

#include "config.h"
#include "lcd_i2c.h"

/* ---------------------------------------------------------
 * PCF8574 bit masks
 * --------------------------------------------------------- */
#define PCF_RS  0x01u
#define PCF_RW  0x02u   /* always 0 */
#define PCF_E   0x04u
#define PCF_BL  0x08u

/* ---------------------------------------------------------
 * Module state
 * --------------------------------------------------------- */
static uint8_t s_backlight = PCF_BL;   /* default: on */

/* =========================================================
 * Internal I2C helpers (MSSP1 polling, no interrupts)
 * ========================================================= */

/**
 * i2c_init() — Configure MSSP1 for I2C master, 100 kHz at 32 MHz Fosc.
 *
 * Baud rate: SSP1ADD = (Fosc / (4 * Fscl)) - 1
 *            = (32 000 000 / (4 * 100 000)) - 1
 *            = 80 - 1 = 79  (0x4F)
 *
 * RC0 and RC1 must be configured as inputs with open-drain outputs
 * (the MSSP peripheral controls the lines).
 */
static void i2c_init(void)
{
    /* Pins: input, digital, no pull-up needed (external 4k7) */
    TRISCbits.TRISC0  = 1;
    TRISCbits.TRISC1  = 1;
    ANSELCbits.ANSC0  = 0;
    ANSELCbits.ANSC1  = 0;

    SSP1STAT = 0x80u;   /* SMP=1 (slew rate disabled for 100 kHz)  */
    SSP1ADD  = 79u;     /* Baud rate divider for 100 kHz @ 32 MHz   */
    SSP1CON1 = 0x28u;   /* SSPEN=1, SSPM=1000 (I2C master mode)    */
    SSP1CON2 = 0x00u;
    SSP1CON3 = 0x00u;
}

/** Wait for the MSSP1 interrupt flag (operation complete). */
static void i2c_wait(void)
{
    while (!PIR1bits.SSP1IF) {
        /* spin */
    }
    PIR1bits.SSP1IF = 0;
}

/** Wait until the bus is idle (no pending operations). */
static void i2c_idle(void)
{
    while ((SSP1CON2 & 0x1Fu) || SSP1STATbits.R_nW) {
        /* spin while SEN/RSEN/PEN/RCEN/ACKEN or transmit in progress */
    }
}

static void i2c_start(void)
{
    i2c_idle();
    SSP1CON2bits.SEN = 1;   /* initiate START condition */
    i2c_wait();
}

static void i2c_stop(void)
{
    i2c_idle();
    SSP1CON2bits.PEN = 1;   /* initiate STOP condition  */
    i2c_wait();
}

/**
 * i2c_write_byte() — Send one byte.  Returns true if ACK received.
 */
static bool i2c_write_byte(uint8_t data)
{
    i2c_idle();
    SSP1BUF = data;
    i2c_wait();
    return (SSP1CON2bits.ACKSTAT == 0);   /* 0 = ACK */
}

/* =========================================================
 * Internal LCD helpers
 * ========================================================= */

/**
 * pcf_write() — Send one byte directly to the PCF8574 expander.
 */
static void pcf_write(uint8_t val)
{
    i2c_start();
    i2c_write_byte((uint8_t)(LCD_ADDRESS << 1));   /* address + write bit */
    i2c_write_byte(val);
    i2c_stop();
}

/**
 * lcd_send_nibble() — Put a 4-bit nibble on D7..D4, pulse E.
 *   nibble : upper 4 bits to send (placed into bits P7..P4)
 *   rs     : 1 for data register, 0 for command register
 */
static void lcd_send_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t base = (uint8_t)((nibble & 0x0Fu) << 4)
                 | s_backlight
                 | (rs ? PCF_RS : 0u);

    pcf_write((uint8_t)(base | PCF_E));   /* E high */
    __delay_us(1);
    pcf_write(base);                       /* E low  */
    __delay_us(50);                        /* execution time ≥ 37 µs */
}

/**
 * lcd_send_byte() — Send a full byte as two nibbles (high then low).
 *   rs = 0 : command
 *   rs = 1 : data (character)
 */
static void lcd_send_byte(uint8_t b, uint8_t rs)
{
    lcd_send_nibble((uint8_t)(b >> 4),   rs);
    lcd_send_nibble((uint8_t)(b & 0x0Fu), rs);
}

/* =========================================================
 * Public API
 * ========================================================= */

void lcd_init(void)
{
    i2c_init();

    /* HD44780 requires ≥ 40 ms after Vcc rises above 2.7 V */
    __delay_ms(50);

    /* --- 4-bit initialisation sequence (HD44780 datasheet) --- */

    /* Three times: send 0x3 nibble in 8-bit fashion             */
    lcd_send_nibble(0x03u, 0);
    __delay_ms(5);                 /* ≥ 4.1 ms                   */

    lcd_send_nibble(0x03u, 0);
    __delay_us(150);               /* ≥ 100 µs                   */

    lcd_send_nibble(0x03u, 0);
    __delay_us(150);

    /* Now switch to 4-bit mode */
    lcd_send_nibble(0x02u, 0);
    __delay_us(150);

    /* Function Set: 4-bit, 2 lines, 5×8 font  (0x28) */
    lcd_send_byte(0x28u, 0);
    __delay_us(50);

    /* Display OFF */
    lcd_send_byte(0x08u, 0);
    __delay_us(50);

    /* Clear Display */
    lcd_send_byte(0x01u, 0);
    __delay_ms(2);                 /* clear needs ≥ 1.52 ms      */

    /* Entry Mode Set: increment cursor, no display shift (0x06) */
    lcd_send_byte(0x06u, 0);
    __delay_us(50);

    /* Display ON, cursor OFF, blink OFF (0x0C) */
    lcd_send_byte(0x0Cu, 0);
    __delay_us(50);

    /* Turn backlight on */
    s_backlight = PCF_BL;
    pcf_write(s_backlight);        /* latch BL bit with no E pulse */
}

void lcd_backlight(bool on)
{
    s_backlight = on ? PCF_BL : 0u;
    pcf_write(s_backlight);
}

void lcd_set_cursor(uint8_t col, uint8_t row)
{
    /* Row 0 → DDRAM base 0x00; Row 1 → DDRAM base 0x40 */
    uint8_t addr = (uint8_t)(col + (row ? 0x40u : 0x00u));
    lcd_send_byte((uint8_t)(0x80u | addr), 0);
    __delay_us(50);
}

void lcd_print_char(char c)
{
    lcd_send_byte((uint8_t)c, 1);
}

void lcd_print_str(const char *s)
{
    while (*s) {
        lcd_print_char(*s++);
    }
}

void lcd_clear(void)
{
    lcd_send_byte(0x01u, 0);
    __delay_ms(2);
}
