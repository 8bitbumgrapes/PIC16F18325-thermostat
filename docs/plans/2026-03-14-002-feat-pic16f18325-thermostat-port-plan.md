---
title: "feat: Port Arduino thermostat to PIC16F18325"
type: feat
status: completed
date: 2026-03-14
---

# feat: Port Arduino thermostat to PIC16F18325

> **Retrospective plan.** The complete firmware was written before this plan was produced.
> Plan created via `/ce:plan` to establish a formal CE workflow record for the project.

## Overview

A complete from-scratch firmware port of an Arduino-based thermostat to the PIC16F18325
14-pin microcontroller. The application reads temperature from a DS18B20 1-Wire sensor,
displays status on a 16×2 I2C LCD (PCF8574 backpack), controls a heating relay with
hysteresis, persists the user setpoint in data EEPROM, and provides debug output over UART.
All Arduino library calls were replaced with hand-written PIC16 peripheral drivers.

**Arduino source:** https://github.com/8bitbumgrapes/arduino-thermostat
**PIC repo:** https://github.com/8bitbumgrapes/PIC16F18325-thermostat
**Commit:** `907fc22` — Initial commit: PIC16F18325 thermostat port

---

## Problem Statement

The Arduino platform is convenient for prototyping but carries significant overhead: a
bootloader, the Arduino runtime, large standard libraries, and a physically large board.
The goal was to produce a compact, standalone firmware for a 14-pin DIP that:

- Runs directly on bare metal (no bootloader, no RTOS)
- Uses hardware peripherals efficiently (MSSP1 for I2C, EUSART1 for UART, Timer1 for timing)
- Retains 100% of the Arduino thermostat's user-facing behaviour
- Fits in the 14-pin PIC16F18325 with room for future features

---

## Proposed Solution

Six C modules compiled under MPLAB X / XC8, each owning one peripheral or concern:

| Module | Peripheral | Responsibility |
|--------|-----------|---------------|
| `config.h` | — | `#pragma config` words, `_XTAL_FREQ`, all constants, pin macros |
| `onewire.h/c` | RC2 (bit-bang) | 1-Wire bus, DS18B20 convert + read, CRC-8 |
| `lcd_i2c.h/c` | MSSP1 (I2C) | PCF8574 backpack + HD44780 4-bit driver |
| `eeprom_float.h/c` | Data EEPROM | IEEE-754 float persistence via NVM unlock |
| `uart.h/c` | EUSART1 (PPS) | TX-only debug output, float formatter |
| `main.c` | Timer1, GPIO | Application state machine, ISR, all control logic |

---

## Technical Approach

### Hardware Overview

```
PIC16F18325 (32 MHz HFINTOSC + 4× PLL)
  ├─ RC0 / RC1  — MSSP1 I2C master → PCF8574 → HD44780 16×2 LCD
  ├─ RC2        — bit-bang 1-Wire → DS18B20 temperature sensor
  ├─ RC3        — relay output (active HIGH)
  ├─ RC4 / RC5  — buttons (active LOW, internal WPU)
  └─ RA0        — EUSART1 TX via PPS (115200 baud, debug)
```

### Pin Assignments

| Signal | Pin | Notes |
|--------|-----|-------|
| SCL | RC0 | MSSP1, open-drain, external 4.7 kΩ pull-up |
| SDA | RC1 | MSSP1, open-drain, external 4.7 kΩ pull-up |
| OneWire DQ | RC2 | Bit-bang, open-drain, external 4.7 kΩ pull-up |
| Relay | RC3 | Active HIGH output, no pull-up |
| BTN_UP | RC4 | Digital-only pin — no ANSEL bit; active LOW, WPU |
| BTN_DOWN | RC5 | Digital-only pin — no ANSEL bit; active LOW, WPU |
| UART TX | RA0 | Via PPS: `RA0PPS = 0x10`; 115200 baud |

### Clock & Timing

| Parameter | Value | How |
|-----------|-------|-----|
| Fosc | 32 MHz | `RSTOSC = HFINT32` in config word + `OSCFRQbits.HFFRQ = 0x06` at runtime |
| Instruction cycle | 125 ns | Fosc / 4 = 8 MHz |
| Timer1 tick | 1 ms | Preload 0xE0C0 (57536), overflow every 8000 cycles at 8 MHz |
| I2C SCL | 100 kHz | `SSP1ADD = 79` → 32 M / (4 × 80) = 100 kHz |
| UART baud | 115200 | `SP1BRGL = 68`, BRG16=1, BRGH=1 → +0.64% error |
| WDT period | ~4.7 s | `WDTCPS_13` in config word; cleared every main loop pass |

### Module Details

#### `config.h` — Configuration & Constants

All `#pragma config` words, `_XTAL_FREQ`, and every runtime constant in one place:

| Constant | Value | Purpose |
|----------|-------|---------|
| `SETPOINT_DEFAULT` | 24.0°C | Initial setpoint if EEPROM blank |
| `SETPOINT_MIN/MAX` | 5.0 / 29.0°C | User-adjustable bounds |
| `SETPOINT_STEP` | 0.5°C | Per-button-press increment |
| `HYSTERESIS` | 1.0°C | Dead-band around setpoint |
| `MAX_SAFE_TEMP` | 30.0°C | Safety cutoff — forces relay OFF |
| `READ_INTERVAL` | 1000 ms | Time between sensor conversion requests |
| `CONVERSION_MS` | 750 ms | DS18B20 12-bit conversion time |
| `BTN_DEBOUNCE_MS` | 50 ms | Button re-arm window |
| `STARTUP_MIN_MS` | 2000 ms | Minimum startup screen display time |
| `FLASH_PERIOD_MS` | 500 ms | Startup screen text toggle rate |
| `LCD_ADDRESS` | 0x27 | PCF8574 I2C address |
| `EEPROM_ADDR` | 0x00 | Data EEPROM base address for 4-byte float |

Config words enable: brown-out reset (BOREN=ON), watchdog (WDTE=ON), HFINTOSC 32 MHz
(RSTOSC=HFINT32). MCLR is disabled (MCLRE=OFF) to free RA3.

#### `onewire.h/c` — 1-Wire Bus & DS18B20 Driver

Bit-bang 1-Wire on RC2 using open-drain topology (TRIS=1 to release, TRIS=0+LAT=0 to
pull low). Timing-critical pulses disable interrupts with `di()`/`ei()`.

Key timing:
- Reset: 480 µs LOW, sample presence at 70 µs, 410 µs recovery
- Write slot: 6 µs LOW + 64 µs recovery (bit '1') / 60 µs LOW + 10 µs recovery (bit '0')
- Read slot: 6 µs LOW, sample at ~9 µs, recover to 70 µs

DS18B20 sequence: Reset → `0xCC` (Skip ROM) → `0x44` (Convert T) → wait 750 ms →
Reset → `0xCC` → `0xBE` (Read Scratchpad) → read 9 bytes → CRC-8 → decode.

CRC-8: polynomial 0x31, reflected form 0x8C. Validated over bytes 0–7 against byte 8.

Temperature decode: `raw = (scratch[1] << 8) | scratch[0]`; `tempC = raw × 0.0625f`.
Valid range checked: −55°C to +125°C.

#### `lcd_i2c.h/c` — HD44780 LCD via PCF8574

MSSP1 hardware I2C master, polled via `SSP1IF`. PCF8574 bit mapping:

| PCF8574 bit | LCD signal | Notes |
|-------------|-----------|-------|
| P0 | RS | 0 = command, 1 = data |
| P1 | RW | Always 0 (write only) |
| P2 | E | Pulse HIGH to latch nibble |
| P3 | BL | Backlight (1 = ON) |
| P4–P7 | D4–D7 | Upper nibble in 4-bit mode |

Init sequence: 50 ms power-up wait, three 8-bit mode nibbles (0x3), switch to 4-bit (0x2),
then Function Set / Display OFF / Clear / Entry Mode / Display ON.

LCD row addressing: Row 0 = DDRAM 0x00, Row 1 = DDRAM 0x40 (non-contiguous).

#### `eeprom_float.h/c` — Float Persistence

Reads/writes 4-byte IEEE-754 float to data EEPROM via union cast. NVM unlock sequence
(0x55 / 0xAA) is protected by disabling GIE. `NVMCON1bits.NVMREGS = 0` selects data
EEPROM (not flash). Write time ~4 ms per byte (~16 ms total per setpoint save).

#### `uart.h/c` — TX-Only Debug UART

EUSART1 on RA0 via PPS (`RA0PPS = 0x10`). Polled TX (spins on TRMT). Includes
`uart_print_float()` — pure integer decomposition, no `printf`, no stdio. Example output:
`"Temp: 24.5 C, Set: 24.0 C, Relay: ON\r\n"`.

#### `main.c` — Application Logic

**State variables:**

```c
volatile uint32_t ms_ticks;        // Timer1 ISR: incremented every 1 ms
static bool       sensorOk;        // True after first valid read
static bool       sensorError;     // True if last read failed / no presence
static bool       conversionRequested; // Gates 750 ms non-blocking wait
static uint32_t   lastRequest;     // Timestamp of last conversion request
static bool       relayOn;         // Current relay state
static float      setpointC;       // User-adjustable target (EEPROM-backed)
static float      lastTemp;        // Last valid sensor reading
static uint32_t   lastBtnTime;     // Last debounce event timestamp
static bool       btnUpArmed, btnDownArmed;  // Button state machine
// Startup screen:
static bool       firstRead = true;      // Gates startup block + LCD guards
static bool       firstReadDone = false; // Set after first conversion cycle
static uint32_t   startupTime, lastFlash;
static bool       flashVisible;
```

**Main loop structure:**

1. `CLRWDT()` — kick watchdog every pass
2. Startup flash block — toggle `" Please wait... "` every 500 ms; dismiss when
   `firstReadDone && (now − startupTime ≥ 2000 ms)`
3. Button debounce — arm/re-arm on release; adjust setpoint ±0.5°C; write EEPROM;
   update LCD if not in startup phase
4. Conversion initiation — request every 1000 ms via `ds18b20_start_conversion()`
5. Non-blocking wait — `continue` until 750 ms elapsed
6. Temperature read — `ds18b20_read_temp()`; CRC + range validate; set error flags on failure
7. Safety cutoff — if `tempC ≥ 30.0°C`, force relay OFF, show "CUTOFF"
8. Hysteresis control — relay ON if `T < setpoint − 1.0`; OFF if `T > setpoint + 1.0`; else hold
9. LCD update — `update_lcd(lastTemp)` (no-op if `firstRead == true`)

**`update_lcd()` display modes:**

| Condition | Line 0 | Line 1 |
|-----------|--------|--------|
| Normal | `"Temp: XX.X°C   "` | `"Set : XX.X°C ON"` |
| Sensor error | `"No Sensor!     "` | `"Set : XX.X°C   "` |
| Safety cutoff | `"Temp: XX.X°C   "` | `"CUTOFF       OFF"` |

**`fmt_temp()`** replaces Arduino `dtostrf()`: pure integer math, no `printf`, 5-char output
format `" XX.X"`.

**Timer1 ISR:** Reloads preload 0xE0C0, clears TMR1IF, increments `ms_ticks`.
**`millis()`**: Disables GIE, reads 32-bit `ms_ticks`, re-enables GIE (prevents torn read on
8-bit CPU).

---

## Alternative Approaches Considered

| Decision | Alternatives rejected | Reason chosen |
|----------|-----------------------|---------------|
| **Bit-bang 1-Wire** | UART-based 1-Wire trick | Bit-bang is direct; no UART resource conflict; well-understood timing |
| **Hardware MSSP1 for I2C** | Bit-bang I2C | Hardware I2C is more reliable; MSSP1 is available; saves code complexity |
| **Timer1 for millis()** | Timer0, busy-wait | Timer1 is 16-bit; sufficient period at 32 MHz; frees Timer0 for future use |
| **Polled MSSP1** | Interrupt-driven I2C | Simpler; I2C transactions are short (< 1 ms); interrupt-driven adds complexity for no benefit |
| **TX-only UART** | Full duplex UART | No receive needed; saves one pin and code |
| **Pure integer fmt_temp()** | `printf` / `dtostrf` | Avoids stdio library; smaller binary; deterministic output |
| **32 MHz via HFINTOSC + PLL** | External crystal | No external component; 1-Wire timing still achievable; saves board space |
| **Data EEPROM for setpoint** | Flash self-write | Data EEPROM has higher endurance (> 100k cycles) and simpler write sequence |

---

## System-Wide Impact

### Interaction Graph

```
Button press (RC4/RC5)
  → setpointC updated (bounds-checked)
  → eeprom_write_float() (NVM unlock, ~16 ms, GIE disabled during unlock)
  → uart_puts() (debug log)
  → update_lcd() (if !firstRead)

Timer1 ISR (1 ms)
  → ms_ticks++

Main loop — every 1000 ms:
  → ds18b20_start_conversion()
      → ow_reset() + ow_write_byte() × 2 (interrupts disabled during pulses)
  → (750 ms non-blocking wait via continue)
  → ds18b20_read_temp()
      → ow_reset() + ow_write_byte() × 2 + ow_read_byte() × 9
      → CRC-8 validation
  → Safety cutoff check → set_relay()
  → Hysteresis control → set_relay()
  → update_lcd() → pcf_write() × N (MSSP1 I2C transactions)
  → uart_puts() (debug log)

Startup screen (first 2000 ms):
  → lcd_print_str() every 500 ms (toggle text)
  → All update_lcd() calls at button/sensor sites suppressed (if firstRead == true)
```

### Error Propagation

| Error source | Immediate effect | Recovery |
|-------------|-----------------|---------|
| Sensor absent (no presence pulse) | `sensorError = true`, relay OFF, LCD shows "No Sensor!" | Retry every 1000 ms; `ow_init()` re-run if sensor was previously OK |
| CRC error / out-of-range read | Same as above; `uart_puts("Sensor lost")` | Same retry loop |
| I2C bus stall (PCF8574 hung) | `i2c_wait()` / `i2c_idle()` spin forever; WDT fires ~4.7 s later | System reset; relay off at power-on; setpoint restored from EEPROM |
| EEPROM write corruption | Silently returns bad setpoint on next read | Self-heals on next button press (overwrites bad bytes) |
| WDT expiry | System reset | Relay OFF at reset; normal startup sequence |
| ms_ticks wraparound (49 days) | No effect | All timing uses `(now − last ≥ interval)` safe subtraction |

### State Lifecycle Risks

None identified. All state variables have defined initialization values and single, clear
transition paths. `firstRead` transitions exactly once (true → false). `firstReadDone`
transitions exactly once (false → true). Relay defaults OFF at reset. Sensor flags are
updated on every read cycle regardless of previous state.

### Integration Test Scenarios (Hardware Validation Required)

| # | Scenario | What it validates |
|---|----------|------------------|
| 1 | **Power-on with sensor** | Full startup sequence; Timer1 ISR; I2C responsive; sensor detected; relay control begins |
| 2 | **Power-on without sensor** | Error path; relay stays OFF; LCD shows "No Sensor!"; no WDT hang |
| 3 | **Temperature crosses 30°C** | Safety cutoff activates; relay forced OFF; "CUTOFF" on LCD; resumes when T drops |
| 4 | **Button presses during startup** | Setpoint adjusts, EEPROM written; LCD update deferred; setpoint correct after transition; persists across power cycle |
| 5 | **Rapid button mashing** | I2C bus stable; no WDT expiry; no double-steps in setpoint; UART output continuous |

---

## Acceptance Criteria

### Functional Requirements

- [x] Temperature read from DS18B20 every 1 s with CRC-8 validation
- [x] Temperature displayed on LCD line 0 in format `"Temp: XX.X°C   "`
- [x] Relay turns ON when T < (setpoint − 1.0°C)
- [x] Relay turns OFF when T > (setpoint + 1.0°C)
- [x] Relay state unchanged within hysteresis band
- [x] Safety cutoff: relay forced OFF, LCD shows "CUTOFF" when T ≥ 30.0°C
- [x] BTN_UP increases setpoint by 0.5°C (max 29.0°C)
- [x] BTN_DOWN decreases setpoint by 0.5°C (min 5.0°C)
- [x] Setpoint written to data EEPROM on every button press
- [x] Setpoint restored from EEPROM at power-on (default 24.0°C if EEPROM blank)
- [x] Startup screen: `" Please wait... "` flashes at 500 ms on LCD line 1 for ≥ 2000 ms
- [x] UART outputs temperature, setpoint, relay state, and error messages at 115200 baud

### Non-Functional Requirements

- [x] Runs on PIC16F18325 (14-pin DIP, 3.5 KB flash, 256 B EEPROM, 512 B RAM)
- [x] 32 MHz system clock (HFINTOSC + 4× PLL, no external crystal)
- [x] No standard library dependencies (`stdio`, `stdlib`, `string`) — XC8 only
- [x] Non-blocking main loop (no `__delay_ms()` in main loop body)
- [x] Watchdog timer active; cleared every loop pass; ~4.7 s period
- [x] All timing safe across `ms_ticks` 32-bit wraparound (49 days)
- [x] Relay defaults OFF at reset (fail-safe)

### Quality Gates

- [x] All peripheral drivers in separate `.h`/`.c` modules
- [x] No `printf` / `dtostrf` / floating-point library calls
- [x] CRC-8 validation on all DS18B20 scratchpad reads
- [x] Interrupt-safe `millis()` (GIE disabled during 32-bit read)
- [x] GIE disabled during EEPROM NVM unlock sequence
- [x] Interrupts disabled only during timing-critical 1-Wire pulse segments
- [ ] Hardware validation complete (Scenarios 1–5 above — pending physical build)

---

## Dependencies & Prerequisites

| Dependency | Detail |
|-----------|--------|
| MPLAB X IDE | Build and programming environment |
| XC8 compiler | Free tier sufficient (no optimization needed) |
| PICkit 3/4 or compatible | For programming via ICSP |
| DS18B20 | 1-Wire temperature sensor; 4.7 kΩ pull-up to Vdd on DQ |
| PCF8574 I2C backpack + HD44780 LCD | 16×2 character LCD; address jumpers set to 0x27; 4.7 kΩ pull-ups on SCL/SDA |
| Relay module | Active-HIGH input; driven from RC3 via transistor or logic-level MOSFET |
| 5 V supply | Vdd for PIC + peripherals |

---

## Risk Analysis & Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| I2C bus stall (PCF8574 offline) | Low | High (WDT reset) | Verify PCF8574 wiring; test with logic analyser; WDT reset is safe (relay OFF) |
| DS18B20 CRC errors on long wire runs | Medium | Low (relay OFF on error) | Keep DQ wire < 1 m; use proper pull-up; errors trigger retry each second |
| EEPROM data corruption on power loss during write | Very low | Low (self-heals on next press) | Brown-out reset (BOREN=ON) reduces window; acceptable for setpoint storage |
| Button double-step during EEPROM write (~16 ms) | Very low | Cosmetic | Arm/re-arm logic + 50 ms debounce window; test with rapid button presses |
| RC4/RC5 ANSEL confusion | Already resolved | — | Digital-only pins have no ANSEL bits; macros are no-ops; documented in code |

---

## Future Considerations

Features discussed but not prioritised (from session 1 brainstorm):

- **Temperature scheduling** — setpoint profiles by time-of-day
- **Hysteresis tuning UI** — allow user to adjust the 1.0°C dead-band
- **Alarm output** — buzzer on sensor failure or safety cutoff
- **Multiple sensor support** — DS18B20 ROM addressing for daisy-chained sensors
- **WDT recovery improvements** — UART message on reset cause; flash LED on WDT trip
- **EEPROM wear levelling** — rotate write address to spread cycles (> 100k cycles per byte, so low urgency)
- **Sleep modes** — interrupt-driven wakeup for battery-powered deployment
- **Code quality review pass** — `/ce:review` pass before any of the above

---

## Sources & References

### Internal

- `thermostat/config.h` — all hardware constants and pin macros
- `thermostat/main.c` — application state machine, Timer1 ISR, millis(), fmt_temp()
- `thermostat/onewire.c` — 1-Wire bit-bang, DS18B20 driver, CRC-8
- `thermostat/lcd_i2c.c` — MSSP1 I2C master, PCF8574 + HD44780 driver
- `thermostat/eeprom_float.c` — data EEPROM read/write, NVM unlock
- `thermostat/uart.c` — EUSART1 TX-only, float formatter
- `docs/plans/2026-03-14-001-feat-startup-screen-lcd-plan.md` — startup screen sub-feature plan
- `docs/brainstorms/2026-03-14-startup-screen-brainstorm.md` — startup screen design decisions

### External

- Arduino original: https://github.com/8bitbumgrapes/arduino-thermostat
- PIC16F18325 datasheet — oscillator, Timer1, MSSP1, EUSART, NVM, WDT registers
- DS18B20 datasheet — 1-Wire timing, scratchpad format, CRC-8 polynomial
- PCF8574 datasheet — I2C address, GPIO bit mapping
- HD44780 datasheet — 4-bit init sequence, DDRAM addressing, timing requirements
