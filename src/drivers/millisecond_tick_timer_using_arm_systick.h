/* =============================================================================
 * millisecond_tick_timer_using_arm_systick.h
 * Provides a millisecond counter that tracks elapsed time since boot.
 *
 * Uses the ARM Cortex-M0+ SysTick timer — a 24-bit countdown timer built
 * into every ARM core (not a SAMD21 peripheral like SERCOM or TCC). It fires
 * an interrupt every 1 millisecond and increments a counter. This gives the
 * firmware a way to measure real elapsed time without blocking the CPU.
 *
 * The millisecond counter wraps after ~49.7 days (2^32 milliseconds).
 * Use unsigned subtraction for elapsed time checks — this handles wrap
 * correctly as long as the interval is less than ~24.8 days:
 *
 *   uint32_t elapsed = now - previous;  // correct even if now < previous
 *   if (elapsed >= 120000u) { ... }     // 120 seconds
 * =============================================================================
 */

#ifndef MILLISECOND_TICK_TIMER_USING_ARM_SYSTICK_H
#define MILLISECOND_TICK_TIMER_USING_ARM_SYSTICK_H

#include <stdint.h>

/* Configure the ARM SysTick timer to fire every 1 millisecond.
 * Must be called AFTER the CPU clock is running at 48 MHz. Calling this
 * before clock configuration produces an incorrect tick rate because
 * SysTick counts CPU clock cycles. */
void millisecond_tick_timer_initialize_at_48mhz(void);

/* Return the number of milliseconds elapsed since boot.
 * The caller should read this into a local variable for use in comparisons,
 * because the volatile counter can change between reads. */
uint32_t millisecond_tick_timer_get_milliseconds_since_boot(void);

#endif /* MILLISECOND_TICK_TIMER_USING_ARM_SYSTICK_H */
