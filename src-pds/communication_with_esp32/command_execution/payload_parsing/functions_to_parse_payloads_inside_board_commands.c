#include <stdint.h>

#include "assertion_handler.h"
#include "eps_state_machine.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"
#include "communication_with_esp32/command_execution/payload_parsing/functions_to_parse_payloads_inside_board_commands.h"
#include "shared_helpers/functions_to_read_and_write_little_endian_values.h"

pds_state_demo_inputs_type read_state_demo_inputs_from_command_payload(
    const uint8_t *payload)
{
    SATELLITE_ASSERT(payload != (void *)0);

    pds_state_demo_inputs_type inputs;
    inputs.battery_voltage_in_millivolts =
        read_uint16_from_little_endian_bytes(&payload[0]);
    inputs.battery_current_in_milliamps =
        read_int16_from_little_endian_bytes(&payload[2]);
    inputs.panel_voltage_in_millivolts =
        read_uint16_from_little_endian_bytes(&payload[4]);
    inputs.panel_current_in_milliamps =
        read_uint16_from_little_endian_bytes(&payload[6]);
    inputs.charging_rail_voltage_in_millivolts =
        read_uint16_from_little_endian_bytes(&payload[8]);
    inputs.battery_temperature_in_decidegrees_celsius =
        read_int16_from_little_endian_bytes(&payload[10]);
    inputs.heartbeat_received = payload[12];
    inputs.satellite_mode_from_obc = payload[13];
    inputs.safe_mode_substate_from_obc = payload[14];
    inputs.fault_flags = read_uint16_from_little_endian_bytes(&payload[15]);
    return inputs;
}

uint8_t state_demo_inputs_are_valid(
    const pds_state_demo_inputs_type *candidate_inputs)
{
    SATELLITE_ASSERT(candidate_inputs != (void *)0);

    if (candidate_inputs->heartbeat_received > 1u)
    {
        return 0u;
    }

    if (candidate_inputs->satellite_mode_from_obc
        > (uint8_t)EPS_SATELLITE_MODE_SAFE)
    {
        return 0u;
    }

    if (candidate_inputs->safe_mode_substate_from_obc
        > (uint8_t)EPS_SAFE_SUB_STATE_REBOOT)
    {
        return 0u;
    }

    return 1u;
}

pds_mppt_demo_curve_type read_mppt_demo_curve_from_command_payload(
    const uint8_t *payload)
{
    SATELLITE_ASSERT(payload != (void *)0);

    pds_mppt_demo_curve_type curve;
    curve.curve_type = payload[0];
    curve.coefficient_a_scaled =
        read_int32_from_little_endian_bytes(&payload[1]);
    curve.coefficient_b_scaled =
        read_int32_from_little_endian_bytes(&payload[5]);
    curve.coefficient_c_in_milliamps =
        read_int16_from_little_endian_bytes(&payload[9]);
    curve.minimum_panel_voltage_in_millivolts =
        read_uint16_from_little_endian_bytes(&payload[11]);
    curve.maximum_panel_voltage_in_millivolts =
        read_uint16_from_little_endian_bytes(&payload[13]);
    curve.battery_voltage_in_millivolts =
        read_uint16_from_little_endian_bytes(&payload[15]);
    return curve;
}

uint8_t mppt_demo_curve_is_valid(
    const pds_mppt_demo_curve_type *candidate_curve)
{
    SATELLITE_ASSERT(candidate_curve != (void *)0);

    if (candidate_curve->curve_type != PDS_CURVE_TYPE_QUADRATIC)
    {
        return 0u;
    }

    if (candidate_curve->minimum_panel_voltage_in_millivolts
        >= candidate_curve->maximum_panel_voltage_in_millivolts)
    {
        return 0u;
    }

    if (candidate_curve->battery_voltage_in_millivolts == 0u)
    {
        return 0u;
    }

    return 1u;
}
