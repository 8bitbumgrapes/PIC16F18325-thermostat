---
title: "Brainstorm: Friendly Startup Screen"
date: 2026-03-14
status: Implemented
generated-by: ce:brainstorm
---

# Brainstorm: Friendly Startup Screen

**Date:** 2026-03-14
**Status:** Implemented

---

## What We're Building

A friendly flashing startup screen for the PIC16F18325 thermostat that displays
`" Please wait... "` centred on line 1 of the 16×2 LCD while the first DS18B20
temperature read cycle completes. Once the cycle resolves (sensor found or not)
and a minimum display time has elapsed, the screen transitions cleanly to the
standard thermostat display.

---

## Why This Approach

The original code initialised `sensorError = false` and immediately called
`update_lcd(0.0f)`, causing the display to show `"Temp:  0.0°C"` for the first
~1750 ms — accurate in format but misleading in content since no reading had
been taken yet.

Two simpler alternatives were rejected:

- **Do nothing / fix the value shown:** Still requires ~1750 ms of either blank
  display or misleading data. Doesn't communicate to the user that the device
  is initialising.
- **Set `sensorError = true` at startup:** Would show `"No Sensor!"` immediately
  at boot — technically correct but unnecessarily alarming for an end-user device
  that hasn't had time to detect anything yet.

The flashing startup screen was chosen because it communicates activity, looks
deliberate, and avoids showing any temperature data until there is a real reading
to show.

---

## Approaches Considered

### Approach A — Flash the backlight (rejected)
Toggle the LCD backlight on/off during startup. Simple to implement (one bit in
PCF8574 `P3`).

**Pros:** Minimal code.
**Cons:** Looks cheap and distracting. Backlight flicker is associated with
hardware problems, not friendly boot sequences.

### Approach B — Flash the text (chosen ✅)
Toggle `" Please wait... "` / `"                "` on line 1 every 500 ms.
Line 0 left blank.

**Pros:** Professional appearance. Text on/off is clearly intentional. Easy to
read during the on-phase. Works within the existing `lcd_print_str()` API.
**Cons:** Requires tracking two extra state variables (`lastFlash`, `flashVisible`).

### Approach C — Static message, no flash
Display `" Please wait... "` statically until the first read completes.

**Pros:** Even simpler — no flash state variables.
**Cons:** A static message looks frozen. The user has no visual feedback that
the device is doing something. Flash was preferred for this reason.

**Chosen: Approach B.**

---

## Key Decisions

- **Flash the text, not the backlight** — text on/off looks more professional;
  backlight flashing looks cheap.

- **Line 1 only, line 0 blank** — visually centres the message on a 2-line
  display. Line 0 left blank rather than showing partial info.

- **500 ms flash period** — fast enough to feel responsive, slow enough to read
  comfortably.

- **2000 ms minimum display time** — prevents the startup screen from
  disappearing in a single flash if the sensor responds quickly (~1750 ms
  naturally). Adds at most ~250 ms delay in the best case.

- **Sensor state still tracked during startup** — relay and sensor flags update
  normally; only LCD output is suppressed. No state is lost.

- **Button presses during startup are accepted** — setpoint updates and EEPROM
  writes proceed normally; LCD just doesn't reflect them until after the
  transition.

- **`firstReadDone` set before read result is processed** — placed immediately
  after `conversionRequested = false`, ensuring it is set on both success and
  failure paths with a single assignment.

---

## Resolved Questions

- *Should there be a minimum display time?*
  Yes — 2000 ms. Prevents a confusing flash-and-gone if sensor is fast.

- *Should line 1 show the saved setpoint during startup?*
  No — keep it blank for a cleaner, focused startup message.

- *Should button presses be blocked during startup?*
  No — setpoint still adjustable, LCD update just deferred.

- *Flash the text or the backlight?*
  Text. Backlight flash looks like a hardware fault.

- *Should a static message be used instead of a flash?*
  No. Static looks frozen; flash communicates active initialisation.

---

## Files to Change

| File | Change |
|------|--------|
| `config.h` | Add `STARTUP_MIN_MS 2000UL` and `FLASH_PERIOD_MS 500UL` |
| `main.c` | Add `firstRead`, `firstReadDone`, `startupTime`, `lastFlash`, `flashVisible` state; startup flash block in main loop; `firstReadDone = true` after first read attempt; all in-loop `update_lcd()` calls guarded with `if (!firstRead)` |
