#ifndef EEPROM_FLOAT_H
#define EEPROM_FLOAT_H

/* =========================================================
 * eeprom_float.h  —  Store/retrieve a float in data EEPROM
 * PIC16F18325 / XC8
 *
 * A float is 4 bytes (IEEE-754 single precision).  The bytes
 * are written/read sequentially starting at the given address.
 * Addresses 0x00 – 0x03 are used for the setpoint float with
 * EEPROM_ADDR = 0x00.
 * ========================================================= */

#include <stdint.h>

/**
 * eeprom_write_float() — Write a 4-byte float to EEPROM starting
 *                         at `addr`.  Each byte uses the NVM unlock
 *                         sequence (0x55 / 0xAA) with GIE disabled
 *                         during the critical window.
 *
 * addr : starting EEPROM byte address (0x00 – 0xFB for 256-byte space,
 *        but only 0x00–0xFF physically available on this device).
 * val  : value to store.
 */
void  eeprom_write_float(uint8_t addr, float val);

/**
 * eeprom_read_float() — Read a 4-byte float from EEPROM starting
 *                        at `addr`.
 *
 * addr : starting EEPROM byte address (same convention as write).
 * Returns the reconstructed float value.
 */
float eeprom_read_float(uint8_t addr);

#endif /* EEPROM_FLOAT_H */
