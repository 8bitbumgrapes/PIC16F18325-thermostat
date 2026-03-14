---
status: pending
priority: p2
issue_id: "009"
tags: [code-review, quality, documentation]
---

# Startup state machine lacks explanatory comment

## Problem Statement

The startup screen logic relies on three interrelated flags (`firstRead`,
`firstReadDone`, `startupTime`) with an implicit state machine. The transition
condition is split across the variable declarations (lines 86–90) and the main
loop block (lines 285–296), with no comment explaining the overall state machine.

## Findings

**File:** `thermostat/main.c`, lines 85–90

```c
static bool     firstRead            = true;
static bool     firstReadDone        = false;
static uint32_t startupTime          = 0;
static uint32_t lastFlash            = 0;
static bool     flashVisible         = true;
```

A future developer (or future-you) reading these declarations has no immediate
context for the valid states and transition conditions.

## Proposed Solution

Add a block comment above the startup state variables:

```c
/* Startup screen state machine
 *   State STARTUP  (firstRead == true):
 *     - Flash " Please wait... " on LCD line 1 every FLASH_PERIOD_MS
 *     - Relay and sensor logic run normally; only LCD output is suppressed
 *     - Button presses accepted; EEPROM writes proceed; LCD update deferred
 *   Transition to NORMAL (firstRead = false) when:
 *     - firstReadDone == true  (first DS18B20 conversion cycle completed)
 *     - AND (now - startupTime >= STARTUP_MIN_MS)  (minimum 2 s elapsed)
 *   State NORMAL (firstRead == false):
 *     - All update_lcd() calls active; standard thermostat display
 */
```

**Effort:** Small (comment only)
**Risk:** None

## Acceptance Criteria

- [ ] Block comment added above startup state variable declarations

## Work Log

- 2026-03-14: Identified by architecture review agent
