---
status: pending
priority: p3
issue_id: "016"
tags: [code-review, hardware, validation]
---

# Hardware validation: 5 integration scenarios pending physical build

## Problem Statement

The firmware is feature-complete and software-reviewed, but has not been validated
on real hardware. Five integration scenarios must be confirmed before deployment.

## Findings

From `docs/plans/2026-03-14-002-feat-pic16f18325-thermostat-port-plan.md`:

The plan includes one unchecked acceptance criterion:
```
- [ ] Hardware validation complete (Scenarios 1–5 above — pending physical build)
```

## Scenarios to Validate

| # | Scenario | Key check |
|---|----------|-----------|
| 1 | Power-on with sensor connected | Full startup, flash, sensor read, relay control |
| 2 | Power-on without sensor | Error path, relay stays OFF, "No Sensor!" on LCD |
| 3 | Temperature crosses 30°C | Safety cutoff activates; relay forced OFF; "CUTOFF" on LCD |
| 4 | Button presses during startup | Setpoint adjusts, EEPROM written, persists after power cycle |
| 5 | Rapid button mashing | No WDT expiry; I2C bus stable; UART output continuous |

## Acceptance Criteria

- [ ] Scenario 1 passes on hardware
- [ ] Scenario 2 passes on hardware
- [ ] Scenario 3 passes on hardware
- [ ] Scenario 4 passes on hardware
- [ ] Scenario 5 passes on hardware
- [ ] Plan `docs/plans/2026-03-14-002-*` acceptance criterion checked off

## Work Log

- 2026-03-14: Identified as outstanding item by learnings-researcher agent
