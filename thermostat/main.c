/* =========================================================
 * main.c  —  PIC16F18325 Thermostat
 * XC8 compiler, MPLAB X
 * ========================================================= */

#include "config.h"
#include "onewire.h"
#include "lcd_i2c.h"
#include "eeprom_float.h"
#include "uart.h"

#if NO_LCD
#  define lcd_init()            do { } while(0)
#  define lcd_backlight(x)      do { } while(0)
#  define lcd_set_cursor(x, y)  do { } while(0)
#  define lcd_print_str(s)      do { } while(0)
#  define lcd_print_char(c)     do { } while(0)
#endif

#if DEBUG_UART
#  define debug_puts(s)          uart_puts(s)
#  define debug_print_float(v,d) uart_print_float((v),(d))
#else
#  define debug_puts(s)          do { } while(0)
#  define debug_print_float(v,d) do { } while(0)
#endif

/* =========================================================
 * Timer1 — polled, no interrupt.
 * All globals zero-initialised (BSS) — no cross-page
 * init_ram fcall needed in XC8 startup.
 * ========================================================= */
volatile uint32_t ms_ticks;

static inline void poll_timer1(void)
{
    if (PIR1bits.TMR1IF) {
        TMR1H = TMR1_PRELOAD_H;
        TMR1L = TMR1_PRELOAD_L;
        PIR1bits.TMR1IF = 0;
        ms_ticks++;
    }
}

static inline uint32_t millis(void) { return ms_ticks; }

/* =========================================================
 * Sensor state machine
 * ========================================================= */
typedef enum {
    SENSOR_UNKNOWN = 0,
    SENSOR_OK,
    SENSOR_ERROR,
    SENSOR_LOST
} sensor_state_t;

/* =========================================================
 * Application state — all zero-initialised (BSS).
 * Non-zero defaults set at top of main().
 * ========================================================= */
static sensor_state_t sensor_state;
static bool     cutoffActive;
static bool     conversionRequested;
static uint32_t lastRequest;
static bool     relayOn;
static float    setpointC;
static float    lastTemp;
static uint32_t lastBtnTime;
static bool     btnUpArmed;
static bool     btnDownArmed;
static bool     firstRead;
static bool     firstReadDone;
static uint32_t startupTime;
static uint32_t lastFlash;
static bool     flashVisible;

/* =========================================================
 * fmt_temp()
 * ========================================================= */
static void fmt_temp(float val, char buf[6])
{
    int16_t t10;
    bool    neg;
    uint8_t tenths, units, tens;

    if (val >  999.9f) val =  999.9f;
    if (val < -99.9f)  val = -99.9f;

    t10 = (int16_t)(val * 10.0f + (val >= 0.0f ? 0.5f : -0.5f));
    neg = (t10 < 0);
    if (neg) t10 = (int16_t)(-t10);

    tenths = (uint8_t)((uint16_t)t10 % 10u);
    units  = (uint8_t)(((uint16_t)t10 / 10u) % 10u);
    tens   = (uint8_t)(((uint16_t)t10 / 100u) % 10u);

    buf[0] = neg  ? '-' : ' ';
    buf[1] = tens ? (char)('0' + tens) : ' ';
    buf[2] = (char)('0' + units);
    buf[3] = '.';
    buf[4] = (char)('0' + tenths);
    buf[5] = '\0';
}

/* =========================================================
 * set_relay()
 * ========================================================= */
static void set_relay(bool on)
{
    relayOn   = on;
    RELAY_LAT = on ? 1u : 0u;
}

/* =========================================================
 * update_lcd()
 * ========================================================= */
static void update_lcd(float tempC)
{
    char buf[6];

    lcd_set_cursor(0, 0);
    if (sensor_state == SENSOR_ERROR || sensor_state == SENSOR_LOST) {
        lcd_print_str(sensor_state == SENSOR_LOST ? "Sensor Lost!    "
                                                  : "No Sensor!      ");
    } else {
        fmt_temp(tempC, buf);
        lcd_print_str("Temp:");
        lcd_print_str(buf);
        lcd_print_char((char)0xDF);
        lcd_print_str("C    ");
    }

    lcd_set_cursor(0, 1);
    if (cutoffActive) {
        lcd_print_str(relayOn ? "CUTOFF        ON" : "CUTOFF       OFF");
    } else {
        fmt_temp(setpointC, buf);
        lcd_print_str("Set :");
        lcd_print_str(buf);
        lcd_print_char((char)0xDF);
        lcd_print_str("C ");
        lcd_print_str(relayOn ? " ON" : "OFF");
    }
}

/* =========================================================
 * hardware_init()
 * ========================================================= */
static void hardware_init(void)
{
    ANSELA = 0;      /* clear all analog selects before pin setup */
    ANSELC = 0;
    RELAY_ANSEL = 0; RELAY_TRIS = 0; RELAY_LAT = 0;
    BTN_UP_TRIS = 1; BTN_DOWN_TRIS = 1;
    BTN_UP_WPU  = 1; BTN_DOWN_WPU  = 1;
    ow_init();
    uart_init();
    T1CON = 0x01u;
    TMR1H = TMR1_PRELOAD_H;
    TMR1L = TMR1_PRELOAD_L;
}

/* =========================================================
 * apply_setpoint_change()
 * ========================================================= */
static void apply_setpoint_change(float delta)
{
    setpointC += delta;
    if (setpointC > SETPOINT_MAX) setpointC = SETPOINT_MAX;
    if (setpointC < SETPOINT_MIN) setpointC = SETPOINT_MIN;
    /* eeprom_write_float(EEPROM_ADDR, setpointC); */  /* WORKAROUND: write crashes, RAM-only for now */
    debug_puts(delta > 0.0f ? "Setpoint UP: " : "Setpoint DOWN: ");
    debug_print_float(setpointC, 1);
    debug_puts(" C\r\n");
    if (!firstRead) update_lcd(lastTemp);
}

/* =========================================================
 * main()
 * ========================================================= */
void main(void)
{
    float saved;

    ANSELAbits.ANSA0 = 0;
    TRISAbits.TRISA0 = 0;
    LATAbits.LATA0   = 1;   /* pin 13 HIGH = main() reached */

    ANSELCbits.ANSC3 = 0;
    TRISCbits.TRISC3 = 0;
    LATCbits.LATC3   = 0;

    hardware_init();

    LATCbits.LATC3 = 1;     /* pin 7 HIGH = hardware_init() done */

    /* Non-zero defaults (previously static initialisers) */
    btnUpArmed   = true;
    btnDownArmed = true;
    firstRead    = true;
    flashVisible = true;
    setpointC    = SETPOINT_DEFAULT;

    debug_puts("PIC16F18325 Thermostat starting\r\n");

    saved = eeprom_read_float(EEPROM_ADDR);
    if (saved >= SETPOINT_MIN && saved <= SETPOINT_MAX) {
        setpointC = saved;
    }
    debug_puts("Setpoint: ");
    debug_print_float(setpointC, 1);
    debug_puts(" C\r\n");

    lcd_init();
    lcd_backlight(true);
    lcd_set_cursor(0, 0);
    lcd_print_str("                ");
    lcd_set_cursor(0, 1);
    lcd_print_str(" Please wait... ");
    startupTime = millis();
    lastFlash   = startupTime;

    CLRWDT();

    while (1) {
        uint32_t now;
        bool     curBtnUp;
        bool     curBtnDown;

        CLRWDT();
        poll_timer1();

        now = millis();

        if (firstRead) {
            if (now - lastFlash >= FLASH_PERIOD_MS) {
                lastFlash    = now;
                flashVisible = !flashVisible;
                lcd_set_cursor(0, 1);
                lcd_print_str(flashVisible ? " Please wait... " : "                ");
            }
            if (firstReadDone && (now - startupTime >= STARTUP_MIN_MS)) {
                firstRead = false;
                update_lcd(lastTemp);
            }
        }

        /* --- Sensor / relay block --- */
        if (!conversionRequested) {
            if (now - lastRequest >= READ_INTERVAL) {
                lastRequest = now;
                if (ds18b20_start_conversion()) {
                    conversionRequested = true;
                } else {
                    sensor_state = SENSOR_ERROR;
                    set_relay(false);
                    if (!firstRead) update_lcd(lastTemp);
                    debug_puts("No sensor present\r\n");
                }
            }
        } else if (now - lastRequest >= CONVERSION_MS) {
            float tempC;
            conversionRequested = false;
            if (ds18b20_read_temp(&tempC)) {
                sensor_state = SENSOR_OK;
                lastTemp = tempC;
                if (!firstReadDone) {
                    firstReadDone = true;
                }
                /* Safety cutoff */
                if (tempC >= MAX_SAFE_TEMP) {
                    cutoffActive = true;
                    set_relay(false);
                } else if (cutoffActive) {
                    if (tempC < CUTOFF_RESET_TEMP) {
                        cutoffActive = false;
                    }
                }
                /* Normal relay control (heating) */
                if (!cutoffActive) {
                    if (tempC < setpointC - HYSTERESIS) {
                        set_relay(true);
                    } else if (tempC >= setpointC) {
                        set_relay(false);
                    }
                }
                debug_puts("Temp: ");
                debug_print_float(tempC, 1);
                debug_puts(" C\r\n");
                if (!firstRead) update_lcd(lastTemp);
            } else {
                sensor_state = SENSOR_LOST;
                set_relay(false);
                if (!firstRead) update_lcd(lastTemp);
                debug_puts("Sensor read failed\r\n");
            }
        }

        if (now - lastBtnTime >= BTN_DEBOUNCE_MS) {
            curBtnUp   = (BTN_UP_PORT  == 1u);
            curBtnDown = (BTN_DOWN_PORT == 1u);

            if (curBtnUp && !btnUpArmed) {
                btnUpArmed  = true;
                lastBtnTime = now;
            }
            if (curBtnDown && !btnDownArmed) {
                btnDownArmed = true;
                lastBtnTime  = now;
            }
            if (!curBtnUp && btnUpArmed) {
                btnUpArmed  = false;
                lastBtnTime = now;
                apply_setpoint_change(SETPOINT_STEP);
            } else if (!curBtnDown && btnDownArmed) {
                btnDownArmed = false;
                lastBtnTime  = now;
                apply_setpoint_change(-SETPOINT_STEP);
            }
        }

    } /* while(1) */
}
