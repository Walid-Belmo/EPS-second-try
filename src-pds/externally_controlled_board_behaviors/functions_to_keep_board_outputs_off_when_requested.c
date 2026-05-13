#include "functions_to_keep_board_outputs_off_when_requested.h"

#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"

void run_off_mode(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    request_no_pwm_output_in_runtime_state(runtime_state);
}

void run_full_satellite_logic(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    /*
     * Flight mode is intentionally electrically quiet in this first PDS layer.
     * The real sensor-reading path has not been rebuilt here yet.
     */
    request_no_pwm_output_in_runtime_state(runtime_state);
}
