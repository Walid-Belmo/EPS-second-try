#include <stdint.h>

#include "eps_state_machine.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "state_machine_pure_logic/functions_to_compute_next_pcu_state_and_actuator_commands_from_pure_logic.h"
#include "externally_controlled_board_behaviors/functions_to_run_power_state_machine_with_injected_sensor_values.h"

static void build_state_machine_inputs_from_injected_values(
    const pds_runtime_state_type *runtime_state,
    struct eps_sensor_readings_this_iteration *sensor_readings);
static uint16_t convert_panel_voltage_to_raw_adc(
    uint16_t panel_voltage_in_millivolts);
static uint16_t convert_panel_current_to_raw_adc(
    uint16_t panel_current_in_milliamps);
static uint8_t build_load_enable_mask(
    const struct eps_actuator_output_commands *actuator_commands);
static uint16_t choose_state_demo_pwm_request(
    const struct eps_actuator_output_commands *actuator_commands);
static void store_state_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    const struct eps_actuator_output_commands *actuator_commands);

void run_state_transition_test_only(void)
{
    if (pds_control_loop_period_has_elapsed() == 0u)
    {
        return;
    }

    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    struct eps_sensor_readings_this_iteration sensor_readings;
    struct eps_actuator_output_commands actuator_commands;

    build_state_machine_inputs_from_injected_values(
        runtime_state,
        &sensor_readings);
    compute_next_pcu_state_and_actuator_commands_from_inputs_and_current_state(
        &runtime_state->state_machine_state,
        &sensor_readings,
        &runtime_state->thresholds,
        &actuator_commands);

    request_pwm_output_in_runtime_state(
        runtime_state,
        choose_state_demo_pwm_request(&actuator_commands));
    store_state_demo_result_for_status(runtime_state, &actuator_commands);
}

static void build_state_machine_inputs_from_injected_values(
    const pds_runtime_state_type *runtime_state,
    struct eps_sensor_readings_this_iteration *sensor_readings)
{
    const pds_state_demo_inputs_type *inputs =
        &runtime_state->injected_state_inputs;

    sensor_readings->battery_voltage_in_millivolts =
        inputs->battery_voltage_in_millivolts;
    sensor_readings->battery_current_in_milliamps =
        inputs->battery_current_in_milliamps;
    sensor_readings->solar_array_voltage_in_millivolts =
        inputs->panel_voltage_in_millivolts;
    sensor_readings->solar_array_voltage_raw_adc_reading =
        convert_panel_voltage_to_raw_adc(inputs->panel_voltage_in_millivolts);
    sensor_readings->solar_array_current_raw_adc_reading =
        convert_panel_current_to_raw_adc(inputs->panel_current_in_milliamps);
    sensor_readings->charging_rail_voltage_in_millivolts =
        inputs->charging_rail_voltage_in_millivolts;
    sensor_readings->battery_temperature_in_decidegrees_celsius =
        inputs->battery_temperature_in_decidegrees_celsius;
    sensor_readings->obc_heartbeat_received_this_iteration =
        inputs->heartbeat_received;
    sensor_readings->satellite_mode_commanded_by_obc =
        inputs->satellite_mode_from_obc;
    sensor_readings->safe_mode_sub_state_commanded_by_obc =
        inputs->safe_mode_substate_from_obc;
}

static uint16_t convert_panel_voltage_to_raw_adc(
    uint16_t panel_voltage_in_millivolts)
{
    uint16_t raw_adc =
        (uint16_t)(panel_voltage_in_millivolts
        / PDS_PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE);

    return (raw_adc > 4095u) ? 4095u : raw_adc;
}

static uint16_t convert_panel_current_to_raw_adc(
    uint16_t panel_current_in_milliamps)
{
    uint16_t raw_adc =
        (uint16_t)(panel_current_in_milliamps
        / PDS_PANEL_RAW_ADC_TO_MILLIAMPS_SCALE);

    return (raw_adc > 4095u) ? 4095u : raw_adc;
}

static uint8_t build_load_enable_mask(
    const struct eps_actuator_output_commands *actuator_commands)
{
    uint8_t load_enable_mask = 0u;

    for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_COUNT;
         load_index += 1u)
    {
        if (actuator_commands->load_enable_flags[load_index] != 0u)
        {
            load_enable_mask |= (uint8_t)(1u << load_index);
        }
    }

    return load_enable_mask;
}

static uint16_t choose_state_demo_pwm_request(
    const struct eps_actuator_output_commands *actuator_commands)
{
    if (actuator_commands->panel_efuse_should_be_enabled == 0u)
    {
        return 0u;
    }

    return actuator_commands->buck_converter_duty_cycle_as_fraction_of_65535;
}

static void store_state_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    const struct eps_actuator_output_commands *actuator_commands)
{
    runtime_state->snapshot.loop_count += 1u;
    runtime_state->snapshot.panel_voltage_in_millivolts =
        runtime_state->injected_state_inputs.panel_voltage_in_millivolts;
    runtime_state->snapshot.panel_current_in_milliamps =
        runtime_state->injected_state_inputs.panel_current_in_milliamps;
    runtime_state->snapshot.panel_power_in_milliwatts =
        ((uint32_t)runtime_state->snapshot.panel_voltage_in_millivolts
         * (uint32_t)runtime_state->snapshot.panel_current_in_milliamps)
        / 1000u;
    runtime_state->snapshot.state_machine_duty_cycle_as_fraction_of_65535 =
        actuator_commands->buck_converter_duty_cycle_as_fraction_of_65535;
    runtime_state->snapshot.pcu_mode =
        actuator_commands->current_pcu_mode_for_telemetry;
    runtime_state->snapshot.safe_mode_is_active =
        runtime_state->state_machine_state.safe_mode_is_active;
    runtime_state->snapshot.safe_mode_reason =
        actuator_commands->safe_mode_alert_reason;
    runtime_state->snapshot.panel_efuse_is_enabled =
        actuator_commands->panel_efuse_should_be_enabled;
    runtime_state->snapshot.heater_is_enabled =
        actuator_commands->heater_should_be_enabled;
    runtime_state->snapshot.safe_mode_alert_for_obc =
        actuator_commands->safe_mode_alert_flag_for_obc;
    runtime_state->snapshot.load_enable_mask =
        build_load_enable_mask(actuator_commands);
}
