# 03 — Debugger Stress Test (Button Interrupt + DMA Logging)

This sample demonstrates:
- **EIC interrupt on PB11** (user button, falling edge, with hardware filter)
- **DMA-based debug logging from ISR context** (proves the circular buffer handles
  concurrent writes from both main loop and interrupt handler)
- All three debug log functions: DEBUG_LOG_TEXT, DEBUG_LOG_UINT, DEBUG_LOG_INT

## What It Does

On boot, it logs system info (clock speed, reset cause) and signed number tests.
Then it enters a heartbeat loop that blinks the LED and logs a counter every 500ms.

Pressing the button triggers an EIC interrupt that logs "BUTTON PRESSED" with an
incrementing press count. The button logs interleave with heartbeat logs without
corruption.

## How the Button Interrupt Works

PB11 is the user button on the DM320119 (active low — pressed = LOW).

1. **APB clock**: `PM_REGS->PM_APBAMASK |= PM_APBAMASK_EIC_Msk`
2. **GCLK**: Connect GCLK0 (48 MHz) to EIC via `GCLK_CLKCTRL` with `ID(EIC_GCLK_ID)`.
   Edge detection requires GCLK_EIC — without it, falling edge sensing does not work.
3. **Pin config**: Set PMUXEN + INEN + PULLEN on PB11. The internal pull-up holds
   the pin HIGH when the button is not pressed. Set pull direction via OUTSET.
4. **Pin mux**: PB11 is odd pin → PMUXO (upper nibble) at PMUX index 5. Mux A (0x00)
   routes to EIC EXTINT11.
5. **EIC CONFIG**: EXTINT11 uses CONFIG[1], SENSE3 field (index 3 = 11-8).
   SENSE3=FALL for falling edge. FILTEN3=1 for hardware debounce filter.
6. **Enable**: Set INTENSET for EXTINT11, enable EIC (CTRL.ENABLE), wait SYNCBUSY,
   then NVIC_EnableIRQ(EIC_IRQn).

## Expected Serial Output (COM6, 115200 baud)

```
=== BOOT OK ===
CPU clock Hz: 48000000
reset cause: 64
test negative: -42
test zero: 0
test positive: 1234
press the button to test interrupts
heartbeat: 1
heartbeat: 2
BUTTON PRESSED
press count: 1
heartbeat: 3
...
```
