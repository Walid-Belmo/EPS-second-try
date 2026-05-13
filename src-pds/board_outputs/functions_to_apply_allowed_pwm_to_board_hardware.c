#include "functions_to_apply_allowed_pwm_to_board_hardware.h"

#include "command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"
#include "pwm_buck_converter.h"

void apply_outputs_to_board(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    pwm_buck_converter_set_duty_cycle(
        runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535);
}
