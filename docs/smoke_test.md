# Smoke Test — LED Blink

The smoke test is the very first program you flash. Its only job is to prove
that the entire chain works: compiler → linker → flasher → chip → your code.

---

## Why Start Here

Before writing a single line of application code, you need to know that:

1. The compiler produces valid ARM Cortex-M0+ machine code
2. The linker script places the vector table at 0x00000000
3. The linker script places code and data at the correct addresses
4. OpenOCD can erase, write, and verify the flash
5. The chip's Reset_Handler runs (startup_samd21.c)
6. SystemInit() runs (system_samd21.c) and the chip reaches 48 MHz
7. Your `main()` is called
8. GPIO registers respond to writes

If any of these eight things is broken, nothing else will work. The LED blink
tests all of them simultaneously in the simplest possible way.

If the LED blinks, every item in that list is confirmed working.
If it does not blink, the failure is somewhere in that list and you find it
before writing thousands of lines of code that all behave mysteriously.

---

## The Hardware

On the DM320119 Curiosity Nano:
- User LED: connected to **PB10** (PORT group B, pin 10)
- The LED is **active low**: setting PB10 LOW turns the LED ON, HIGH turns it OFF

Source: DM320119 User Guide, Table 4-1 LED Connection

---

## The Minimal main.c

```c
// main.c — smoke test
// Goal: blink PB10 LED at approximately 1 Hz.
// If this blinks, the build system, flash, startup code, and GPIO all work.

#include "samd21g17d.h"
#include <stdint.h>

#define USER_LED_PIN_NUMBER  10u   // PB10 on Curiosity Nano

static void configure_pb10_as_gpio_output_for_user_led(void) {
    // Set PB10 as output. DIRSET sets bits without touching others.
    PORT->Group[1].DIRSET.reg = (1u << USER_LED_PIN_NUMBER);
    // Start with LED off: drive PB10 high (active low LED).
    PORT->Group[1].OUTSET.reg = (1u << USER_LED_PIN_NUMBER);
}

static void turn_user_led_on(void) {
    // Active low: pull PB10 low to illuminate the LED.
    PORT->Group[1].OUTCLR.reg = (1u << USER_LED_PIN_NUMBER);
}

static void turn_user_led_off(void) {
    PORT->Group[1].OUTSET.reg = (1u << USER_LED_PIN_NUMBER);
}

static void wait_approximately_500_milliseconds(void) {
    // Busy-wait loop. At 48 MHz (set by SystemInit), approximately 500ms.
    // This is intentionally imprecise — the goal is visible blinking,
    // not accurate timing. Replace with a timer-based delay in later phases.
    //
    // At 48 MHz: ~48 million instructions per second.
    // Simple loop body: ~3 instructions (decrement, compare, branch).
    // 48,000,000 / 3 = ~16,000,000 iterations per second.
    // 8,000,000 iterations ≈ 500ms.
    volatile uint32_t count = 8000000u;
    while (count > 0u) {
        count -= 1u;
    }
}

int main(void) {
    configure_pb10_as_gpio_output_for_user_led();

    while (1) /* @non-terminating@ */ {
        turn_user_led_on();
        wait_approximately_500_milliseconds();
        turn_user_led_off();
        wait_approximately_500_milliseconds();
    }

    return 0; // never reached on embedded hardware
}
```

Note: `PORT->Group[1]` is Port B. `PORT->Group[0]` is Port A. This is a common
source of confusion. Always verify the group index before wiring port references.

---

## Build and Flash

```bash
# From inside MSYS2 MINGW64 terminal, in the project root:
make flash
```

Expected output ending with:
```
** Verified OK **
** Resetting Target **
```

---

## What to Observe

The yellow LED on the board blinks at approximately 1 Hz (on 500ms, off 500ms).

If the blink rate is wildly different from 1 Hz, the CPU is not running at 48 MHz.
Check that SystemInit() was compiled and linked (it must be in SRCS in the Makefile).

---

## Pass Criterion

The LED blinks visibly and continuously. This is the only criterion.
You do not need a precise 1 Hz. You need visible, regular blinking.

Once this passes, proceed to Phase 2 (DMA UART logging).

---

## Things to Be Careful About

**Port group index.** PB10 is in `PORT->Group[1]` (Port B). Writing to
`PORT->Group[0]` (Port A) does nothing visible for this LED.

**Active low confusion.** OUTSET drives the pin HIGH (LED off). OUTCLR drives
the pin LOW (LED on). This is the opposite of what most people expect on first
encounter.

**The volatile keyword on the counter.** Without `volatile`, the compiler
optimizes the busy-wait loop away entirely (it detects that the loop has no
observable effect). The LED never blinks. This is one of the most common
mistakes in embedded bare-metal C and a good reason to prefer timer-based delays
as soon as possible.

**Clock frequency assumption.** The delay is calibrated for 48 MHz. If
`system_samd21.c` is not linked, the chip runs on the default 8 MHz RC oscillator
(OSC8M divided by 8 = 1 MHz) and the blink will be 48 times slower — about one
blink every 48 seconds. This is a useful diagnostic: very slow blinking means
the clock setup did not run.
