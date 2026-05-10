// =============================================================================
// main_mainboard_blink_pb22.c
//
// First bring-up firmware (Firmware A) for the EPS PCU testing board V4.1.
// This program does the smallest useful thing the chip can do: it brings the
// CPU up to 48 MHz and then toggles the green status LED on PB22 forever at
// approximately 1 Hz. If the LED blinks visibly after flashing, every link
// in the chain — toolchain, build, programmer, SWD wiring, board power,
// chip identity, startup code, clock configuration, GPIO — is working.
//
// BUILD TARGET: mainboard only
//   Compiled when `make BOARD=mainboard`. The dev-board has no LED on PB22
//   (its user LED is PB10), so this file is NOT compiled when BOARD=devboard.
//   For the dev-board's blink-and-button stress test, see src/main.c.
//
// The code intentionally does NOT use any UART, any timer, any DMA, any
// interrupt, any peripheral other than PORT. Every additional peripheral
// would be one more thing to misconfigure on first power-up.
//
// Category: APPLICATION (test)
// Peripheral: PORT group B (LED2 output on PB22)
// Pins: PB22 (GPIO output, drives LED2 active-HIGH through R52 = 750 ohm)
// Clock: 48 MHz (DFLL48M open-loop)
// Interrupt: none
//
// Reference for the pinout: docs/mainboard_pinout_pcu_v4_1.md
// =============================================================================

#include <stdint.h>

#include "samd21j17d.h"

#include "clock_configure_48mhz_dfll_open_loop.h"
#include "led_status_pb22_active_high_on_mainboard.h"

// ── Private function prototypes ──────────────────────────────────────────────

static void wait_approximately_500_milliseconds_at_48mhz(void);

// ── Public entry point ───────────────────────────────────────────────────────

int main(void)
{
    configure_cpu_clock_to_48mhz_using_dfll_open_loop();
    configure_pb22_as_gpio_output_for_status_led_on_mainboard();

    while (1) /* @non-terminating@ */
    {
        toggle_pb22_status_led_on_mainboard();
        wait_approximately_500_milliseconds_at_48mhz();
    }

    return 0;
}

// ── Private functions — delay ────────────────────────────────────────────────

static void wait_approximately_500_milliseconds_at_48mhz(void)
{
    // Burns CPU cycles in a tight busy-wait. We do NOT use SysTick or any
    // timer here on purpose: the goal of this firmware is to be the smallest
    // possible "is the chip alive?" test, so adding a timer peripheral would
    // mean one more thing to misconfigure. The trade-off is that the timing
    // is approximate (depends on optimisation level, exact instruction
    // pipelining, and flash wait-states), but visible-eye blink rates do not
    // need to be precise.
    //
    // 4,000,000 iterations of this loop at 48 MHz with -O0 lands close to
    // 500 ms wall time. Matches the existing dev-board firmware's busy-wait
    // (src/main.c, function wait_approximately_500_milliseconds_at_48mhz)
    // so we get the same blink rate the dev-board team is used to seeing.
    volatile uint32_t loop_iteration_counter = 4000000u;
    while (loop_iteration_counter > 0u)
    {
        loop_iteration_counter -= 1u;
    }
}
