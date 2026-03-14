---
status: pending
priority: p2
issue_id: "010"
tags: [code-review, quality, lcd]
---

# LCD init delay values are magic numbers

## Problem Statement

`lcd_i2c.c` contains multiple `__delay_ms()` and `__delay_us()` calls with bare
numeric values. These are HD44780-specified timing requirements but they're
undocumented inline, making it impossible to verify them against the datasheet
without knowing what each number represents.

## Findings

**File:** `thermostat/lcd_i2c.c`, lines ~131, 156, 162, 165, 168, 172, 176, 184, 188

Examples:
```c
__delay_ms(50);       // what is this?
__delay_ms(5);        // what is this?
__delay_us(150);      // what is this?
__delay_us(1);        // E pulse width?
__delay_us(50);       // execution time?
__delay_ms(2);        // clear display time?
```

## Proposed Solution

Add named constants to `config.h` (or a dedicated section in `lcd_i2c.c`):

```c
/* HD44780 initialisation timing (from datasheet) */
#define LCD_POWERUP_MS      50u    /* ≥ 40 ms after Vcc > 2.7 V */
#define LCD_INIT1_MS         5u    /* ≥ 4.1 ms after first function set */
#define LCD_INIT2_US       150u    /* ≥ 100 µs after second/third function set */
#define LCD_E_PULSE_US       1u    /* E pulse width ≥ 450 ns */
#define LCD_EXEC_US         50u    /* Command execution ≥ 37 µs */
#define LCD_CLEAR_MS         2u    /* Clear/home execution ≥ 1.52 ms */
```

**Effort:** Small-Medium
**Risk:** Low — rename only, values unchanged

## Acceptance Criteria

- [ ] All bare delay values in `lcd_i2c.c` replaced with named constants
- [ ] Constants annotated with datasheet reference values

## Work Log

- 2026-03-14: Identified by simplicity review agent
