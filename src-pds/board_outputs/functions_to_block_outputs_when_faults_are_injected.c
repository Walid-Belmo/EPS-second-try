#include "functions_to_block_outputs_when_faults_are_injected.h"

#include "command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"
#include "functions_to_store_requested_pwm_output_before_safety_checks.h"

void block_dangerous_outputs(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    if (runtime_state->injected_state_inputs.fault_flags == 0u)
    {
        return;
    }

    request_no_pwm_output_in_runtime_state(runtime_state);
    runtime_state->snapshot.safe_mode_is_active = 1u;
    runtime_state->snapshot.safe_mode_alert_for_obc = 1u;
    runtime_state->snapshot.safe_mode_reason = PDS_SAFE_REASON_INJECTED_FAULT;
}
