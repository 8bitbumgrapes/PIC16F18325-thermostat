---
status: pending
priority: p2
issue_id: "003"
tags: [code-review, safety, relay-control]
---

# Safety cutoff boundary at 30.0°C may cause relay chatter

## Problem Statement

The safety cutoff uses `>=` at exactly 30.0°C. DS18B20 resolution is 0.0625°C, so
a sensor reading oscillating between 29.9375°C and 30.0°C will toggle the cutoff on
and off every read cycle (every ~1 s), causing relay chatter.

## Findings

**File:** `thermostat/main.c`, line 412; `thermostat/config.h`, line 61

```c
if (tempC >= MAX_SAFE_TEMP) {   // MAX_SAFE_TEMP = 30.0f
    set_relay(false);
    ...
}
```

Setpoint max is 29.0°C with 1.0°C hysteresis, so the setpoint would need to be at
its maximum AND the sensor near 30°C for this to occur. Unlikely in normal use but
possible in a warm environment.

## Proposed Solutions

**Option A — Raise cutoff threshold slightly (simplest):**
Change `MAX_SAFE_TEMP` to 30.5°C to provide 0.5°C margin above the max operable
setpoint of 29.0°C + 1.0°C = 30.0°C.
- Pros: One-constant change, no logic change
- Cons: Slightly less conservative safety margin

**Option B — Add hysteresis to the cutoff:**
```c
#define MAX_SAFE_TEMP       30.0f
#define CUTOFF_RESET_TEMP   29.0f   // relay re-enables below this

static bool cutoffActive = false;

if (tempC >= MAX_SAFE_TEMP) { cutoffActive = true; }
else if (tempC <= CUTOFF_RESET_TEMP) { cutoffActive = false; }

if (cutoffActive) { set_relay(false); ... }
```
- Pros: Proper hysteresis, no relay chatter
- Cons: Adds one flag and two constants

**Recommendation:** Option B — matches the pattern already used for normal relay control.

**Effort:** Small (Option A) / Medium (Option B)

## Acceptance Criteria

- [ ] Relay does not chatter when temperature hovers near 30.0°C
- [ ] Safety cutoff still activates within one read cycle of crossing the threshold

## Work Log

- 2026-03-14: Identified by security review agent
