# 01 — LED Blink + Button Toggle

**Date:** 2026-04-01
**Phase:** Phase 0 — Smoke Test
**Clock:** 1 MHz (default OSC8M / 8, no DFLL48M)

## What it does

- LED on PB10 blinks with ~3 second period (1.5s on, 1.5s off)
- Pressing the button on PB11 instantly toggles the LED and resets the blink rhythm

## Why it exists

First successful program on the board. Proves the entire chain works:
compiler, linker, flasher, startup code (Reset_Handler), and GPIO read/write.

## Key details

- Register API: `PORT_REGS->GROUP[1].PORT_DIRSET` (DFP v3.6.144 style)
- Button uses internal pull-up via `PORT_PINCFG` (INEN + PULLEN)
- Main loop polls every ~20ms using a busy-wait counter
- Blink is driven by a poll counter (75 intervals = 1.5s), not by long delays
- The first version had a bug: the blink loop overwrote the button toggle
  immediately. Fixed by replacing long waits with short polling intervals.

## Files

- `src/main.c` — the complete smoke test
