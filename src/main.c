/* =============================================================================
 * main.c
 * Application entry point. Initializes hardware, starts the DMA-based debug
 * logging system, and runs the main LED heartbeat loop.
 *
 * Category: APPLICATION
 * Peripheral: PORT group B (LED)
 * Pins: PB10 (user LED, active low)
 * Clock: 48 MHz (DFLL48M open-loop)
 * =============================================================================
 */

#include "samd21g17d.h"
#include <stdint.h>
#include "clock_configure_48mhz_dfll_open_loop.h"
#include "debug_functions.h"

#define USER_LED_PIN_NUMBER  10u

/* ── Private function prototypes ──────────────────────────────────────────── */

static void configure_pb10_as_gpio_output_for_user_led(void);
static void toggle_user_led(void);
static void wait_approximately_500_milliseconds_at_48mhz(void);

/* ── Public entry point ───────────────────────────────────────────────────── */

int main(void)
{
    configure_cpu_clock_to_48mhz_using_dfll_open_loop();
    configure_pb10_as_gpio_output_for_user_led();
    DEBUG_LOG_INIT();

    DEBUG_LOG_TEXT("BOOT OK");

    while (1) /* @non-terminating@ */
    {
        toggle_user_led();
        DEBUG_LOG_TEXT("blink");
        wait_approximately_500_milliseconds_at_48mhz();
    }

    return 0;
}

/* ── Private functions — LED ──────────────────────────────────────────────── */

static void configure_pb10_as_gpio_output_for_user_led(void)
{
    PORT_REGS->GROUP[1].PORT_DIRSET = (1u << USER_LED_PIN_NUMBER);
    PORT_REGS->GROUP[1].PORT_OUTSET = (1u << USER_LED_PIN_NUMBER);
}

static void toggle_user_led(void)
{
    PORT_REGS->GROUP[1].PORT_OUTTGL = (1u << USER_LED_PIN_NUMBER);
}

/* ── Private functions — delay ────────────────────────────────────────────── */

static void wait_approximately_500_milliseconds_at_48mhz(void)
{
    volatile uint32_t count = 4000000u;
    while (count > 0u)
    {
        count -= 1u;
    }
}
