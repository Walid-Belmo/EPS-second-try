#include <stdint.h>

#include "eps_state_machine.h"
#include "functions_to_apply_manually_requested_pwm_to_buck_converter.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"

void run_fixed_pwm_test_only(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    request_pwm_output_in_runtime_state(
        runtime_state,
        runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535);
    runtime_state->snapshot.pcu_mode = (uint8_t)EPS_PCU_MODE_MPPT_CHARGE;
    runtime_state->snapshot.panel_efuse_is_enabled =
        (runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535 > 0u)
        ? 1u
        : 0u;
}
