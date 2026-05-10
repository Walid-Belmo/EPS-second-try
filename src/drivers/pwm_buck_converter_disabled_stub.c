/* =============================================================================
 * pwm_buck_converter_disabled_stub.c
 * No-op PWM implementation for builds that must not drive buck hardware yet.
 * =============================================================================
 */

#include <stdint.h>

#include "pwm_buck_converter.h"

void pwm_buck_converter_initialize_for_demo(void)
{
}

void pwm_buck_converter_set_duty_cycle(
    uint16_t duty_cycle_as_fraction_of_65535)
{
    (void)duty_cycle_as_fraction_of_65535;
}
