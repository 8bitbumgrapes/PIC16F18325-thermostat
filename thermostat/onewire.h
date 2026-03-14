#ifndef ONEWIRE_H
#define ONEWIRE_H

/* =========================================================
 * onewire.h  —  Bit-bang 1-Wire on RC2 + DS18B20 helpers
 * PIC16F18325 / XC8
 * ========================================================= */

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------
 * Low-level 1-Wire bus primitives
 * --------------------------------------------------------- */

/**
 * ow_init() — Configure RC2 as a floating input (bus idle high via
 *             external pull-up).  Disables the analogue function on
 *             the pin so the digital input reads correctly.
 */
void    ow_init(void);

/**
 * ow_reset() — Issue a 1-Wire reset and detect the presence pulse.
 * Returns true  when at least one device pulls the bus low during
 *               the presence window.
 * Returns false when the bus stays high (no devices present).
 *
 * Timing (all with interrupts disabled):
 *   Drive LOW for 480 µs, release, wait 70 µs, sample, wait 410 µs.
 */
bool    ow_reset(void);

/**
 * ow_write_byte() — Write one byte, LSB first.
 * '1' slot: drive LOW 6 µs, release, wait 64 µs (total 70 µs)
 * '0' slot: drive LOW 60 µs, release, wait 10 µs (total 70 µs)
 */
void    ow_write_byte(uint8_t b);

/**
 * ow_read_byte() — Read one byte, LSB first.
 * Each bit: drive LOW 6 µs, release, sample at ~9 µs, wait remainder
 *           to complete a 70 µs slot.
 */
uint8_t ow_read_byte(void);

/* ---------------------------------------------------------
 * DS18B20 helpers
 * --------------------------------------------------------- */

/**
 * ds18b20_start_conversion() — Reset, Send 0xCC (Skip ROM),
 *                               Send 0x44 (Convert T).
 * Returns true  if presence pulse was detected.
 * Returns false if no device on the bus.
 */
bool    ds18b20_start_conversion(void);

/**
 * ds18b20_read_temp() — Reset, 0xCC, 0xBE, read 9 scratchpad bytes,
 *                        verify CRC-8 (poly 0x31, init 0x00) over
 *                        the first 8 bytes, decode 12-bit result.
 *
 * Stores the decoded temperature in *tempC.
 * Returns true  on success (CRC OK, presence detected).
 * Returns false on error  (no presence, CRC fail).
 *
 * Resolution: 12-bit → 0.0625 °C per LSB.
 * Valid range: -55 °C … +125 °C.
 */
bool    ds18b20_read_temp(float *tempC);

#endif /* ONEWIRE_H */
