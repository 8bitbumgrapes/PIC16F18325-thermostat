---
status: pending
priority: p3
issue_id: "012"
tags: [code-review, quality, uart]
---

# uart_puts_P() is dead code

## Problem Statement

`uart_puts_P()` is defined in `uart.h`/`uart.c` as an alias for `uart_puts()`. It
exists for Arduino source compatibility (Arduino's `uart_puts_P()` sends strings
from PROGMEM flash). On PIC16, there is no PROGMEM distinction — the function is
identical to `uart_puts()` and is never called anywhere.

## Findings

**File:** `thermostat/uart.c`, lines 73–76; `thermostat/uart.h`, lines ~43–46

```c
void uart_puts_P(const char *s) {
    uart_puts(s);   // identical wrapper, never called
}
```

## Proposed Solution

**Option A — Remove it:** Delete the function from uart.c and its declaration from uart.h.
Simple; keeps codebase lean.

**Option B — Keep with comment:** Add a comment explaining the Arduino heritage so a
future reader doesn't wonder why it exists.

```c
/* uart_puts_P() — Arduino compatibility alias.
 * On PIC16 there is no PROGMEM; this is identical to uart_puts(). */
```

**Recommendation:** Option A if cleanliness is priority. Option B if the Arduino
comparison is useful documentation.

**Effort:** Small
**Risk:** None

## Acceptance Criteria

- [ ] Either remove `uart_puts_P()` entirely, or document its purpose clearly

## Work Log

- 2026-03-14: Identified by simplicity review agent
