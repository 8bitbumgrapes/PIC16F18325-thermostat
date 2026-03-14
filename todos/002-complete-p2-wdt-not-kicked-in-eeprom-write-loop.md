---
status: pending
priority: p2
issue_id: "002"
tags: [code-review, wdt, eeprom]
---

# EEPROM write busy-wait doesn't kick WDT

## Problem Statement

`eeprom_write_byte()` spins on `NVMCON1bits.WR` for ~4 ms per byte (~16 ms total for a
float). The WDT is only cleared at the top of the main loop. While 16 ms is well within
the 4.7 s WDT budget, adding `CLRWDT()` inside the write loop is defensive best practice.

## Findings

**File:** `thermostat/eeprom_float.c`, line 53

```c
while (NVMCON1bits.WR) {
    /* spin — WDT not kicked here */
}
```

Currently safe: 16 ms << 4.7 s WDT. Risk escalates if future changes add multiple
consecutive EEPROM writes (e.g., saving a full settings struct of N floats).

## Proposed Solution

```c
while (NVMCON1bits.WR) {
    CLRWDT();
}
```

**Effort:** Small (1-line change)
**Risk:** None

## Acceptance Criteria

- [ ] `CLRWDT()` called inside the WR busy-wait loop in `eeprom_write_byte()`

## Work Log

- 2026-03-14: Identified by security + performance review agents
