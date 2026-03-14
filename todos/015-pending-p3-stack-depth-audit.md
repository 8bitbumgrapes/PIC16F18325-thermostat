---
status: pending
priority: p3
issue_id: "015"
tags: [code-review, performance, robustness]
---

# Stack depth audit needed — deepest call chain is 7 levels

## Problem Statement

PIC16F18325 has a 16-level hardware call stack. The deepest observed call chain
reaches 7 levels:

`main → update_lcd → lcd_print_str → lcd_print_char → lcd_send_byte → lcd_send_nibble → pcf_write → i2c_write_byte`

This uses 7 of 16 stack levels. Combined with local variables in each frame, the
hardware stack is feasible but not audited with the XC8 compiler's stack analysis tool.
Future features (multiple sensors, alarm output) could add nesting.

## Findings

**Files:** `main.c`, `lcd_i2c.c`

7-level chain identified. XC8 free tier does not output a stack depth report by default.
No overflow has been observed in testing.

## Proposed Solution

Run MPLAB X / XC8 with stack analysis enabled:

1. In MPLAB X: Project → Properties → XC8 Global Options → Enable stack analysis
2. Review the generated report for worst-case stack depth
3. Ensure total nesting ≤ 14 levels (leave 2 spare for ISR + one more)

If headroom is tight, consider flattening the `lcd_print_char → lcd_send_byte → lcd_send_nibble → pcf_write` chain into fewer levels.

**Effort:** Small (to audit); Medium (to fix if overflow found)
**Risk:** None for current code; risk increases as features are added

## Acceptance Criteria

- [ ] XC8 stack analysis run and depth reported
- [ ] Worst-case stack depth confirmed ≤ 14 levels

## Work Log

- 2026-03-14: Identified by performance review agent
