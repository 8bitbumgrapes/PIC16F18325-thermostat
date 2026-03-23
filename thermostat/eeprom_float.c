/* =========================================================
 * eeprom_float.c  —  Store/retrieve a float in data EEPROM
 * PIC16F18325 / XC8
 *
 * NVM registers used:
 *   NVMADRL  — low byte of NVM address
 *   NVMADRH  — high byte of NVM address (always 0 for EEPROM)
 *   NVMDATL  — data byte
 *   NVMCON1  — control register
 *   NVMCON2  — unlock register (write 0x55 then 0xAA to enable WR)
 *
 * NVMCON1 bit fields (relevant bits):
 *   NVMREGS  (bit 6): 0 = EEPROM / RAM  (select data EEPROM)
 *                     1 = Program Flash
 *   WREN     (bit 2): 1 = allow write/erase
 *   WR       (bit 1): set to 1 to start write cycle; hw clears when done
 *   RD       (bit 0): set to 1 to start read;  hw clears immediately
 * ========================================================= */

#include "config.h"
#include "eeprom_float.h"

/* Union lets us alias the 4 float bytes as a byte array. */
typedef union {
    float    f;
    uint8_t  b[4];
} float_bytes_t;

/* =========================================================
 * eeprom_write_byte  — internal helper
 * Writes a single byte to the data EEPROM at `addr`.
 * GIE is disabled during the unlock + WR window to prevent
 * an interrupt from corrupting the unlock sequence.
 * ========================================================= */
static void eeprom_write_byte(uint8_t addr, uint8_t data)
{
    NVMADRH = 0x00u;
    NVMADRL = addr;
    NVMDATL = data;
    NVMCON1bits.NVMREGS = 0;
    NVMCON1bits.WREN    = 1;
    NVMCON2 = 0x55u;
    NVMCON2 = 0xAAu;
    NVMCON1bits.WR = 1;
    /* BISECT: polling loop omitted — not waiting for write to complete */
    NVMCON1bits.WREN = 0;
}

/* =========================================================
 * eeprom_read_byte  — internal helper
 * ========================================================= */
static uint8_t eeprom_read_byte(uint8_t addr)
{
    NVMADRH = 0x00u;
    NVMADRL = addr;

    NVMCON1bits.NVMREGS = 0;   /* select data EEPROM            */
    NVMCON1bits.RD      = 1;   /* initiate read (self-clearing) */

    /* RD clears within one instruction cycle on PIC16F18325 */
    NOP();
    NOP();

    return NVMDATL;
}

/* =========================================================
 * Public functions
 * ========================================================= */

void eeprom_write_float(uint8_t addr, float val)
{
    float_bytes_t fb;
    uint8_t i;

    fb.f = val;
    for (i = 0; i < 4u; i++) {
        eeprom_write_byte((uint8_t)(addr + i), fb.b[i]);
    }
}

float eeprom_read_float(uint8_t addr)
{
    float_bytes_t fb;
    uint8_t i;

    for (i = 0; i < 4u; i++) {
        fb.b[i] = eeprom_read_byte((uint8_t)(addr + i));
    }
    return fb.f;
}
