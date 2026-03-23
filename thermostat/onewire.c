/* =========================================================
 * onewire.c  —  Bit-bang 1-Wire on RC2 + DS18B20 helpers
 * PIC16F18325 / XC8
 *
 * Bus idle state: RC2 configured as input; external 4.7 kΩ
 * pull-up to Vdd holds the line high.
 * To drive the line LOW: set TRIS=0, LAT=0.
 * To release  the line : set TRIS=1 (LAT stays 0).
 * ========================================================= */

#include "config.h"
#include "onewire.h"

/* ---------------------------------------------------------
 * Internal bus macros
 * --------------------------------------------------------- */
#define OW_DRIVE_LOW()  do { OW_LAT = 0; OW_TRIS = 0; } while(0)
#define OW_RELEASE()    do { OW_TRIS = 1; }              while(0)
#define OW_READ()       (OW_PORT)

/* ---------------------------------------------------------
 * CRC-8 (Dallas / Maxim)
 *   Polynomial : 0x31  (x^8 + x^5 + x^4 + 1), reflected form 0x8C
 *   Init value : 0x00
 *
 * Test vector (DS18B20 scratchpad example):
 *   Data : 0x50 0x05 0x4B 0x46 0x7F 0xFF 0x0C 0x10  (bytes 0–7)
 *   CRC  : 0x1C  (byte 8 of the scratchpad must equal this)
 *   i.e. crc8_update(0x00, data[0..7]) == 0x1C
 * --------------------------------------------------------- */
static uint8_t crc8_update(uint8_t crc, uint8_t data)
{
    uint8_t i;
    crc ^= data;
    for (i = 0; i < 8; i++) {
        if (crc & 0x01u) {
            crc = (uint8_t)((crc >> 1) ^ 0x8Cu);
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

/* =========================================================
 * ow_init
 * ========================================================= */
void ow_init(void)
{
    OW_ANSEL = 0;    /* disable analogue on RC2                 */
    OW_LAT   = 0;    /* pre-clear latch so DRIVE_LOW works      */
    OW_TRIS  = 1;    /* pin starts as input (bus idle / high)   */
}

/* =========================================================
 * ow_reset
 * Returns true if presence pulse detected.
 *
 * Timing (with interrupts disabled throughout):
 *   480 µs LOW  → release → 70 µs wait → sample → 410 µs wait
 * ========================================================= */
bool ow_reset(void)
{
    bool presence;

    di();

    OW_DRIVE_LOW();
    __delay_us(480);

    OW_RELEASE();
    __delay_us(70);

    presence = (OW_READ() == 0);   /* device pulls low → presence */

    __delay_us(410);

    return presence;
}

/* =========================================================
 * ow_write_byte  — LSB first
 * ========================================================= */
void ow_write_byte(uint8_t b)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (b & 0x01u) {
            /* Write '1' slot: 6 µs LOW, release, 64 µs recovery */
            di();
            OW_DRIVE_LOW();
            __delay_us(6);
            OW_RELEASE();
            __delay_us(64);
        } else {
            /* Write '0' slot: 60 µs LOW, release, 10 µs recovery */
            di();
            OW_DRIVE_LOW();
            __delay_us(60);
            OW_RELEASE();
            __delay_us(10);
        }
        b >>= 1;
    }
}

/* =========================================================
 * ow_read_byte  — LSB first
 *
 * Each read slot:
 *   6 µs LOW (initiate) → release → sample at ~9 µs →
 *   wait remainder to reach 70 µs total slot width.
 * ========================================================= */
uint8_t ow_read_byte(void)
{
    uint8_t b = 0;
    uint8_t i;

    for (i = 0; i < 8; i++) {
        b >>= 1;

        di();
        OW_DRIVE_LOW();
        __delay_us(6);
        OW_RELEASE();
        __delay_us(3);                /* 6+3 = 9 µs — sample window */
        NOP(); NOP();                 /* anchor sample point ≥ 9 µs from release */

        if (OW_READ()) {
            b |= 0x80u;              /* MSB position after >> above */
        }

        __delay_us(61);              /* 9 + 61 = 70 µs slot total   */
    }

    return b;
}

/* =========================================================
 * ds18b20_start_conversion
 * ========================================================= */
bool ds18b20_start_conversion(void)
{
    if (!ow_reset()) {
        return false;
    }
    ow_write_byte(0xCCu);   /* Skip ROM        */
    ow_write_byte(0x44u);   /* Convert T       */
    return true;
}

/* =========================================================
 * ds18b20_read_temp
 * ========================================================= */
bool ds18b20_read_temp(float *tempC)
{
    uint8_t  scratch[9];
    uint8_t  crc = 0;
    uint8_t  i;
    int16_t  raw;

    if (!ow_reset()) {
        return false;
    }

    ow_write_byte(0xCCu);   /* Skip ROM        */
    ow_write_byte(0xBEu);   /* Read Scratchpad */

    for (i = 0; i < 9u; i++) {
        scratch[i] = ow_read_byte();
    }

    /* CRC-8 over bytes 0..7; result must equal byte 8 */
    for (i = 0; i < 8u; i++) {
        crc = crc8_update(crc, scratch[i]);
    }
    if (crc != scratch[8]) {
        return false;             /* CRC mismatch */
    }

    /* Decode 12-bit two's-complement temperature
     * DS18B20 default config is 12-bit (bits 12..0 valid).
     * LSB = 0.0625 °C                                         */
    raw = (int16_t)(((uint16_t)scratch[1] << 8) | (uint16_t)scratch[0]);
    *tempC = (float)raw * 0.0625f;

    return true;
}
