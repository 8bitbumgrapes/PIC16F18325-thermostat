#ifndef CONFIG_H
#define CONFIG_H

/* =========================================================
 * config.h  —  PIC16F18325 Thermostat
 * XC8 compiler, MPLAB X
 * ========================================================= */

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------
 * Oscillator / clock
 * --------------------------------------------------------- */
#define _XTAL_FREQ  32000000UL   /* 32 MHz (8 MHz INTOSC + 4x PLL) */

/* ---------------------------------------------------------
 * Configuration words (PIC16F18325)
 * --------------------------------------------------------- */

/* CONFIG1 */
#pragma config FEXTOSC  = OFF       /* External oscillator not used         */
#pragma config RSTOSC   = HFINT32   /* HFINTOSC with OSCFRQ = 32 MHz        */
#pragma config CLKOUTEN = OFF       /* CLKOUT pin disabled                  */
#pragma config CSWEN    = ON        /* Clock switch enabled                 */
#pragma config FCMEN    = ON        /* Fail-Safe Clock Monitor enabled      */

/* CONFIG2 */
#pragma config MCLRE    = OFF       /* MCLR pin is GPIO (RA3 = input only)  */
#pragma config PWRTE    = ON        /* Power-up timer enabled               */
#pragma config WDTE     = ON        /* WDT enabled (hardware)               */
#pragma config LPBOREN  = OFF       /* Low-Power BOR disabled               */
#pragma config BOREN    = ON        /* Brown-out reset enabled              */
#pragma config BORV     = LO        /* BOR voltage: low trip point          */
#pragma config ZCD      = OFF       /* ZCD disabled                         */
#pragma config PPS1WAY  = OFF       /* PPS can be changed multiple times    */
#pragma config STVREN   = ON        /* Stack overflow/underflow reset        */

/* CONFIG3 — WDT period ≈ 4.7 s (WDTCPS_13 = 4096 * 1.152 ms) */
#pragma config WDTCPS   = WDTCPS_13 /* WDT period ~4.7 s                   */
#pragma config WDTCWS   = WDTCWS_7  /* WDT window: always open (100%)       */
#pragma config WDTCAS   = WDTCAS_1  /* WDT always active (not gated)        */

/* CONFIG4 */
#pragma config WRT      = OFF       /* Flash write protection off           */
#pragma config SCANE    = AVAILABLE /* Scanner available                    */
#pragma config LVP      = ON        /* Low-voltage programming enabled      */

/* CONFIG5 */
#pragma config CP       = OFF       /* Code protection off                  */

/* ---------------------------------------------------------
 * Thermostat parameters  (match Arduino source exactly)
 * --------------------------------------------------------- */
#define SETPOINT_DEFAULT  24.0f
#define SETPOINT_STEP     0.5f
#define SETPOINT_MIN      5.0f
#define SETPOINT_MAX      29.0f
#define HYSTERESIS        1.0f
#define MAX_SAFE_TEMP     30.0f
#define READ_INTERVAL     1000UL    /* ms between temperature read cycles   */
#define CONVERSION_MS     750UL     /* DS18B20 conversion time (12-bit)     */
#define BTN_DEBOUNCE_MS   50UL      /* Button debounce period               */
#define STARTUP_MIN_MS    2000UL    /* Minimum startup screen display time  */
#define FLASH_PERIOD_MS   500UL     /* Please-wait flash toggle period      */

/* Safety cutoff hysteresis — relay re-enables below this temperature.
 * Prevents relay chatter when sensor reads hover near MAX_SAFE_TEMP. */
#define CUTOFF_RESET_TEMP 29.0f    /* °C: cutoff clears below this value   */

/* Timer1 preload for 1 ms overflow at 32 MHz (Fosc/4 = 8 MHz instruction clock).
 * Count = 65536 - (8 000 000 / 1000) = 65536 - 8000 = 57536 = 0xE0C0        */
#define TMR1_PRELOAD_H    0xE0u
#define TMR1_PRELOAD_L    0xC0u

/* Set to 0 to build a silent production binary (no UART output). */
#define DEBUG_UART        1

/* EEPROM base address for setpoint float (4 bytes: 0x00 – 0x03) */
#define EEPROM_ADDR       0x00

/* LCD I2C address */
#define LCD_ADDRESS       0x27

/* ---------------------------------------------------------
 * Pin macros — Port C
 *   RC0 = SCL  (MSSP1, hardware)
 *   RC1 = SDA  (MSSP1, hardware)
 *   RC2 = OneWire DQ  (bit-bang)
 *   RC3 = Relay output (active HIGH)
 *   RC4 = BTN_UP   (input, WPU)
 *   RC5 = BTN_DOWN (input, WPU)
 *
 * Port A
 *   RA0 = UART TX (via PPS)
 *   RA3 = MCLR/input-only — leave unconnected
 * --------------------------------------------------------- */

/* OneWire on RC2 */
#define OW_TRIS     TRISCbits.TRISC2
#define OW_LAT      LATCbits.LATC2
#define OW_PORT     PORTCbits.RC2
#define OW_ANSEL    ANSELCbits.ANSC2

/* Relay on RC3 */
#define RELAY_TRIS  TRISCbits.TRISC3
#define RELAY_LAT   LATCbits.LATC3
#define RELAY_ANSEL ANSELCbits.ANSC3

/* Buttons on RC4, RC5 */
#define BTN_UP_TRIS   TRISCbits.TRISC4
#define BTN_UP_PORT   PORTCbits.RC4
#define BTN_UP_WPU    WPUCbits.WPUC4
/* RC4 is digital-only — no ANSELCbits.ANSC4 exists on PIC16F18325 */

#define BTN_DOWN_TRIS  TRISCbits.TRISC5
#define BTN_DOWN_PORT  PORTCbits.RC5
#define BTN_DOWN_WPU   WPUCbits.WPUC5
/* RC5 is digital-only — no ANSELCbits.ANSC5 exists on PIC16F18325 */

/* UART TX on RA0 */
#define UART_TX_TRIS  TRISAbits.TRISA0
#define UART_TX_ANSEL ANSELAbits.ANSA0

#endif /* CONFIG_H */
