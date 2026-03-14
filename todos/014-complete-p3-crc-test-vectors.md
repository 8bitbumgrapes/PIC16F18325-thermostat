---
status: pending
priority: p3
issue_id: "014"
tags: [code-review, onewire, quality]
---

# CRC-8 implementation has no test vectors

## Problem Statement

The 1-Wire CRC-8 in `onewire.c` uses polynomial 0x31 (reflected as 0x8C). The
implementation is correct but there are no test vectors in comments or code to
confirm it at a glance. A subtle bug (wrong poly, wrong init, wrong direction)
would silently accept corrupt sensor data.

## Findings

**File:** `thermostat/onewire.c`, lines 26–38

The algorithm is correct per Dallas/Maxim Application Note 27, but this is
asserted by inspection only.

## Proposed Solution

Add a known-good test vector as a comment:

```c
/* CRC-8 (Dallas/Maxim 1-Wire)
 * Polynomial: 0x31 (x^8+x^5+x^4+1), reflected form: 0x8C
 * Init: 0x00
 *
 * Test vector (DS18B20 scratchpad example):
 *   Data:  0x50 0x05 0x4B 0x46 0x7F 0xFF 0x0C 0x10
 *   CRC:   0x1C  (byte 8 of scratchpad should equal this)
 *   crc8_update(0x00, data, 8) == 0x1C
 */
```

Optionally add a `static_assert` or startup self-test function (only in debug builds).

**Effort:** Small (comment only for basic; small for self-test)
**Risk:** None

## Acceptance Criteria

- [ ] At least one known-good test vector documented in a comment above `crc8_update()`

## Work Log

- 2026-03-14: Identified by security review agent
