/* =============================================================================
 * pwm_buck_converter.h
 * Board-selected PWM API for the EPS buck-converter demo path.
 * =============================================================================
 */

#ifndef PWM_BUCK_CONVERTER_H
#define PWM_BUCK_CONVERTER_H

#include <stdint.h>

void pwm_buck_converter_initialize_for_demo(void);

void pwm_buck_converter_set_duty_cycle(
    uint16_t duty_cycle_as_fraction_of_65535);

#endif /* PWM_BUCK_CONVERTER_H */
