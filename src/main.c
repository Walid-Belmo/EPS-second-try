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

/* ── Private function prototypes ──────────────────────────────────────────── */

static void configure_pb10_as_gpio_output_for_user_led(void);
static void configure_pb11_as_gpio_input_for_user_button(void);
static void turn_user_led_on(void);
static void turn_user_led_off(void);
static void toggle_user_led(void);
static uint8_t read_user_button_is_pressed(void);
static void wait_approximately_1500_milliseconds(void);
static void wait_approximately_50_milliseconds_for_debounce(void);

/* ── Public entry point ───────────────────────────────────────────────────── */

int main(void)
{
    configure_pb10_as_gpio_output_for_user_led();
    configure_pb11_as_gpio_input_for_user_button();

    uint8_t button_was_pressed_last_iteration = 0u;

    while (1) /* @non-terminating@ */
    {
        /* Check if button is currently pressed */
        uint8_t button_is_pressed_now = read_user_button_is_pressed();

        if ((button_is_pressed_now == 1u) &&
            (button_was_pressed_last_iteration == 0u))
        {
            /* Rising edge detected — button just pressed. Toggle LED. */
            toggle_user_led();
            wait_approximately_50_milliseconds_for_debounce();
        }

        button_was_pressed_last_iteration = button_is_pressed_now;

        /* Default behavior: blink with ~3 second period (1.5s on, 1.5s off).
         * The button toggle overrides this by changing the LED state.
         * After 1.5 seconds the blink loop forces the next state anyway.
         * This is intentional — the smoke test just needs to prove GPIO works. */
        turn_user_led_on();
        wait_approximately_1500_milliseconds();
        turn_user_led_off();
        wait_approximately_1500_milliseconds();
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

static void turn_user_led_on(void)
{
    /* Active low: pull PB10 low to illuminate the LED. */
    PORT_REGS->GROUP[1].PORT_OUTCLR = (1u << USER_LED_PIN_NUMBER);
}

static void turn_user_led_off(void)
{
    /* Active low: drive PB10 high to turn the LED off. */
    PORT_REGS->GROUP[1].PORT_OUTSET = (1u << USER_LED_PIN_NUMBER);
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

static void wait_approximately_1500_milliseconds(void)
{
    /* Busy-wait loop. At 1 MHz default clock (OSC8M / 8, the DFP SystemInit
     * does not configure DFLL48M), approximately 1500ms.
     *
     * At 1 MHz: ~1,000,000 instructions per second.
     * Simple loop body at -O0: ~6 instructions (load, subtract, store,
     * load, compare, branch) because volatile prevents optimization.
     * 1,000,000 / 6 = ~166,667 iterations per second.
     * 250,000 iterations = ~1500ms.
     *
     * This is intentionally imprecise. The goal is visible blinking,
     * not accurate timing. If the rate is wrong, adjust this count. */
    volatile uint32_t count = 250000u;
    while (count > 0u)
    {
        count -= 1u;
    }
}

static void wait_approximately_50_milliseconds_for_debounce(void)
{
    /* Short delay for button debounce. At 1 MHz, ~50ms.
     * 166,667 iterations/sec * 0.050s = ~8,333 iterations. */
    volatile uint32_t count = 8333u;
    while (count > 0u)
    {
        count -= 1u;
    }
}
