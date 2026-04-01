/* =============================================================================
 * driver_For_Generating_PWM_for_Buck_Converter.h
 * Complementary PWM driver for the EPC2152 GaN half-bridge buck converter.
 *
 * Generates two complementary 300 kHz PWM signals with hardware dead-time
 * insertion on TCC0:
 *   PA18 (TCC0 WO[2]) → EPC2152 HSin (high-side gate)
 *   PA20 (TCC0 WO[6]) → EPC2152 LSin (low-side gate)
 *
 * Dead time (~42 ns) is enforced in hardware by TCC0's DTI unit.
 * Even if the CPU crashes, the dead time is maintained.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: TCC0, PORT (pins PA18 and PA20)
 * Clock: GCLK0 (48 MHz) → TCC0, no prescaler
 * =============================================================================
 */

#ifndef DRIVER_FOR_GENERATING_PWM_FOR_BUCK_CONVERTER_H
#define DRIVER_FOR_GENERATING_PWM_FOR_BUCK_CONVERTER_H

#include <stdint.h>

/* Initialize TCC0 for complementary PWM at 300 kHz with hardware dead-time.
 * After this call, both outputs are LOW (0% duty cycle).
 * Call pwm_set_buck_converter_duty_cycle() to start switching. */
void pwm_initialize_tcc0_complementary_300khz_with_dead_time(void);

/* Set the buck converter duty cycle.
 *
 * duty_cycle_as_fraction_of_65535:
 *   0     = fully off (both outputs LOW, no switching)
 *   32768 = 50% duty cycle
 *   65535 = maximum duty (clamped to ~94% for safety)
 *
 * Values below ~5% are clamped up to the minimum safe operating point.
 * The update takes effect at the next PWM cycle boundary (glitch-free). */
void pwm_set_buck_converter_duty_cycle(
    uint16_t duty_cycle_as_fraction_of_65535);

#endif /* DRIVER_FOR_GENERATING_PWM_FOR_BUCK_CONVERTER_H */
