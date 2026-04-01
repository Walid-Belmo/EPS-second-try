/* =============================================================================
 * main.c
 * Smoke test: blink PB10 LED with ~3 second period, toggle with PB11 button.
 * Proves the entire build-flash-startup-GPIO chain works.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: PORT group B
 * Pins: PB10 (user LED, active low), PB11 (user button, active low)
 * Clock: default 1 MHz (OSC8M / 8, no DFLL48M configured yet)
 * =============================================================================
 */

#include "samd21g17d.h"
#include <stdint.h>

#define USER_LED_PIN_NUMBER     10u   /* PB10 on Curiosity Nano */
#define USER_BUTTON_PIN_NUMBER  11u   /* PB11 on Curiosity Nano */

/* Number of 20ms polling intervals that make up one blink half-period.
 * 75 intervals * 20ms = 1500ms = 1.5 seconds per half (3 seconds full). */
#define BLINK_HALF_PERIOD_IN_POLL_INTERVALS  75u

/* ── Private function prototypes ──────────────────────────────────────────── */

static void configure_pb10_as_gpio_output_for_user_led(void);
static void configure_pb11_as_gpio_input_for_user_button(void);
static void toggle_user_led(void);
static uint8_t read_user_button_is_pressed(void);
static void wait_approximately_20_milliseconds(void);

/* ── Public entry point ───────────────────────────────────────────────────── */

int main(void)
{
    configure_pb10_as_gpio_output_for_user_led();
    configure_pb11_as_gpio_input_for_user_button();

    uint8_t button_was_pressed_last_poll = 0u;
    uint32_t polls_since_last_blink_toggle = 0u;

    while (1) /* @non-terminating@ */
    {
        /* ── Poll button ──────────────────────────────────────────────── */
        uint8_t button_is_pressed_now = read_user_button_is_pressed();

        if ((button_is_pressed_now == 1u) &&
            (button_was_pressed_last_poll == 0u))
        {
            /* Falling edge detected — button just pressed. Toggle LED.
             * Reset the blink counter so the blink rhythm restarts from
             * the new LED state rather than immediately overriding it. */
            toggle_user_led();
            polls_since_last_blink_toggle = 0u;
        }

        button_was_pressed_last_poll = button_is_pressed_now;

        /* ── Blink ────────────────────────────────────────────────────── */
        polls_since_last_blink_toggle += 1u;

        if (polls_since_last_blink_toggle >= BLINK_HALF_PERIOD_IN_POLL_INTERVALS)
        {
            toggle_user_led();
            polls_since_last_blink_toggle = 0u;
        }

        /* ── Wait one polling interval (~20ms) ────────────────────────── */
        wait_approximately_20_milliseconds();
    }

    return 0; /* never reached on embedded hardware */
}

/* ── Private functions ────────────────────────────────────────────────────── */

static void configure_pb10_as_gpio_output_for_user_led(void)
{
    /* Set PB10 as output. DIRSET sets direction bits without touching others. */
    PORT_REGS->GROUP[1].PORT_DIRSET = (1u << USER_LED_PIN_NUMBER);

    /* Start with LED off: drive PB10 high (active low LED means
     * high = off, low = on). */
    PORT_REGS->GROUP[1].PORT_OUTSET = (1u << USER_LED_PIN_NUMBER);
}

static void configure_pb11_as_gpio_input_for_user_button(void)
{
    /* PB11 is an input by default after reset (DIRCLR is the power-on state).
     * Enable the internal pull-up so the pin reads high when the button is
     * not pressed, and low when the button connects it to ground. */
    PORT_REGS->GROUP[1].PORT_PINCFG[USER_BUTTON_PIN_NUMBER] =
        PORT_PINCFG_INEN_Msk |    /* enable input buffer so PORT_IN reads work */
        PORT_PINCFG_PULLEN_Msk;   /* enable internal pull resistor */

    /* When PULLEN is set, the pull direction is controlled by OUT:
     * OUT=1 → pull-up, OUT=0 → pull-down. We want pull-up. */
    PORT_REGS->GROUP[1].PORT_OUTSET = (1u << USER_BUTTON_PIN_NUMBER);
}

static void toggle_user_led(void)
{
    /* OUTTGL atomically flips the output state of the specified pin. */
    PORT_REGS->GROUP[1].PORT_OUTTGL = (1u << USER_LED_PIN_NUMBER);
}

static uint8_t read_user_button_is_pressed(void)
{
    /* The button connects PB11 to ground when pressed. With pull-up enabled,
     * pressed = pin reads 0, released = pin reads 1. */
    uint32_t port_input_value = PORT_REGS->GROUP[1].PORT_IN;
    uint8_t pin_is_low = ((port_input_value & (1u << USER_BUTTON_PIN_NUMBER)) == 0u);
    return pin_is_low;
}

static void wait_approximately_20_milliseconds(void)
{
    /* Busy-wait loop. At 1 MHz default clock (OSC8M / 8), approximately 20ms.
     *
     * At 1 MHz with -O0: ~166,667 loop iterations per second
     * (loop body is ~6 instructions due to volatile).
     * 166,667 * 0.020 = ~3,333 iterations for 20ms.
     *
     * This also serves as the button debounce interval — mechanical
     * switch bounce settles within 5-10ms, so 20ms is sufficient. */
    volatile uint32_t count = 3333u;
    while (count > 0u)
    {
        count -= 1u;
    }
}
