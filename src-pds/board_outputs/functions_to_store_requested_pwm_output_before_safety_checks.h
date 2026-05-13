#ifndef FUNCTIONS_TO_STORE_REQUESTED_PWM_OUTPUT_BEFORE_SAFETY_CHECKS_H
#define FUNCTIONS_TO_STORE_REQUESTED_PWM_OUTPUT_BEFORE_SAFETY_CHECKS_H

#include <stdint.h>

#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"

void request_pwm_output_in_runtime_state(
    pds_runtime_state_type *runtime_state,
    uint16_t duty_cycle_as_fraction_of_65535);
void request_no_pwm_output_in_runtime_state(
    pds_runtime_state_type *runtime_state);

#endif /* FUNCTIONS_TO_STORE_REQUESTED_PWM_OUTPUT_BEFORE_SAFETY_CHECKS_H */
