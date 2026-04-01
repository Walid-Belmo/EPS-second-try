/* =============================================================================
 * main.c
 * SAFE BLINK — no clock change, no UART, no DMA.
 * Uses default 1 MHz clock. Blinks LED on PB10 with ~3 second period.
 * This is a recovery build to confirm the board works after erase.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: PORT group B
 * Pins: PB10 (user LED, active low)
 * Clock: default 1 MHz (OSC8M / 8)
 * =============================================================================
 */

#include "samd21g17d.h"
#include <stdint.h>

#define USER_LED_PIN_NUMBER  10u

static void configure_pb10_as_gpio_output_for_user_led(void);
static void toggle_user_led(void);
static void wait_approximately_20_milliseconds_at_1mhz(void);

int main(void)
{
    configure_pb10_as_gpio_output_for_user_led();

    uint32_t blink_counter = 0u;

    while (1) /* @non-terminating@ */
    {
        blink_counter += 1u;
        if (blink_counter >= 75u)
        {
            toggle_user_led();
            blink_counter = 0u;
        }
        wait_approximately_20_milliseconds_at_1mhz();
    }

    return 0;
}

static void configure_pb10_as_gpio_output_for_user_led(void)
{
    PORT_REGS->GROUP[1].PORT_DIRSET = (1u << USER_LED_PIN_NUMBER);
    PORT_REGS->GROUP[1].PORT_OUTSET = (1u << USER_LED_PIN_NUMBER);
}

static void toggle_user_led(void)
{
    PORT_REGS->GROUP[1].PORT_OUTTGL = (1u << USER_LED_PIN_NUMBER);
}

static void wait_approximately_20_milliseconds_at_1mhz(void)
{
    volatile uint32_t count = 3333u;
    while (count > 0u)
    {
        count -= 1u;
    }
}
