/* =========================================================
 * eeprom_float.c  —  Store/retrieve a float in data EEPROM
 * PIC16F18325 / XC8
 *
 * NVM registers used:
 *   NVMADRL  — low byte of NVM address
 *   NVMADRH  — high byte of NVM address (0x70 for data EEPROM at 0x7000)
 *   NVMDATL  — data byte
 *   NVMCON1  — control register
 *   NVMCON2  — unlock register (write 0x55 then 0xAA to enable WR)
 *
 * NVMCON1 bit fields (relevant bits):
 *   NVMREGS  (bit 6): 0 = Program Flash  (NVMREGS=0 targets flash; 0x0000 = reset vector)
 *                     1 = Data EEPROM    (EEPROM mapped at NVM address 0x7000–0x70FF)
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
    NVMADRH = 0x70u;             /* EEPROM mapped at 0x7000 in NVM space   */
    NVMADRL = addr;
    NVMDATL = data;
    NVMCON1bits.NVMREGS = 1;             /* 1 = data EEPROM (0 = flash)     */
    NVMCON1bits.WREN    = 1;
    /* Single asm block: compiler cannot insert instructions inside it.      *
     * Separate asm() calls allow BSR-restore code between them — fatal.    *
     * INTCON (0x0B) is an all-banks register; no MOVLB needed for BCF GIE. *
     * Bank 17 (0x880): NVMCON1 offset 0x15, NVMCON2 offset 0x16.           */
    asm("BCF 0x0B,7\n"           /* GIE = 0 — disable interrupts            */
        "MOVLB 17\n"             /* BSR = 17 — select NVM register bank      */
        "MOVLW 0x55\n"
        "MOVWF 0x16\n"           /* NVMCON2 = 0x55 — unlock step 1           */
        "MOVLW 0xAA\n"
        "MOVWF 0x16\n"           /* NVMCON2 = 0xAA — unlock step 2           */
        "BSF 0x15,1");           /* NVMCON1.WR = 1 — start write (~2 ms)     */
    while (NVMCON1bits.WR) {     /* wait for hardware to clear WR          */
        NOP();
    }
    NVMCON1bits.WREN = 0;
}

/* =========================================================
 * eeprom_read_byte  — internal helper
 * ========================================================= */
static uint8_t eeprom_read_byte(uint8_t addr)
{
    NVMADRH = 0x70u;             /* EEPROM mapped at 0x7000 in NVM space   */
    NVMADRL = addr;

    NVMCON1bits.NVMREGS = 1;   /* 1 = data EEPROM (0 = flash)   */
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
