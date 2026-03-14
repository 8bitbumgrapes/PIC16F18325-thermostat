---
status: pending
priority: p2
issue_id: "005"
tags: [code-review, startup, ux]
---

# Startup screen loops indefinitely if sensor never responds

## Problem Statement

`firstReadDone` is set at `main.c:375`, which only executes after a conversion
completes (`conversionRequested = false`). If `ds18b20_start_conversion()` fails
every time (no sensor), `conversionRequested` is never set to true, so the code
never reaches line 375, and `firstReadDone` stays false indefinitely.

The result: the "Please wait..." screen flashes forever with no way to dismiss it.

## Findings

**File:** `thermostat/main.c`, lines 353–375

```c
if (!ds18b20_start_conversion()) {
    // error path — conversionRequested stays false
    // firstReadDone is NEVER set on this path
    return;  // (continue)
}
conversionRequested = true;
...
// 750ms later:
conversionRequested = false;
firstReadDone = true;   // ← only reached if conversion was requested
```

## Proposed Solution

Set `firstReadDone = true` on the sensor-not-found error path as well:

```c
if (!ds18b20_start_conversion()) {
    sensorError = true;
    set_relay(false);
    firstReadDone = true;    // ← allow startup screen to transition
    if (!firstRead) update_lcd(0.0f);
    continue;
}
```

**Effort:** Small (1-line addition)
**Risk:** Low — firstReadDone is a one-way flag; setting it early on error is correct.

## Acceptance Criteria

- [ ] `firstReadDone = true` set on the no-sensor error path (line ~362)
- [ ] Startup screen transitions to normal display (showing "No Sensor!") after 2000ms even with no sensor connected

## Work Log

- 2026-03-14: Identified by security review agent
