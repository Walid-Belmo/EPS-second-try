/* =============================================================================
 * millisecond_tick_timer_using_arm_systick.c
 * Provides a millisecond counter that tracks elapsed time since boot.
 * Uses the ARM Cortex-M0+ SysTick timer — a 24-bit countdown timer built
 * into every ARM core (not a SAMD21 peripheral). It fires an interrupt
 * every 1 millisecond and increments a counter. This gives the firmware
 * a way to measure real elapsed time without blocking the CPU.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: ARM SysTick (core timer, not a SAMD21 SERCOM/TC/TCC)
 * Clock: Uses CPU clock (48 MHz), counts down 48000 ticks per interrupt
 * Interrupt: SysTick_Handler — fires every 1 ms, increments counter
 *
 * How SysTick works:
 *   1. You load a value (48000) into the RELOAD register.
 *   2. SysTick counts down from 48000 to 0, one tick per CPU clock cycle.
 *   3. At 48 MHz, 48000 ticks = exactly 1 millisecond.
 *   4. When it reaches 0, it fires the SysTick_Handler interrupt and
 *      automatically reloads back to 48000. This repeats forever.
 *
 * The SysTick_Handler ISR is declared as a weak alias to Dummy_Handler
 * in startup_samd21g17d.c (line 64). Defining our own SysTick_Handler
 * here overrides the weak alias — the linker picks our version.
 * =============================================================================
 */

#include <stdint.h>

#include "samd21g17d.h"
#include "millisecond_tick_timer_using_arm_systick.h"

/* ── Constants ────────────────────────────────────────────────────────────── */

/* SysTick counts down this many CPU clock cycles per interrupt.
 * 48,000,000 Hz / 48,000 ticks = 1,000 interrupts per second = 1 ms each.
 * The SysTick RELOAD register is 24 bits wide (max 16,777,215).
 * 48,000 fits easily within this limit. */
#define SYSTICK_RELOAD_VALUE_FOR_1MS_AT_48MHZ  48000u

/* ── Module state ─────────────────────────────────────────────────────────── */

/* volatile because the ISR writes this and the main loop reads it.
 * Without volatile, the compiler may cache the value in a register
 * and the main loop would never see updates from the ISR. */
static volatile uint32_t milliseconds_elapsed_since_boot = 0u;

/* ── Interrupt handler ────────────────────────────────────────────────────── */

/* SysTick_Handler fires every 1 millisecond. It increments the counter
 * and returns. This is the entire ISR — 1 line of real work. */
void SysTick_Handler(void);

void SysTick_Handler(void)
{
    milliseconds_elapsed_since_boot += 1u;
}

/* ── Public functions ─────────────────────────────────────────────────────── */

void millisecond_tick_timer_initialize_at_48mhz(void)
{
    /* SysTick_Config() is a CMSIS function provided in core_cm0plus.h.
     * It does three things:
     *   1. Sets the RELOAD register to (value - 1) = 47999
     *   2. Sets SysTick priority to the lowest urgency
     *   3. Enables the SysTick counter and its interrupt
     *
     * After this call, SysTick_Handler fires every 1 ms automatically.
     * No NVIC_EnableIRQ needed — SysTick is a core exception, not a
     * peripheral interrupt, so it is enabled by the SysTick control
     * register directly (not through the NVIC). */
    (void)SysTick_Config(SYSTICK_RELOAD_VALUE_FOR_1MS_AT_48MHZ);
}

uint32_t millisecond_tick_timer_get_milliseconds_since_boot(void)
{
    return milliseconds_elapsed_since_boot;
}
