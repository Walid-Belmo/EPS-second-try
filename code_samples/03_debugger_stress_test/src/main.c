/* =============================================================================
 * main.c
 * Debugger stress test. Exercises all three logging functions (TEXT, UINT, INT),
 * tests logging from both main loop and ISR contexts, and verifies the button
 * interrupt (EIC EXTINT11 on PB11).
 *
 * Category: APPLICATION / TEST
 * Peripheral: PORT group B (LED, button), EIC (EXTINT11), SERCOM5 (UART via DMA)
 * Pins: PB10 (user LED), PB11 (user button, active low), PA22 (UART TX)
 * Clock: 48 MHz (DFLL48M open-loop)
 * =============================================================================
 */

#include "samd21g17d.h"
#include <stdint.h>
#include "clock_configure_48mhz_dfll_open_loop.h"
#include "debug_functions.h"

#define USER_LED_PIN_NUMBER    10u
#define USER_BUTTON_PIN_NUMBER 11u

/* ── Module state ────────────────────────────────────────────────────────── */

static volatile uint32_t button_press_count = 0u;

/* ── ISR prototype (public — called by NVIC, needs prototype for -Wmissing) */

void EIC_Handler(void);

/* ── Private function prototypes ──────────────────────────────────────────── */

static void configure_pb10_as_gpio_output_for_user_led(void);
static void toggle_user_led(void);
static void configure_pb11_button_interrupt_on_extint11(void);
static void wait_approximately_500_milliseconds_at_48mhz(void);

/* ── Public entry point ───────────────────────────────────────────────────── */

int main(void)
{
    configure_cpu_clock_to_48mhz_using_dfll_open_loop();
    configure_pb10_as_gpio_output_for_user_led();
    DEBUG_LOG_INIT();
    configure_pb11_button_interrupt_on_extint11();

    /* ── Boot info ─────────────────────────────────────────────────────── */

    DEBUG_LOG_TEXT("=== BOOT OK ===");
    DEBUG_LOG_UINT("CPU clock Hz", SystemCoreClock);
    DEBUG_LOG_UINT("reset cause", PM_REGS->PM_RCAUSE);

    /* ── Signed number test ────────────────────────────────────────────── */

    DEBUG_LOG_INT("test negative", -42);
    DEBUG_LOG_INT("test zero", 0);
    DEBUG_LOG_INT("test positive", 1234);

    DEBUG_LOG_TEXT("press the button to test interrupts");

    /* ── Heartbeat loop ────────────────────────────────────────────────── */

    uint32_t heartbeat_count = 0u;

    while (1) /* @non-terminating@ */
    {
        toggle_user_led();
        heartbeat_count += 1u;
        DEBUG_LOG_UINT("heartbeat", heartbeat_count);
        wait_approximately_500_milliseconds_at_48mhz();
    }

    return 0;
}

/* ── ISR — button press on EXTINT11 ──────────────────────────────────────── */

void EIC_Handler(void)
{
    /* Check that this interrupt is from EXTINT11 (PB11 button). */
    if ((EIC_REGS->EIC_INTFLAG & EIC_INTFLAG_EXTINT11_Msk) != 0u)
    {
        /* Clear the interrupt flag IMMEDIATELY. If not cleared before
         * returning, the interrupt fires again instantly. */
        EIC_REGS->EIC_INTFLAG = EIC_INTFLAG_EXTINT11_Msk;

        button_press_count += 1u;
        DEBUG_LOG_TEXT("BUTTON PRESSED");
        DEBUG_LOG_UINT("press count", button_press_count);
    }
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

/* ── Private functions — button interrupt ─────────────────────────────────── */

static void configure_pb11_button_interrupt_on_extint11(void)
{
    /* 1. Enable EIC bus clock on APB A bus. Without this, writes to
     *    EIC registers are silently ignored. */
    PM_REGS->PM_APBAMASK |= PM_APBAMASK_EIC_Msk;

    /* 2. Connect GCLK0 (48 MHz) to EIC functional clock.
     *    The EIC needs a clock source for edge detection and the
     *    digital glitch filter. Without it, edge sensing does not work. */
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk     |
        GCLK_CLKCTRL_GEN_GCLK0     |
        GCLK_CLKCTRL_ID(EIC_GCLK_ID);

    while ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for GCLK synchronization */
    }

    /* 3. Configure PB11 for EIC EXTINT11 (mux A).
     *    PB11 is an odd pin, so we write the PMUXO (upper nibble) at
     *    PMUX index 11/2 = 5. Mux A = 0x00.
     *    INEN enables the input buffer so the EIC can sample the pin.
     *    PULLEN enables the internal pull-up (direction set via OUTSET). */
    PORT_REGS->GROUP[1].PORT_PINCFG[USER_BUTTON_PIN_NUMBER] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk | PORT_PINCFG_PULLEN_Msk;

    /* Set pull direction to UP (button pulls LOW when pressed). */
    PORT_REGS->GROUP[1].PORT_OUTSET = (1u << USER_BUTTON_PIN_NUMBER);

    PORT_REGS->GROUP[1].PORT_PMUX[5] =
        (PORT_REGS->GROUP[1].PORT_PMUX[5] & 0x0Fu)
        | (PORT_PMUX_PMUXO_A_Val << 4u);

    /* 4. Configure EXTINT11 in CONFIG[1] register.
     *    EXTINT 8–15 use CONFIG[1]. EXTINT11 is at index 3 within CONFIG[1]
     *    (11 - 8 = 3). Each EXTINT uses 4 bits: 3 for SENSE + 1 for FILTEN.
     *    SENSE3 = FALL (0x2): detect falling edge (button press = HIGH→LOW).
     *    FILTEN3 = 1: enable hardware glitch filter for debounce. */
    EIC_REGS->EIC_CONFIG[1] =
        (EIC_REGS->EIC_CONFIG[1]
         & ~(EIC_CONFIG_SENSE3_Msk | EIC_CONFIG_FILTEN3_Msk))
        | EIC_CONFIG_SENSE3_FALL
        | EIC_CONFIG_FILTEN3_Msk;

    /* 5. Enable the EXTINT11 interrupt in the EIC. */
    EIC_REGS->EIC_INTENSET = EIC_INTENSET_EXTINT11_Msk;

    /* 6. Enable the EIC peripheral. Configuration registers must be
     *    written BEFORE enabling (they are locked while enabled). */
    EIC_REGS->EIC_CTRL = EIC_CTRL_ENABLE_Msk;
    while ((EIC_REGS->EIC_STATUS & EIC_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for EIC enable synchronization */
    }

    /* 7. Enable the EIC interrupt in the NVIC (ARM interrupt controller). */
    NVIC_EnableIRQ(EIC_IRQn);
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
