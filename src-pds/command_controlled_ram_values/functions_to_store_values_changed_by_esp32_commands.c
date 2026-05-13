#include <stdint.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "eps_state_machine.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "mppt_algorithm.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"
#include "command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.h"

static struct pds_runtime_state_storage {
    pds_runtime_state_type values;
} pds_runtime_state_storage;

static void set_default_threshold_values(pds_runtime_state_type *runtime_state);
static void set_default_injected_state_values(
    pds_runtime_state_type *runtime_state);
static void set_default_mppt_curve_values(pds_runtime_state_type *runtime_state);
static void set_default_snapshot_values(pds_runtime_state_type *runtime_state);
static uint8_t requested_mode_matches(uint8_t requested_mode_to_check);

void set_starting_ram_values_to_safe_defaults(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_OFF;
    runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    set_default_injected_state_values(runtime_state);
    set_default_mppt_curve_values(runtime_state);
    set_default_threshold_values(runtime_state);
    set_default_snapshot_values(runtime_state);

    runtime_state->telemetry_stream_is_enabled = 0u;
    runtime_state->telemetry_stream_period_ms = 1000u;
    runtime_state->telemetry_field_mask = PDS_VALUE_FIELD_ALL;
    runtime_state->last_stream_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
    runtime_state->stream_sequence_number = 0u;
    runtime_state->valid_chips_frame_count = 0u;
    runtime_state->crc_error_count = 0u;
    runtime_state->too_long_frame_count = 0u;
    runtime_state->executed_command_count = 0u;
    runtime_state->last_command_id = 0u;
    runtime_state->last_command_status = (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;

    reset_mppt_demo_ram_values_after_new_curve();
    reset_state_demo_ram_values_after_new_inputs();
}

pds_runtime_state_type *get_pointer_to_pds_runtime_ram_values(void)
{
    return &pds_runtime_state_storage.values;
}

uint8_t requested_mode_is_off(void)
{
    return requested_mode_matches((uint8_t)PDS_REQUESTED_MODE_OFF);
}

uint8_t requested_mode_is_flight(void)
{
    return requested_mode_matches((uint8_t)PDS_REQUESTED_MODE_FLIGHT);
}

uint8_t requested_mode_is_mppt_test(void)
{
    return requested_mode_matches((uint8_t)PDS_REQUESTED_MODE_MPPT_TEST);
}

uint8_t requested_mode_is_state_test(void)
{
    return requested_mode_matches((uint8_t)PDS_REQUESTED_MODE_STATE_TEST);
}

uint8_t requested_mode_is_fixed_pwm_test(void)
{
    return requested_mode_matches((uint8_t)PDS_REQUESTED_MODE_FIXED_PWM_TEST);
}

uint8_t pds_control_loop_period_has_elapsed(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();
    uint32_t now_ms = millisecond_tick_timer_get_milliseconds_since_boot();
    uint32_t elapsed_ms = now_ms - runtime_state->last_control_loop_timestamp_ms;

    if (elapsed_ms < PDS_CONTROL_LOOP_PERIOD_MS)
    {
        return 0u;
    }

    runtime_state->last_control_loop_timestamp_ms = now_ms;
    return 1u;
}

void reset_mppt_demo_ram_values_after_new_curve(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    mppt_algorithm_initialize(&runtime_state->mppt_algorithm_state);
    runtime_state->snapshot.mppt_duty_cycle_as_fraction_of_65535 = 32768u;
    runtime_state->last_control_loop_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
}

void reset_state_demo_ram_values_after_new_inputs(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    eps_state_machine_initialize(
        &runtime_state->state_machine_state,
        &runtime_state->thresholds,
        (uint8_t)EPS_PCU_MODE_MPPT_CHARGE);
    runtime_state->snapshot.state_machine_duty_cycle_as_fraction_of_65535 =
        32768u;
    runtime_state->last_control_loop_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
}

void record_valid_chips_frame_from_esp32(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    runtime_state->valid_chips_frame_count += 1u;
}

void record_broken_chips_message_without_changing_outputs(
    chips_parser_result_type parser_result)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    if (parser_result == CHIPS_PARSER_RESULT_ERROR_CRC_MISMATCH)
    {
        runtime_state->crc_error_count += 1u;
    }
    else if (parser_result == CHIPS_PARSER_RESULT_ERROR_FRAME_TOO_LONG)
    {
        runtime_state->too_long_frame_count += 1u;
    }
}

void remember_result_of_command_from_esp32(
    const chips_parsed_frame_type *received_command,
    const chips_parsed_frame_type *reply_to_send)
{
    SATELLITE_ASSERT(received_command != (void *)0);
    SATELLITE_ASSERT(reply_to_send != (void *)0);

    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();
    runtime_state->executed_command_count += 1u;
    runtime_state->last_command_id = received_command->command_id;

    if (reply_to_send->payload_length_in_bytes > 0u)
    {
        runtime_state->last_command_status = reply_to_send->payload_bytes[0];
    }
}

static void set_default_threshold_values(pds_runtime_state_type *runtime_state)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    runtime_state->thresholds.battery_voltage_maximum_in_millivolts = 8400u;
    runtime_state->thresholds.battery_voltage_full_threshold_in_millivolts =
        8300u;
    runtime_state->thresholds.battery_voltage_charge_resume_threshold_in_millivolts =
        8100u;
    runtime_state->thresholds.battery_voltage_minimum_in_millivolts = 5000u;
    runtime_state->thresholds.battery_voltage_critical_in_millivolts = 5500u;
    runtime_state->thresholds.battery_voltage_hysteresis_margin_in_millivolts =
        200u;
    runtime_state->thresholds.battery_current_maximum_charge_in_milliamps =
        2000;
    runtime_state->thresholds.battery_current_maximum_discharge_in_milliamps =
        -2000;
    runtime_state->thresholds.battery_current_minimum_charge_threshold_in_milliamps =
        100;
    runtime_state->thresholds.solar_array_minimum_voltage_for_availability_in_millivolts =
        8200u;
    runtime_state->thresholds.temperature_minimum_for_heater_activation_in_decidegrees =
        -100;
    runtime_state->thresholds.temperature_maximum_for_load_shedding_in_decidegrees =
        600;
    runtime_state->thresholds.temperature_minimum_for_charging_allowed_in_decidegrees =
        0;
    runtime_state->thresholds.mppt_charge_timeout_for_insufficient_buffer_in_iterations =
        1200u;
    runtime_state->thresholds.cv_float_low_voltage_wait_timeout_in_iterations =
        1200u;
    runtime_state->thresholds.obc_heartbeat_timeout_in_iterations = 1200u;
    runtime_state->thresholds.cv_float_duty_cycle_adjustment_step_size = 164u;
}

static void set_default_injected_state_values(
    pds_runtime_state_type *runtime_state)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    runtime_state->injected_state_inputs.battery_voltage_in_millivolts = 7400u;
    runtime_state->injected_state_inputs.battery_current_in_milliamps = 250;
    runtime_state->injected_state_inputs.panel_voltage_in_millivolts = 12000u;
    runtime_state->injected_state_inputs.panel_current_in_milliamps = 2200u;
    runtime_state->injected_state_inputs.charging_rail_voltage_in_millivolts =
        7600u;
    runtime_state->injected_state_inputs.battery_temperature_in_decidegrees_celsius =
        220;
    runtime_state->injected_state_inputs.heartbeat_received = 1u;
    runtime_state->injected_state_inputs.satellite_mode_from_obc =
        (uint8_t)EPS_SATELLITE_MODE_CHARGING;
    runtime_state->injected_state_inputs.safe_mode_substate_from_obc =
        (uint8_t)EPS_SAFE_SUB_STATE_CHARGING;
    runtime_state->injected_state_inputs.fault_flags = 0u;
}

static void set_default_mppt_curve_values(pds_runtime_state_type *runtime_state)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    runtime_state->mppt_curve.curve_type = PDS_CURVE_TYPE_QUADRATIC;
    runtime_state->mppt_curve.coefficient_a_scaled = -12000;
    runtime_state->mppt_curve.coefficient_b_scaled = 300000;
    runtime_state->mppt_curve.coefficient_c_in_milliamps = 2500;
    runtime_state->mppt_curve.minimum_panel_voltage_in_millivolts = 0u;
    runtime_state->mppt_curve.maximum_panel_voltage_in_millivolts = 18000u;
    runtime_state->mppt_curve.battery_voltage_in_millivolts = 7400u;
}

static void set_default_snapshot_values(pds_runtime_state_type *runtime_state)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    runtime_state->snapshot.loop_count = 0u;
    runtime_state->snapshot.panel_voltage_in_millivolts =
        runtime_state->injected_state_inputs.panel_voltage_in_millivolts;
    runtime_state->snapshot.panel_current_in_milliamps =
        runtime_state->injected_state_inputs.panel_current_in_milliamps;
    runtime_state->snapshot.panel_power_in_milliwatts =
        ((uint32_t)runtime_state->snapshot.panel_voltage_in_millivolts
         * (uint32_t)runtime_state->snapshot.panel_current_in_milliamps)
        / 1000u;
    runtime_state->snapshot.mppt_duty_cycle_as_fraction_of_65535 = 32768u;
    runtime_state->snapshot.state_machine_duty_cycle_as_fraction_of_65535 =
        32768u;
    runtime_state->snapshot.requested_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->snapshot.pwm_output_is_enabled = 0u;
    runtime_state->snapshot.pcu_mode = (uint8_t)EPS_PCU_MODE_MPPT_CHARGE;
    runtime_state->snapshot.safe_mode_is_active = 0u;
    runtime_state->snapshot.safe_mode_reason = (uint8_t)EPS_SAFE_REASON_NONE;
    runtime_state->snapshot.panel_efuse_is_enabled = 0u;
    runtime_state->snapshot.heater_is_enabled = 0u;
    runtime_state->snapshot.safe_mode_alert_for_obc = 0u;
    runtime_state->snapshot.load_enable_mask = PDS_LOAD_ENABLE_MASK_ALL_LOADS;
}

static uint8_t requested_mode_matches(uint8_t requested_mode_to_check)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    return (runtime_state->requested_mode == requested_mode_to_check) ? 1u : 0u;
}
