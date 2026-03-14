---
title: "PIC16F18325 embedded C — common pitfalls found in code review"
category: logic-errors
date: 2026-03-14
tags: [pic16f18325, xc8, nvm, eeprom, interrupt, onewire, safety, refactor]
severity: p1-and-p2
modules: [eeprom_float.c, onewire.c, lcd_i2c.c, main.c]
---

# PIC16F18325 embedded C — common pitfalls found in code review

Five patterns identified during a full code review of the thermostat firmware.
Documented here because all five are easy to repeat on any XC8/PIC16 project.

---

## 1 (P1) — NVM unlock sequence: `ei()` before `WR` clears

### Symptom
EEPROM writes appear to succeed but the device occasionally resets or writes
corrupt values, especially when buttons are pressed during a write.

### Root cause
`ei()` was called immediately after setting `NVMCON1bits.WR = 1`, before the
hardware had finished the write cycle (~4 ms). Any interrupt firing during that
window could corrupt the NVM state machine.

```c
/* WRONG — ei() fires while write is still in progress */
di();
NVMCON2 = 0x55u;
NVMCON2 = 0xAAu;
NVMCON1bits.WR = 1;
ei();                       /* ← dangerous: WR not yet cleared */
while (NVMCON1bits.WR) { }
```

### Fix
Keep GIE disabled until `WR` clears. Add `CLRWDT()` inside the busy-wait so
the ~4 ms write duration does not expire the watchdog.

```c
/* CORRECT */
di();
NVMCON2 = 0x55u;
NVMCON2 = 0xAAu;
NVMCON1bits.WR = 1;
while (NVMCON1bits.WR) {
    CLRWDT();   /* kick WDT during ~4 ms write cycle */
}
ei();
NVMCON1bits.WREN = 0;
```

### Prevention
Any time you write the NVM unlock sequence on a PIC, the critical section
must span from `di()` to after `WR` polls low — not just to the WR set.
Check every `eeprom_write_byte` / flash-write helper in the project.

---

## 2 (P2) — Safety cutoff relay chatter at boundary temperature

### Symptom
Relay toggling rapidly when temperature hovers near the safety cutoff
threshold (30 °C). DS18B20 12-bit resolution is 0.0625 °C, so consecutive
reads can straddle the boundary.

### Root cause
A simple `>=` comparison with no hysteresis:

```c
if (tempC >= MAX_SAFE_TEMP) {
    set_relay(false);
    continue;
}
/* otherwise, normal hysteresis control re-enables relay */
```

### Fix
Latch `cutoffActive` on entry; clear only when temp drops below a lower
threshold (`CUTOFF_RESET_TEMP`).

```c
if (tempC >= MAX_SAFE_TEMP) {
    cutoffActive = true;
} else if (cutoffActive && tempC < CUTOFF_RESET_TEMP) {
    cutoffActive = false;
}

if (cutoffActive) {
    set_relay(false);
    ...
    continue;
}
```

`CUTOFF_RESET_TEMP` (29 °C) is defined in `config.h`, 1 °C below
`MAX_SAFE_TEMP` (30 °C).

### Prevention
Any on/off decision based on a threshold that a noisy sensor can straddle
needs hysteresis. The same pattern applies to the normal relay control
(already uses `HYSTERESIS = 1.0f`); apply the same thinking to safety limits.

---

## 3 (P2) — Dual-boolean state machine has invalid states

### Symptom
Two `bool` variables (`sensorOk`, `sensorError`) used to represent sensor
state, creating four possible combinations but only three valid ones. The
`(sensorOk=false, sensorError=false)` initial state is technically distinct
from `(sensorOk=false, sensorError=true)` but code paths treat them the same.

### Fix
Replace with an enum — self-documenting, impossible to reach an invalid state:

```c
typedef enum {
    SENSOR_UNKNOWN = 0,   /* initial state — no read attempted yet */
    SENSOR_OK,            /* last read was valid                    */
    SENSOR_ERROR,         /* read failed; sensor never seen         */
    SENSOR_LOST           /* read failed; sensor was previously OK  */
} sensor_state_t;

static sensor_state_t sensor_state = SENSOR_UNKNOWN;
```

Transitions become explicit:
- `SENSOR_OK` → read fail → `SENSOR_LOST` (reinit bus, show "Sensor Lost!")
- `SENSOR_UNKNOWN/ERROR` → read fail → `SENSOR_ERROR` (show "No Sensor!")
- Any state → valid read → `SENSOR_OK`

### Prevention
When you find yourself with two related booleans, reach for an enum.
The compiler will warn on missing switch cases; booleans won't.

---

## 4 (P2) — 1-Wire read-bit sample point vulnerable to optimisation

### Symptom
Intermittent CRC errors on DS18B20 reads, particularly in optimised builds.

### Root cause
The read-bit sample occurs at nominally 9 µs after the falling edge.
With `__delay_us(3)` after `OW_RELEASE()`, compiler instruction reordering
or variation in the release-to-read path can push the sample outside the
valid window (must be < 15 µs per 1-Wire spec).

```c
OW_RELEASE();
__delay_us(3);     /* 6+3 = 9 µs — but margin is thin */
if (OW_READ()) {
```

### Fix
Two `NOP()` instructions anchor the sample point and survive optimisation:

```c
OW_RELEASE();
__delay_us(3);
NOP(); NOP();      /* anchor sample point ≥ 9 µs from release */
if (OW_READ()) {
```

### Prevention
On bit-bang timing-critical paths, a `NOP()` or two before the sample is
cheap insurance. Always verify 1-Wire timing with a logic analyser when
changing clock frequency or optimisation level.

---

## 5 (P2) — Startup splash stuck forever on persistent sensor absence

### Symptom
If `ds18b20_start_conversion()` fails on every call, `firstReadDone` is
never set quickly enough, and the "Please wait..." screen can persist
longer than expected (it does eventually clear after 750 ms, but the UX
is confusing).

### Root cause
`firstReadDone` was only set in the conversion-complete path (after 750 ms
wait), not in the immediate error path when start_conversion fails:

```c
if (ds18b20_start_conversion()) {
    ...
} else {
    sensorError = true;
    set_relay(false);
    /* firstReadDone never set here → startup stays up for extra 750 ms */
}
```

### Fix
Set `firstReadDone = true` immediately in the error branch:

```c
} else {
    sensor_state  = SENSOR_ERROR;
    firstReadDone = true;   /* unblock startup on persistent no-sensor */
    set_relay(false);
    ...
}
```

### Prevention
When a state flag gates a UI transition, make sure every exit path
(success and all failure modes) sets it. A "startup complete" flag should
be set wherever the startup work can legitimately end — not just on the
happy path.

---

## Files changed

| File | Todos resolved |
|------|---------------|
| `thermostat/eeprom_float.c` | 001 (P1), 002 (P2) |
| `thermostat/onewire.c` | 004 (P2) |
| `thermostat/lcd_i2c.c` | 010 (P2) — magic delays → named constants |
| `thermostat/main.c` | 003, 005, 006, 007, 008, 009, 011 (all P2) |
| `thermostat/config.h` | 003, 008, 011 (constants added) |

Commit: `fix: apply all P1/P2 code review findings (todos 001–011)`
