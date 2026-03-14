---
status: pending
priority: p1
issue_id: "001"
tags: [code-review, eeprom, interrupt-safety]
---

# EEPROM: GIE re-enabled before write cycle completes

## Problem Statement

In `eeprom_float.c`, `ei()` (re-enable global interrupts) is called immediately after
setting `NVMCON1bits.WR = 1`, before the hardware write cycle finishes. This means an
interrupt can fire while the EEPROM write is still in progress.

## Findings

**File:** `thermostat/eeprom_float.c`, lines 46–53

Current code:
```c
di();
NVMCON2 = 0x55u;
NVMCON2 = 0xAAu;
NVMCON1bits.WR = 1;
ei();                          // ← interrupts re-enabled HERE
while (NVMCON1bits.WR) { }    // ← write still in progress
NVMCON1bits.WREN = 0;
```

The Timer1 ISR only increments `ms_ticks`, so in practice this causes no observable
corruption on the current hardware. However, the pattern is incorrect: any future ISR
that touches NVM registers (e.g., if a watchdog or debug ISR is added) could interact
with an in-progress write cycle.

## Proposed Solution

Move `ei()` to after the WR poll completes:

```c
di();
NVMCON2 = 0x55u;
NVMCON2 = 0xAAu;
NVMCON1bits.WR = 1;
while (NVMCON1bits.WR) { }    // wait for write to complete
ei();                          // ← re-enable interrupts AFTER write done
NVMCON1bits.WREN = 0;
```

**Effort:** Small (2-line change)
**Risk:** None — the write cycle is typically ~4 ms; Timer1 ISR latency is not affected.

## Acceptance Criteria

- [ ] `ei()` called only after `while (NVMCON1bits.WR)` loop exits
- [ ] All 4 bytes of a float write complete before interrupts re-enabled
- [ ] WREN cleared after each byte write

## Work Log

- 2026-03-14: Identified by security review agent
