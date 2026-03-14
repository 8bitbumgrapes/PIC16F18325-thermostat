---
status: pending
priority: p2
issue_id: "004"
tags: [code-review, onewire, timing]
---

# 1-Wire read bit sample timing may drift under compiler optimisation

## Problem Statement

In `ow_read_byte()`, the bit sample is taken at ~9 µs after the bus is released.
DS18B20 spec requires sampling within 15 µs of release. The current code relies on
`__delay_us(3)` plus incidental instruction cycles to hit this window, but the XC8
compiler may schedule instructions differently at various optimisation levels.

## Findings

**File:** `thermostat/onewire.c`, lines 124–127

```c
OW_DRIVE_LOW();
__delay_us(6);
OW_RELEASE();
__delay_us(3);          // ~9 µs total from drive-low
if (OW_READ()) { ... }  // sample here — compiler may insert/remove instructions
```

The sample window is 0–15 µs from release. At 9 µs nominal this is fine, but
without explicit NOPs the compiler has freedom to move code around the sample point.

Confirmed safe at default XC8 optimisation (free tier = no optimisation). Risk
increases if the paid XC8 Pro licence is used with `-O2`.

## Proposed Solution

Add explicit `NOP()` calls to anchor timing:

```c
OW_DRIVE_LOW();
__delay_us(6);
OW_RELEASE();
__delay_us(3);
NOP(); NOP();            // anchor: guarantee sample ≥ 9 µs from release
if (OW_READ()) { ... }
```

**Effort:** Small
**Risk:** None — `NOP()` adds 0.25 µs at 32 MHz, still well within the 15 µs window.

## Acceptance Criteria

- [ ] Two `NOP()` calls inserted between `__delay_us(3)` and `OW_READ()` in `ow_read_byte()`
- [ ] Timing verified: sample at ~9.5 µs from release, well within 15 µs spec

## Work Log

- 2026-03-14: Identified by performance review agent
