---
status: pending
priority: p2
issue_id: "011"
tags: [code-review, quality, timer1]
---

# Timer1 preload value 0xE0C0 is duplicated without a named constant

## Problem Statement

The Timer1 preload value `0xE0C0` (57536) appears twice in `main.c`: once in
`timer1_init()` and once in the ISR. A typo in either location would cause the
millisecond timing to silently drift with no compile error.

## Findings

**File:** `thermostat/main.c`, lines 36–37 and 50–51

```c
// timer1_init():
TMR1H = 0xE0u;
TMR1L = 0xC0u;

// ISR:
TMR1H = 0xE0u;   // must match timer1_init exactly
TMR1L = 0xC0u;
```

## Proposed Solution

Add named constants to `config.h`:

```c
/* Timer1 preload for 1 ms overflow at 32 MHz (Fosc/4 = 8 MHz)
 * Count = 65536 - (8 MHz / 1000 Hz) = 65536 - 8000 = 57536 = 0xE0C0 */
#define TMR1_PRELOAD_H  0xE0u
#define TMR1_PRELOAD_L  0xC0u
```

Use in both locations:
```c
TMR1H = TMR1_PRELOAD_H;
TMR1L = TMR1_PRELOAD_L;
```

**Effort:** Small
**Risk:** None

## Acceptance Criteria

- [ ] `TMR1_PRELOAD_H` and `TMR1_PRELOAD_L` defined in `config.h` with derivation comment
- [ ] Both uses in `main.c` updated to use the named constants

## Work Log

- 2026-03-14: Identified by simplicity review agent
