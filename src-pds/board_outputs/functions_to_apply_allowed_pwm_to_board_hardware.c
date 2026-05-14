#include "functions_to_apply_allowed_pwm_to_board_hardware.h"

#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "pwm_buck_converter.h"

void apply_outputs_to_board(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    pwm_buck_converter_set_duty_cycle(
        runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535);
}
