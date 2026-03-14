/* =========================================================
 * main.c  —  PIC16F18325 Thermostat
 * XC8 compiler, MPLAB X
 *
 * Faithful port of the Arduino sketch.  All behaviour is
 * preserved:
 *   • Asynchronous DS18B20 temperature conversion
 *   • Hysteresis-based relay control
 *   • Safety cutoff above MAX_SAFE_TEMP
 *   • Button debounce with arm / re-arm
 *   • EEPROM setpoint persistence
 *   • 16×2 I2C LCD status display
 *   • WDT reset every main-loop pass
 *   • Debug output via UART at 115200 baud
 * ========================================================= */

#include "config.h"
#include "onewire.h"
#include "lcd_i2c.h"
#include "eeprom_float.h"
#include "uart.h"

/* =========================================================
 * Timer1 — 1 ms tick counter
 *
 * Fosc/4 = 8 MHz instruction clock
 * Prescaler 1:1 → timer increments at 8 MHz
 * Preload  : 65536 - 8000 = 57536  (0xE0C0)
 * Overflow : every 8000 counts = 1 ms exactly
 * ========================================================= */
volatile uint32_t ms_ticks = 0;

void timer1_init(void)
{
    T1CON  = 0x01u;      /* TMR1ON=1, prescaler 1:1, Fosc/4, sync */
    TMR1H  = 0xE0u;      /* preload high byte                      */
    TMR1L  = 0xC0u;      /* preload low  byte  (0xE0C0 = 57536)    */
    PIE1bits.TMR1IE = 1; /* enable Timer1 overflow interrupt        */
}

/* ---------------------------------------------------------
 * Interrupt Service Routine
 * Only Timer1 overflow is used; no other interrupts are
 * enabled.
 * --------------------------------------------------------- */
void __interrupt() isr(void)
{
    if (PIR1bits.TMR1IF) {
        /* Reload preload value — must be done first to minimise jitter */
        TMR1H = 0xE0u;
        TMR1L = 0xC0u;
        PIR1bits.TMR1IF = 0;
        ms_ticks++;
    }
}

/* ---------------------------------------------------------
 * millis() — atomic read of ms_ticks (disables interrupts
 *            briefly to avoid a torn 4-byte read on an 8-bit
 *            CPU).
 * --------------------------------------------------------- */
static inline uint32_t millis(void)
{
    uint32_t t;
    di();
    t = ms_ticks;
    ei();
    return t;
}

/* =========================================================
 * Application state  (matches Arduino globals)
 * ========================================================= */
static bool     sensorOk             = false;
static bool     sensorError          = false;
static bool     conversionRequested  = false;
static uint32_t lastRequest          = 0;
static bool     relayOn              = false;
static float    setpointC            = SETPOINT_DEFAULT;
static float    lastTemp             = 0.0f;
static uint32_t lastBtnTime          = 0;
static bool     btnUpArmed           = true;
static bool     btnDownArmed         = true;

/* Startup screen state */
static bool     firstRead            = true;   /* true until startup screen dismissed */
static bool     firstReadDone        = false;  /* true once first sensor cycle done   */
static uint32_t startupTime          = 0;
static uint32_t lastFlash            = 0;
static bool     flashVisible         = true;

/* =========================================================
 * fmt_temp()  —  replaces Arduino dtostrf(val, 5, 1, buf)
 *
 * Formats a temperature to exactly 5 printable characters:
 *   " XX.X"  (positive, no tens)
 *   "XXX.X"  (positive, three digits)
 *   "-XX.X"  (negative)
 *
 * buf must be at least 6 bytes (5 chars + NUL).
 * Uses integer arithmetic only — no printf/sprintf.
 * ========================================================= */
static void fmt_temp(float val, char buf[6])
{
    int16_t t10;
    bool    neg;
    uint8_t tenths;
    uint8_t units;
    uint8_t tens;

    /* Round to one decimal place */
    t10 = (int16_t)(val * 10.0f + (val >= 0.0f ? 0.5f : -0.5f));
    neg = (t10 < 0);
    if (neg) t10 = (int16_t)(-t10);

    tenths = (uint8_t)((uint16_t)t10 % 10u);
    units  = (uint8_t)(((uint16_t)t10 / 10u) % 10u);
    tens   = (uint8_t)(((uint16_t)t10 / 100u) % 10u);

    buf[0] = neg  ? '-' : ' ';
    buf[1] = tens ? (char)('0' + tens)  : ' ';
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
    relayOn     = on;
    RELAY_LAT   = on ? 1u : 0u;
}

/* =========================================================
 * update_lcd()  —  mirrors Arduino updateLCD()
 *
 * Line 0:
 *   Normal : "Temp: XX.X°C    "  (16 chars)
 *   Error  : "Sensor Lost!    " or "No Sensor!      "
 *
 * Line 1:
 *   Cutoff : "CUTOFF        ON" or "CUTOFF       OFF"
 *   Normal : "Set : XX.X°C  ON" or "Set : XX.X°C OFF"
 *
 * The degree character on HD44780 is 0xDF (same as Arduino).
 * ========================================================= */
static void update_lcd(float tempC)
{
    char buf[6];

    /* --- Line 0 --- */
    lcd_set_cursor(0, 0);
    if (sensorError) {
        if (sensorOk) {
            lcd_print_str("Sensor Lost!    ");
        } else {
            lcd_print_str("No Sensor!      ");
        }
    } else {
        fmt_temp(tempC, buf);
        lcd_print_str("Temp:");
        lcd_print_str(buf);
        lcd_print_char((char)0xDF);   /* degree symbol */
        lcd_print_str("C    ");
    }

    /* --- Line 1 --- */
    lcd_set_cursor(0, 1);
    if (!sensorError && tempC >= MAX_SAFE_TEMP) {
        /* Safety cutoff active */
        if (relayOn) {
            lcd_print_str("CUTOFF        ON");
        } else {
            lcd_print_str("CUTOFF       OFF");
        }
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
 * Hardware initialisation
 * ========================================================= */
static void hardware_init(void)
{
    /* --- Oscillator: already set by config words (RSTOSC=HFINT32)
     *     Verify / force the frequency in software for safety.   --- */
    OSCFRQbits.HFFRQ = 0x06u;   /* HFINTOSC = 32 MHz (with 4x PLL) */

    /* --- Relay output: RC3 --- */
    RELAY_ANSEL = 0;
    RELAY_TRIS  = 0;
    RELAY_LAT   = 0;    /* relay off at reset */

    /* --- Button inputs: RC4, RC5 with weak pull-ups --- */
    BTN_UP_TRIS   = 1;
    BTN_DOWN_TRIS = 1;
    /* RC4 and RC5 are digital-only pins — no ANSEL register bits exist.  */

    /* Enable weak pull-ups on RC4 and RC5 */
    BTN_UP_WPU   = 1;
    BTN_DOWN_WPU = 1;

    /* Global weak pull-up enable: nWPUEN=0 in OPTION_REG */
    OPTION_REGbits.nWPUEN = 0;

    /* --- OneWire pin: RC2 (configured inside ow_init()) --- */
    ow_init();

    /* --- UART --- */
    uart_init();

    /* --- Timer1 --- */
    timer1_init();

    /* --- Interrupts --- */
    INTCONbits.PEIE = 1;   /* peripheral interrupt enable */
    INTCONbits.GIE  = 1;   /* global interrupt enable     */
}

/* =========================================================
 * main()
 * ========================================================= */
void main(void)
{
    float saved;

    hardware_init();

    /* UART banner */
    uart_puts("PIC16F18325 Thermostat starting\r\n");

    /* --- Read saved setpoint from EEPROM (mirrors Arduino EEPROM.get) --- */
    saved = eeprom_read_float(EEPROM_ADDR);
    if (saved >= SETPOINT_MIN && saved <= SETPOINT_MAX) {
        setpointC = saved;
    }
    uart_puts("Setpoint: ");
    uart_print_float(setpointC, 1);
    uart_puts(" C\r\n");

    /* --- LCD --- */
    lcd_init();
    lcd_backlight(true);

    /* Startup screen: blank line 0, "Please wait..." centred on line 1 */
    lcd_set_cursor(0, 0);
    lcd_print_str("                ");
    lcd_set_cursor(0, 1);
    lcd_print_str(" Please wait... ");
    startupTime = millis();
    lastFlash   = startupTime;

    /* WDT is enabled via config bits; first kick here */
    CLRWDT();

    /* =====================================================
     * Main loop  —  non-blocking, mirrors Arduino loop()
     * ===================================================== */
    while (1) {
        uint32_t now;
        float    tempC;
        bool     readOk;
        bool     curBtnUp;
        bool     curBtnDown;
        bool     desired;

        CLRWDT();   /* kick watchdog — matches wdt_reset() in Arduino */

        now = millis();

        /* -----------------------------------------------
         * Startup flash: "Please wait..." on line 1
         * Stays until first read cycle done AND minimum
         * display time elapsed.
         * ----------------------------------------------- */
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

        /* -----------------------------------------------
         * Button handling with debounce + arm/re-arm
         * Buttons are active LOW (pulled high by WPU).
         * ----------------------------------------------- */
        if (now - lastBtnTime >= BTN_DEBOUNCE_MS) {

            curBtnUp   = (BTN_UP_PORT   == 1u);
            curBtnDown = (BTN_DOWN_PORT  == 1u);

            /* Re-arm on release (HIGH) */
            if (curBtnUp && !btnUpArmed) {
                btnUpArmed  = true;
                lastBtnTime = now;
            }
            if (curBtnDown && !btnDownArmed) {
                btnDownArmed = true;
                lastBtnTime  = now;
            }

            /* Act on press (LOW) */
            if (!curBtnUp && btnUpArmed) {
                btnUpArmed  = false;
                lastBtnTime = now;

                setpointC = setpointC + SETPOINT_STEP;
                if (setpointC > SETPOINT_MAX) setpointC = SETPOINT_MAX;

                eeprom_write_float(EEPROM_ADDR, setpointC);

                uart_puts("Setpoint UP: ");
                uart_print_float(setpointC, 1);
                uart_puts(" C\r\n");

                if (!firstRead) update_lcd(lastTemp);

            } else if (!curBtnDown && btnDownArmed) {
                btnDownArmed = false;
                lastBtnTime  = now;

                setpointC = setpointC - SETPOINT_STEP;
                if (setpointC < SETPOINT_MIN) setpointC = SETPOINT_MIN;

                eeprom_write_float(EEPROM_ADDR, setpointC);

                uart_puts("Setpoint DOWN: ");
                uart_print_float(setpointC, 1);
                uart_puts(" C\r\n");

                if (!firstRead) update_lcd(lastTemp);
            }
        }

        /* -----------------------------------------------
         * Initiate DS18B20 conversion if interval elapsed
         * ----------------------------------------------- */
        if (!conversionRequested && (now - lastRequest >= READ_INTERVAL)) {

            if (ds18b20_start_conversion()) {
                uart_puts("Conversion started\r\n");
            } else {
                uart_puts("No sensor present\r\n");
                sensorError = true;
                sensorOk    = false;
                set_relay(false);
                if (!firstRead) update_lcd(0.0f);
            }
            lastRequest          = now;
            conversionRequested  = true;
        }

        /* -----------------------------------------------
         * Wait for conversion to finish (750 ms for 12-bit)
         * ----------------------------------------------- */
        if (!conversionRequested || (now - lastRequest < CONVERSION_MS)) {
            continue;   /* nothing more to do this pass */
        }
        conversionRequested = false;
        firstReadDone       = true;   /* first cycle complete regardless of outcome */

        /* -----------------------------------------------
         * Read temperature
         * ----------------------------------------------- */
        readOk = ds18b20_read_temp(&tempC);

        if (!readOk || tempC < -55.0f || tempC > 125.0f) {
            sensorError = true;
            set_relay(false);

            if (sensorOk) {
                /* Sensor was OK before — try to reinitialise the bus */
                uart_puts("Sensor lost — reinitialising bus\r\n");
                ow_init();
            } else {
                uart_puts("No sensor / CRC error\r\n");
            }

            if (!firstRead) update_lcd(0.0f);
            continue;
        }

        /* Valid reading */
        sensorOk    = true;
        sensorError = false;
        lastTemp    = tempC;

        uart_puts("Temp: ");
        uart_print_float(tempC, 2);
        uart_puts(" C  Setpoint: ");
        uart_print_float(setpointC, 1);
        uart_puts(" C\r\n");

        /* -----------------------------------------------
         * Safety cutoff
         * ----------------------------------------------- */
        if (tempC >= MAX_SAFE_TEMP) {
            set_relay(false);

            uart_puts("SAFETY CUTOFF — relay OFF\r\n");

            if (!firstRead) update_lcd(tempC);
            continue;
        }

        /* -----------------------------------------------
         * Hysteresis control
         * ----------------------------------------------- */
        if (tempC < (setpointC - HYSTERESIS)) {
            desired = true;
        } else if (tempC > (setpointC + HYSTERESIS)) {
            desired = false;
        } else {
            desired = relayOn;   /* inside hysteresis band — no change */
        }

        if (desired != relayOn) {
            set_relay(desired);
            uart_puts(relayOn ? "Relay ON\r\n" : "Relay OFF\r\n");
        }

        if (!firstRead) update_lcd(tempC);

    } /* while(1) */
}
