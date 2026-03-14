---
review_agents:
  - compound-engineering:review:security-sentinel
  - compound-engineering:review:performance-oracle
  - compound-engineering:review:architecture-strategist
  - compound-engineering:review:code-simplicity-reviewer
---

# Project: PIC16F18325 Thermostat Firmware

This is bare-metal embedded C firmware for the PIC16F18325 microcontroller, compiled
with MPLAB X / XC8. There is no OS, no RTOS, no heap allocation, and no standard
library (no stdio, no stdlib). All code runs in a single-threaded main loop with one
ISR (Timer1 overflow).

## Review Context

When reviewing this codebase, apply embedded systems conventions, NOT web/backend conventions:

- **No unit tests exist** — this is normal for bare-metal firmware at this stage. Do not flag absence of tests as a critical issue.
- **No heap / dynamic allocation** — all state is static or on the stack. This is correct and intentional.
- **Blocking peripheral calls are acceptable** — polled I2C, polled UART TX, and `__delay_us()` are standard practice on PIC16.
- **Interrupt safety matters** — flag any 32-bit variable reads that aren't protected by GIE disable.
- **Timing correctness is critical** — 1-Wire bit-bang timing must be within ±10 µs of spec. Flag any concerns.
- **WDT coverage** — the watchdog must be cleared on every main loop pass. Flag any code paths that could prevent this.
- **Relay defaults** — relay must be OFF at reset and on any error condition. Flag anything that could leave relay in an undefined state.
- **EEPROM endurance** — flag any code that writes EEPROM more frequently than intended (e.g., in a loop).
- **RC4/RC5 are digital-only pins** — they have no ANSEL bits on PIC16F18325. This is correct; do not flag as a bug.

## Technology Stack

- Language: C (C99)
- Compiler: XC8 (Microchip)
- IDE: MPLAB X
- MCU: PIC16F18325 (14-pin DIP, 3.5 KB flash, 512 B RAM, 256 B EEPROM)
- Peripherals: Timer1, MSSP1 (I2C), EUSART1 (UART), GPIO, WDT, BOR, NVM
- External hardware: DS18B20 (1-Wire), PCF8574+HD44780 LCD (I2C), relay, 2 buttons
