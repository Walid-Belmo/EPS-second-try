# SAMD21G17D Clock System

The SAMD21 clock system is more complex than most microcontrollers. Understanding
it is mandatory because a misconfigured clock causes peripherals to silently fail.

---

## Clock Architecture Overview

There are three layers:

```
Layer 1: Clock Sources
  DFLL48M     — 48 MHz, locked to a reference (this is what we use for CPU)
  OSC8M       — 8 MHz internal RC oscillator (chip default at startup)
  OSCULP32K   — 32.768 kHz ultra-low-power oscillator (always on)
  XOSC32K     — 32.768 kHz external crystal (if fitted — not on Curiosity Nano)

Layer 2: Generic Clock Generators (GCLK0..8)
  Each generator takes a source and can divide it.
  GCLK0 — the CPU clock. Must be configured for 48 MHz.
  GCLK1 — commonly used for WDT and RTC (32.768 kHz)
  GCLK2..8 — available for peripheral use

Layer 3: Generic Clock Channels
  Each peripheral has one or more clock channels.
  A channel connects a GCLK generator to a specific peripheral.
  Example: SERCOM5_GCLK_ID_CORE connects to SERCOM5's functional clock.
  A channel must be enabled and connected to a generator before the peripheral works.
```

The CPU always runs on GCLK0. Peripherals each have their own clock channel that
must be explicitly connected to a GCLK generator.

Source: SAMD21 datasheet, Section 15 (GCLK — Generic Clock Controller)

---

## What SystemInit() Does (and Why You Must Not Call It Twice)

`startup/system_samd21.c` contains `SystemInit()` which is called by Reset_Handler
before `main()`. It performs the minimum clock setup needed to run at 48 MHz:

1. Sets flash wait states: `NVMCTRL->CTRLB.bit.RWS = 1`
   — Required before running above 24 MHz. If omitted, flash reads can return
   garbage at high speeds.

2. Configures the DFLL48M in open-loop mode locked to OSCULP32K.

3. Switches GCLK0 to DFLL48M → CPU now runs at 48 MHz.

You never call `SystemInit()` yourself. It runs once before `main()`. Do not
duplicate its register writes in your code or you risk undefined state.

---

## What You Must Configure in main()

SystemInit only configures GCLK0 (CPU clock). Everything else is your responsibility.

For each peripheral you use, you must:

**Step 1: Enable the APB bus clock** (lets the CPU write to the peripheral's registers)

```c
// For SERCOM5:
PM->APBCMASK.reg |= PM_APBCMASK_SERCOM5;

// For DMAC:
PM->AHBMASK.reg  |= PM_AHBMASK_DMAC;
PM->APBBMASK.reg |= PM_APBBMASK_DMAC;
```

**Step 2: Connect a GCLK generator to the peripheral's clock channel**

```c
// Connect GCLK0 (48 MHz) to SERCOM5 functional clock:
GCLK->CLKCTRL.reg =
    GCLK_CLKCTRL_CLKEN        |
    GCLK_CLKCTRL_GEN_GCLK0    |
    GCLK_CLKCTRL_ID(SERCOM5_GCLK_ID_CORE);

// CRITICAL: this write crosses a clock domain boundary.
// Wait for synchronisation before proceeding.
while (GCLK->STATUS.bit.SYNCBUSY);
```

Only after both steps can you configure the peripheral itself.

---

## SYNCBUSY — The Most Common Source of Silent Bugs

Many register writes in the SAMD21 cross between different clock domains.
The CPU runs in one clock domain. The peripheral operates in another.
A write must propagate across the boundary before it takes effect.

If you proceed before the write has propagated, the register silently reverts to
its previous value. Your peripheral appears configured but does not work. There
is no error flag. No warning. It simply does not work.

The rule: after any register write that involves clock domain crossing, check
the relevant SYNCBUSY bit and wait for it to clear.

Common SYNCBUSY locations:

```c
while (GCLK->STATUS.bit.SYNCBUSY);              // after GCLK->CLKCTRL writes
while (SERCOM5->USART.SYNCBUSY.bit.SWRST);      // after software reset
while (SERCOM5->USART.SYNCBUSY.bit.CTRLB);      // after CTRLB write
while (SERCOM5->USART.SYNCBUSY.bit.ENABLE);     // after ENABLE write
```

Every SYNCBUSY wait in the codebase must have a comment explaining what breaks
if the wait is omitted. This is a conventions.md requirement.

Source: SAMD21 datasheet, Section 15.8.2 (GCLK Synchronisation)
Source: Established convention confirmed in community practice:
        https://community.element14.com/products/roadtest/rv/roadtest_reviews/510/sam_d21_curiosity_na

---

## Baud Rate Calculation for SERCOM UART

For a UART configured with 16x oversampling (the default):

```
BAUD = 65536 × (1 − 16 × (baud_rate / f_ref))
```

For 115200 baud at 48 MHz:

```
BAUD = 65536 × (1 − 16 × (115200 / 48000000))
     = 65536 × (1 − 0.03840)
     = 65536 × 0.96160
     = 63019
```

Resulting timing error: < 0.01%. UART tolerates up to approximately 3% error.

```c
SERCOM5->USART.BAUD.reg = 63019;
```

This value only holds if the SERCOM functional clock is exactly 48 MHz
(GCLK0 connected as above). If you connect a different GCLK generator or
configure a divider, recalculate.

Source: SAMD21 datasheet, Section 26.10.10 (SERCOM BAUD Register)

---

## Things to Be Careful About

**Clock order matters.** Flash wait states must be increased before the CPU
clock is raised. Raising the clock first and setting wait states second causes
the CPU to fetch instructions faster than flash can respond. The result is
reading garbage from flash — silent, unpredictable, extremely hard to debug.
SystemInit handles this correctly. Do not rearrange its sequence.

**GCLK write safety.** The GCLK peripheral uses a single shared CLKCTRL register
to configure all clock channels. Writes are not atomic with respect to the clock
domains. Always wait for SYNCBUSY after each write before issuing another.

**Peripheral clock independence.** Just because GCLK0 is at 48 MHz does not mean
all peripherals run at 48 MHz. Each peripheral's functional clock is independent.
A SERCOM will not generate correct baud rates unless its GCLK channel is explicitly
connected to an appropriately configured generator. Forgetting this step is one of
the most common bring-up mistakes.

**The WDT and sleep.** The watchdog timer is clocked by GCLK1 (typically OSCULP32K
at 32.768 kHz). If you enter standby sleep mode and the watchdog is running,
the WDT continues counting. If your sleep period is longer than the WDT timeout,
the chip resets before it wakes. You must either pet the watchdog before sleeping,
set the timeout longer than the sleep period, or disable the WDT before sleep
and re-enable after wake.
