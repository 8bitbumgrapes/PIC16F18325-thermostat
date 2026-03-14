---
status: pending
priority: p2
issue_id: "006"
tags: [code-review, quality, simplicity]
---

# Redundant sensorOk/sensorError dual-boolean state

## Problem Statement

Two booleans track sensor state: `sensorOk` and `sensorError`. They represent three
valid states (unknown, ok, error) but use 4-bit storage with one invalid combination
(both true). The code must always update both together to keep them consistent.

## Findings

**File:** `thermostat/main.c`, lines 74–75, 383–384, 399–400

```c
static bool sensorOk = false;    // always set alongside sensorError
static bool sensorError = false; // always set alongside sensorOk

// Must update both:
sensorOk    = true;  sensorError = false;   // on success (lines 399-400)
sensorError = true;                          // on error (line 383) — sensorOk not cleared here
```

Note: `sensorOk` is only cleared via `ow_init()` path; the dual-write invariant is
maintained in practice but not enforced by the type system.

## Proposed Solution

Replace with a single enum:

```c
typedef enum { SENSOR_UNKNOWN = 0, SENSOR_OK, SENSOR_ERROR } sensor_state_t;
static sensor_state_t sensor_state = SENSOR_UNKNOWN;
```

Update all read/write sites (approximately 6 locations in `main.c` and `update_lcd()`).

**Effort:** Medium (refactor ~6 sites, update `update_lcd()` conditionals)
**Risk:** Low — straightforward replacement, behavior preserved

## Acceptance Criteria

- [ ] `sensorOk` and `sensorError` removed
- [ ] `sensor_state_t` enum replaces them
- [ ] All conditional checks updated to use `sensor_state`
- [ ] `update_lcd()` updated to use enum

## Work Log

- 2026-03-14: Identified by simplicity review agent
