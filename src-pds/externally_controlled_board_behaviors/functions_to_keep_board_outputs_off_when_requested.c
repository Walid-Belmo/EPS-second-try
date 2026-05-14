#include "functions_to_keep_board_outputs_off_when_requested.h"

#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"

void run_off_mode(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    request_no_pwm_output_in_runtime_state(runtime_state);
}

void run_full_satellite_logic(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /*
     * Flight mode is intentionally electrically quiet in this first PDS layer.
     * The real sensor-reading path has not been rebuilt here yet.
     */
    request_no_pwm_output_in_runtime_state(runtime_state);
}
