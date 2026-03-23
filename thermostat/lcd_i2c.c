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
#include "uart.h"

/* ---------------------------------------------------------
 * PCF8574 bit masks
 * --------------------------------------------------------- */
#define PCF_RS  0x01u
#define PCF_RW  0x02u   /* always 0 */
#define PCF_E   0x04u
#define PCF_BL  0x08u

/* ---------------------------------------------------------
 * HD44780 timing constants (all sourced from datasheet)
 * --------------------------------------------------------- */
#define LCD_POWERUP_MS    50u   /* ≥ 40 ms after Vcc > 2.7 V                  */
#define LCD_INIT1_MS       5u   /* ≥ 4.1 ms after first function-set nibble   */
#define LCD_INIT2_US     150u   /* ≥ 100 µs after subsequent function sets    */
#define LCD_E_PULSE_US     1u   /* E pulse width ≥ 450 ns                     */
#define LCD_EXEC_US       50u   /* Standard command execution ≥ 37 µs         */
#define LCD_CLEAR_MS       2u   /* Clear / Return-home execution ≥ 1.52 ms    */

/* ---------------------------------------------------------
 * Module state
 * --------------------------------------------------------- */
static uint8_t s_backlight = PCF_BL;   /* default: on */

/* =========================================================
 * Internal I2C helpers — bit-bang, RC0=SCL, RC1=SDA.
 *
 * Open-drain emulation (same approach as 1-Wire):
 *   Drive LOW  : LAT=0, TRIS=0  (output, driven low)
 *   Release    : TRIS=1         (input, external pullup pulls HIGH)
 *
 * 100 kHz: half-period = 5 µs.
 * ========================================================= */
#define SCL_LOW()   do { LATCbits.LATC0 = 0; TRISCbits.TRISC0 = 0; } while(0)
#define SCL_HIGH()  do { TRISCbits.TRISC0 = 1; } while(0)
#define SDA_LOW()   do { LATCbits.LATC1 = 0; TRISCbits.TRISC1 = 0; } while(0)
#define SDA_HIGH()  do { TRISCbits.TRISC1 = 1; } while(0)
#define SDA_READ()  (PORTCbits.RC1)

#define I2C_HALF_US 5u

static void i2c_init(void)
{
    ANSELCbits.ANSC0 = 0;
    ANSELCbits.ANSC1 = 0;
    LATCbits.LATC0   = 0;   /* pre-clear latches */
    LATCbits.LATC1   = 0;
    SCL_HIGH();              /* bus idle */
    SDA_HIGH();
}

static void i2c_start(void)
{
    SDA_HIGH(); __delay_us(I2C_HALF_US);
    SCL_HIGH(); __delay_us(I2C_HALF_US);
    SDA_LOW();  __delay_us(I2C_HALF_US);   /* SDA falls while SCL high */
    SCL_LOW();  __delay_us(I2C_HALF_US);
}

static void i2c_stop(void)
{
    SDA_LOW();  __delay_us(I2C_HALF_US);
    SCL_HIGH(); __delay_us(I2C_HALF_US);
    SDA_HIGH(); __delay_us(I2C_HALF_US);   /* SDA rises while SCL high */
}

/**
 * i2c_write_byte() — Send one byte MSB-first.  Returns true if ACK received.
 */
static bool i2c_write_byte(uint8_t data)
{
    uint8_t i;
    bool    ack;
    for (i = 0; i < 8u; i++) {
        if (data & 0x80u) { SDA_HIGH(); } else { SDA_LOW(); }
        __delay_us(I2C_HALF_US);
        SCL_HIGH();
        __delay_us(I2C_HALF_US);
        SCL_LOW();
        data <<= 1;
    }
    SDA_HIGH();                         /* release SDA for ACK bit  */
    __delay_us(I2C_HALF_US);
    SCL_HIGH();
    __delay_us(I2C_HALF_US);
    ack = (SDA_READ() == 0);            /* device pulls SDA low = ACK */
    SCL_LOW();
    return ack;
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
    if (!i2c_write_byte((uint8_t)(LCD_ADDRESS << 1))) {
        uart_puts("I2C NACK addr\r\n");
    }
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
    __delay_us(LCD_E_PULSE_US);
    pcf_write(base);                       /* E low  */
    __delay_us(LCD_EXEC_US);              /* execution time ≥ 37 µs */
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
    __delay_ms(LCD_POWERUP_MS);

    /* --- 4-bit initialisation sequence (HD44780 datasheet) --- */

    /* Three times: send 0x3 nibble in 8-bit fashion             */
    lcd_send_nibble(0x03u, 0);
    __delay_ms(LCD_INIT1_MS);              /* ≥ 4.1 ms           */

    lcd_send_nibble(0x03u, 0);
    __delay_us(LCD_INIT2_US);             /* ≥ 100 µs            */

    lcd_send_nibble(0x03u, 0);
    __delay_us(LCD_INIT2_US);

    /* Now switch to 4-bit mode */
    lcd_send_nibble(0x02u, 0);
    __delay_us(LCD_INIT2_US);

    /* Function Set: 4-bit, 2 lines, 5×8 font  (0x28) */
    lcd_send_byte(0x28u, 0);
    __delay_us(LCD_EXEC_US);

    /* Display OFF */
    lcd_send_byte(0x08u, 0);
    __delay_us(LCD_EXEC_US);

    /* Clear Display */
    lcd_send_byte(0x01u, 0);
    __delay_ms(LCD_CLEAR_MS);             /* clear needs ≥ 1.52 ms */

    /* Entry Mode Set: increment cursor, no display shift (0x06) */
    lcd_send_byte(0x06u, 0);
    __delay_us(LCD_EXEC_US);

    /* Display ON, cursor OFF, blink OFF (0x0C) */
    lcd_send_byte(0x0Cu, 0);
    __delay_us(LCD_EXEC_US);

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
    __delay_us(LCD_EXEC_US);
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
    __delay_ms(LCD_CLEAR_MS);
}
