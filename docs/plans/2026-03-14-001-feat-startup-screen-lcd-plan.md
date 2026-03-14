---
title: "feat: Add friendly startup screen to thermostat LCD"
type: feat
status: completed
date: 2026-03-14
origin: docs/brainstorms/2026-03-14-startup-screen-brainstorm.md
---

# feat: Add friendly startup screen to thermostat LCD

> **Retrospective plan.** This feature was implemented in the same session as the brainstorm.
> Plan produced via `/ce:plan` to complete the CE workflow record.

## Overview

Display a flashing `" Please wait... "` message on LCD line 1 during the first DS18B20
temperature read cycle, then transition cleanly to the standard thermostat display.
Prevents the original misleading `"Temp:  0.0°C"` that appeared at boot before any
reading was taken.

## Problem Statement / Motivation

The original code initialised `sensorError = false` and immediately called
`update_lcd(0.0f)`, showing `"Temp:  0.0°C"` for ~1750 ms — accurate in format but
misleading since no read had occurred. Setting `sensorError = true` at startup was
considered but rejected: it would show `"No Sensor!"` immediately, which is
unnecessarily alarming for an end-user device.
(see brainstorm: `docs/brainstorms/2026-03-14-startup-screen-brainstorm.md`)

## Solution

Three-variable state machine added to `main.c`:

| Variable | Role |
|----------|------|
| `firstRead` | Master flag — gates startup flash block and LCD update guards |
| `firstReadDone` | Set once first sensor cycle completes (success or failure) |
| `flashVisible` | Tracks current text on/off state for flash toggle |

Two constants added to `config.h`:

| Constant | Value | Purpose |
|----------|-------|---------|
| `STARTUP_MIN_MS` | 2000 ms | Minimum display time before transition |
| `FLASH_PERIOD_MS` | 500 ms | Text flash toggle frequency |

Transition fires when: `firstReadDone == true` AND `(now - startupTime >= STARTUP_MIN_MS)`.

All `update_lcd()` calls in the main loop are guarded with `if (!firstRead)`. Relay control,
sensor state tracking, safety cutoff, and button setpoint adjustments all proceed normally
during startup — only LCD output is deferred.
(see brainstorm: key decisions — flash text not backlight; buttons still active)

## Technical Considerations

- **`firstReadDone` placement (line 375, `main.c`):** Set immediately after
  `conversionRequested = false`, before the actual read attempt. This ensures it is set on
  both success and failure paths with a single assignment. Intentional trade-off: startup
  screen exits as soon as the conversion timer expires, regardless of subsequent read outcome.
  (see brainstorm: `firstReadDone set before read result is processed`)

- **Timing pattern:** Uses `(now - lastFlash >= FLASH_PERIOD_MS)` — safe subtraction handles
  `ms_ticks` wraparound; tolerates main-loop jitter without missing flash beats.

- **No blocking added:** Startup screen adds zero blocking operations. WDT exposure
  unchanged. Main loop timing unaffected.

- **LCD update centralisation maintained:** All writes still flow through `update_lcd()`;
  guards are applied at call sites, not inside the function.

## Acceptance Criteria

- [x] Startup screen shows `" Please wait... "` on line 1; line 0 blank
- [x] Text flashes at 500 ms period (on/off)
- [x] Screen persists for a minimum of 2000 ms regardless of sensor speed
- [x] Screen transitions to normal display after first read cycle completes AND minimum time elapsed
- [x] Transition works on both sensor-found and sensor-not-found paths
- [x] Relay control operates normally during startup (sensor state tracked)
- [x] Button presses during startup adjust setpoint and write to EEPROM normally
- [x] Safety cutoff active during startup
- [x] No misleading `"Temp: 0.0°C"` or false `"No Sensor!"` shown at boot

## SpecFlow Validation (2026-03-14)

11/11 edge cases validated. 0 risks. 2 minor acceptable gaps:

1. `firstReadDone` set before read attempt — intentional per brainstorm, not a defect
2. `lastTemp` is 0.0f on first transition if read fails — acceptable; error display follows immediately on next cycle

## Files Changed

| File | Change |
|------|--------|
| `config.h` | Added `STARTUP_MIN_MS 2000UL` (line 65) and `FLASH_PERIOD_MS 500UL` (line 66) |
| `main.c` | State vars (lines 85–90); startup init block (lines 254–260); flash/transition block (lines 285–296); `firstReadDone = true` (line 375); LCD update guards at lines 331, 346, 362, 394, 417, 437 |

## Sources

- **Origin brainstorm:** [docs/brainstorms/2026-03-14-startup-screen-brainstorm.md](../brainstorms/2026-03-14-startup-screen-brainstorm.md)
  — Key decisions carried forward: flash text not backlight; 2000 ms minimum display time; buttons active during startup
- Commit: `907fc22` — Initial commit: PIC16F18325 thermostat port (includes this feature)
