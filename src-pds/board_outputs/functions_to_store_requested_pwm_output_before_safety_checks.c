#include <stdint.h>

#include "assertion_handler.h"
#include "functions_to_store_requested_pwm_output_before_safety_checks.h"

void request_pwm_output_in_runtime_state(
    pds_runtime_state_type *runtime_state,
    uint16_t duty_cycle_as_fraction_of_65535)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    runtime_state->snapshot.requested_pwm_duty_cycle_as_fraction_of_65535 =
        duty_cycle_as_fraction_of_65535;
    runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535 =
        duty_cycle_as_fraction_of_65535;
    runtime_state->snapshot.pwm_output_is_enabled =
        (duty_cycle_as_fraction_of_65535 > 0u) ? 1u : 0u;
}

void request_no_pwm_output_in_runtime_state(
    pds_runtime_state_type *runtime_state)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    runtime_state->snapshot.requested_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->snapshot.pwm_output_is_enabled = 0u;
    runtime_state->snapshot.panel_efuse_is_enabled = 0u;
}
