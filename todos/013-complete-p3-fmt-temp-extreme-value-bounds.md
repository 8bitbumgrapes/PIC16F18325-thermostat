---
status: pending
priority: p3
issue_id: "013"
tags: [code-review, quality, robustness]
---

# fmt_temp() and uart_print_float() lack bounds checks for extreme values

## Problem Statement

Both formatting functions assume the input float is within a reasonable range.
A corrupt sensor reading producing NaN, ±infinity, or a value outside ±999°C could
corrupt the output buffer or produce undefined output. The DS18B20 is already
range-checked elsewhere, but the formatters themselves have no internal guard.

## Findings

**File:** `thermostat/main.c`, lines 103–126 (`fmt_temp`)
**File:** `thermostat/uart.c`, lines 90–154 (`uart_print_float`)

`fmt_temp()` multiplies by 10 and extracts digits — a value > 9999 overflows
the uint16_t used internally. `uart_print_float()` scales by up to 10000 into
a uint32_t — overflows at ~429,496°C.

In practice: `ds18b20_read_temp()` already rejects values outside −55…+125°C,
so these formatters never receive extreme values from the sensor path.

## Proposed Solution

Add a clamp in `fmt_temp()`:

```c
/* Clamp to display range — DS18B20 is -55..+125 but guard anyway */
if (val >  999.9f) val =  999.9f;
if (val < -99.9f)  val = -99.9f;
```

And a similar clamp at the top of `uart_print_float()`.

**Effort:** Small
**Risk:** None

## Acceptance Criteria

- [ ] `fmt_temp()` clamps input to ±999.9°C before formatting
- [ ] `uart_print_float()` clamps input to a safe range before scaling

## Work Log

- 2026-03-14: Identified by security + performance review agents
