---
status: pending
priority: p2
issue_id: "007"
tags: [code-review, quality, simplicity]
---

# Button UP/DOWN handler logic is copy-pasted

## Problem Statement

The button debounce, arm/re-arm, setpoint adjustment, EEPROM write, UART log, and
LCD update logic is duplicated verbatim for BTN_UP (lines 318–331) and BTN_DOWN
(lines 333–346). A bug fix or feature change must be applied twice.

## Findings

**File:** `thermostat/main.c`, lines 302–348

~46 lines of near-identical code for two buttons, differing only in:
- Which button port is read (`BTN_UP_PORT` vs `BTN_DOWN_PORT`)
- Which armed flag is used (`btnUpArmed` vs `btnDownArmed`)
- The setpoint delta (`+SETPOINT_STEP` vs `-SETPOINT_STEP`)
- The log string (`"Setpoint UP"` vs `"Setpoint DOWN"`)

## Proposed Solution

Extract a `handle_button()` helper:

```c
static void handle_button(bool pressed, bool *armed, float delta, uint32_t now) {
    if (!pressed && *armed) {
        *armed = false;
    }
    if (pressed && *armed) {
        *armed = false;
        setpointC += delta;
        if (setpointC > SETPOINT_MAX) setpointC = SETPOINT_MAX;
        if (setpointC < SETPOINT_MIN) setpointC = SETPOINT_MIN;
        eeprom_write_float(EEPROM_ADDR, setpointC);
        uart_puts(delta > 0.0f ? "Setpoint UP: " : "Setpoint DOWN: ");
        uart_print_float(setpointC, 1);
        uart_puts(" C\r\n");
        if (!firstRead) update_lcd(lastTemp);
    }
    if (!pressed && !*armed) { *armed = true; }
}
```

**Effort:** Medium
**Risk:** Low — pure refactor, behavior preserved

## Acceptance Criteria

- [ ] Single `handle_button()` function handles both buttons
- [ ] No logic duplication between UP and DOWN handlers
- [ ] Behavior identical to current implementation

## Work Log

- 2026-03-14: Identified by simplicity + architecture review agents
