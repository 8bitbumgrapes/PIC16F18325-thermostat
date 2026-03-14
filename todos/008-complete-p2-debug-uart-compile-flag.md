---
status: pending
priority: p2
issue_id: "008"
tags: [code-review, uart, quality]
---

# No compile-time flag to disable UART debug output

## Problem Statement

UART debug output (`uart_puts()`, `uart_print_float()`) is always-on. On a deployed
device without a serial monitor connected, this wastes instruction cycles and power
on every sensor read, button press, and relay change. There is no way to build a
"silent production" binary without manually removing uart calls.

## Findings

**File:** `thermostat/main.c`, multiple locations (lines ~237, 246, 330, 343, 392, 405, 415, 432 etc.)

Approximately 10–15 `uart_puts()` / `uart_print_float()` call sites throughout `main.c`.

## Proposed Solution

Add a `DEBUG_UART` flag to `config.h` and wrap all debug output:

```c
/* config.h */
#define DEBUG_UART  1   /* Set to 0 for silent production build */

/* uart.h — add macros */
#if DEBUG_UART
  #define debug_puts(s)         uart_puts(s)
  #define debug_print_float(v,d) uart_print_float(v,d)
#else
  #define debug_puts(s)          do { } while(0)
  #define debug_print_float(v,d) do { } while(0)
#endif
```

Replace all `uart_puts(...)` debug calls in `main.c` with `debug_puts(...)`.
Keep `uart_init()` call unconditional (or also guard it).

**Effort:** Medium (touch ~15 call sites)
**Risk:** Low

## Acceptance Criteria

- [ ] `DEBUG_UART` flag in `config.h` (default 1)
- [ ] All debug output in `main.c` uses `debug_puts()` / `debug_print_float()`
- [ ] Setting `DEBUG_UART 0` produces no UART output and no uart_init() call
- [ ] Startup banner still appears when `DEBUG_UART 1`

## Work Log

- 2026-03-14: Identified by architecture review agent
