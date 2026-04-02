// =============================================================================
// eps_state_machine.c
// Complete EPS power management state machine for the CHESS CubeSat.
//
// Implements:
//   - PCU modes: MPPT_CHARGE, CV_FLOAT, SA_LOAD_FOLLOW, BATTERY_DISCHARGE
//   - Safe mode with 4 sub-states and per-sub-state load shedding
//   - Thermal control: heater, overtemperature, charging prohibition <0C
//   - Load shedding: priority-based, one load per iteration
//   - OBC heartbeat monitoring with 120s autonomy timeout
//
// This module calls mppt_algorithm_run_one_iteration() from mppt_algorithm.c
// when Incremental Conductance tracking is needed. It does NOT reimplement
// or modify the MPPT algorithm.
//
// Category: PURE LOGIC (no hardware)
// =============================================================================

#include <stdint.h>

#include "assertion_handler.h"
#include "mppt_algorithm.h"
#include "eps_configuration_parameters.h"
#include "eps_state_machine.h"

// ── Private helper: clamp duty cycle to safe operating range ────────────────

static uint16_t clamp_duty_cycle_to_safe_operating_range(
    uint16_t unclamped_duty_cycle)
{
    if (unclamped_duty_cycle < MPPT_MINIMUM_DUTY_CYCLE) {
        return MPPT_MINIMUM_DUTY_CYCLE;
    }
    if (unclamped_duty_cycle > MPPT_MAXIMUM_DUTY_CYCLE) {
        return MPPT_MAXIMUM_DUTY_CYCLE;
    }
    return unclamped_duty_cycle;
}

// ── Private helper: increase duty cycle by a specified step ─────────────────

static uint16_t increase_duty_cycle_by_specified_step_size(
    uint16_t current_duty_cycle,
    uint16_t step_size)
{
    uint32_t new_value = (uint32_t)current_duty_cycle + (uint32_t)step_size;
    if (new_value > MPPT_MAXIMUM_DUTY_CYCLE) {
        return MPPT_MAXIMUM_DUTY_CYCLE;
    }
    return (uint16_t)new_value;
}

// ── Private helper: decrease duty cycle by a specified step ─────────────────

static uint16_t decrease_duty_cycle_by_specified_step_size(
    uint16_t current_duty_cycle,
    uint16_t step_size)
{
    if (current_duty_cycle <= (MPPT_MINIMUM_DUTY_CYCLE + step_size)) {
        return MPPT_MINIMUM_DUTY_CYCLE;
    }
    return (uint16_t)(current_duty_cycle - step_size);
}

// ── Private helper: check if solar array voltage is above minimum ───────────

static uint8_t check_if_solar_array_voltage_is_above_minimum_operating_threshold(
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds)
{
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);
    SATELLITE_ASSERT(eps_configuration_thresholds != (void *)0);

    uint8_t solar_is_available =
        (sensor_readings_this_iteration->solar_array_voltage_in_millivolts
         >= eps_configuration_thresholds->solar_array_minimum_voltage_for_availability_in_millivolts)
        ? 1u : 0u;

    return solar_is_available;
}

// ── Private helper: update OBC heartbeat counter ────────────────────────────

static void update_obc_heartbeat_iteration_counter(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);

    if (sensor_readings_this_iteration->obc_heartbeat_received_this_iteration == 1u) {
        eps_persistent_state->iterations_since_last_obc_heartbeat = 0u;
    } else {
        eps_persistent_state->iterations_since_last_obc_heartbeat += 1u;
    }
}

// ── Private helper: check safe mode entry conditions ────────────────────────

static void check_for_safe_mode_entry_conditions_and_set_alert_flag(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);
    SATELLITE_ASSERT(eps_configuration_thresholds != (void *)0);
    SATELLITE_ASSERT(actuator_commands_output != (void *)0);

    // Battery below minimum voltage
    if (sensor_readings_this_iteration->battery_voltage_in_millivolts
        < eps_configuration_thresholds->battery_voltage_minimum_in_millivolts)
    {
        eps_persistent_state->safe_mode_is_active = 1u;
        actuator_commands_output->safe_mode_alert_flag_for_obc = 1u;
        actuator_commands_output->safe_mode_alert_reason =
            (uint8_t)EPS_SAFE_REASON_BATTERY_BELOW_MINIMUM;
        return;
    }

    // Temperature outside safe range
    if ((sensor_readings_this_iteration->battery_temperature_in_decidegrees_celsius
         < eps_configuration_thresholds->temperature_minimum_for_heater_activation_in_decidegrees)
        ||
        (sensor_readings_this_iteration->battery_temperature_in_decidegrees_celsius
         > eps_configuration_thresholds->temperature_maximum_for_load_shedding_in_decidegrees))
    {
        eps_persistent_state->safe_mode_is_active = 1u;
        actuator_commands_output->safe_mode_alert_flag_for_obc = 1u;
        actuator_commands_output->safe_mode_alert_reason =
            (uint8_t)EPS_SAFE_REASON_TEMPERATURE_OUT_OF_RANGE;
        return;
    }

    // OBC heartbeat timeout
    if (eps_persistent_state->iterations_since_last_obc_heartbeat
        > eps_configuration_thresholds->obc_heartbeat_timeout_in_iterations)
    {
        eps_persistent_state->safe_mode_is_active = 1u;
        actuator_commands_output->safe_mode_alert_flag_for_obc = 1u;
        actuator_commands_output->safe_mode_alert_reason =
            (uint8_t)EPS_SAFE_REASON_OBC_HEARTBEAT_TIMEOUT;

        // Autonomous sub-state selection when OBC is dead
        if (sensor_readings_this_iteration->battery_voltage_in_millivolts
            < eps_configuration_thresholds->battery_voltage_critical_in_millivolts)
        {
            eps_persistent_state->current_safe_mode_sub_state =
                (uint8_t)EPS_SAFE_SUB_STATE_CHARGING;
        } else {
            eps_persistent_state->current_safe_mode_sub_state =
                (uint8_t)EPS_SAFE_SUB_STATE_COMMUNICATION;
        }
        return;
    }

    // No safe mode condition detected — clear the alert flag
    actuator_commands_output->safe_mode_alert_flag_for_obc = 0u;
    actuator_commands_output->safe_mode_alert_reason =
        (uint8_t)EPS_SAFE_REASON_NONE;
}

// ── Private helper: apply safe mode load shedding per sub-state ─────────────

static void apply_safe_mode_load_shedding_for_current_sub_state(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(actuator_commands_output != (void *)0);

    uint8_t sub_state = eps_persistent_state->current_safe_mode_sub_state;

    // All sub-states: SPAD and GNSS always shed
    actuator_commands_output->load_enable_flags[EPS_LOAD_SPAD_CAMERA] = 0u;
    actuator_commands_output->load_enable_flags[EPS_LOAD_GNSS_RECEIVER] = 0u;
    eps_persistent_state->load_is_currently_enabled[EPS_LOAD_SPAD_CAMERA] = 0u;
    eps_persistent_state->load_is_currently_enabled[EPS_LOAD_GNSS_RECEIVER] = 0u;

    // OBC always powered (unless REBOOT sub-state cycles it)
    actuator_commands_output->load_enable_flags[EPS_LOAD_OBC] = 1u;

    if (sub_state == (uint8_t)EPS_SAFE_SUB_STATE_DETUMBLING) {
        // ADCS needed for magnetorquers, UHF for beacon
        actuator_commands_output->load_enable_flags[EPS_LOAD_ADCS] = 1u;
        actuator_commands_output->load_enable_flags[EPS_LOAD_UHF_RADIO] = 1u;
    } else if (sub_state == (uint8_t)EPS_SAFE_SUB_STATE_CHARGING) {
        // Minimal power: only OBC and UHF beacon
        actuator_commands_output->load_enable_flags[EPS_LOAD_ADCS] = 0u;
        actuator_commands_output->load_enable_flags[EPS_LOAD_UHF_RADIO] = 1u;
        eps_persistent_state->load_is_currently_enabled[EPS_LOAD_ADCS] = 0u;
    } else if (sub_state == (uint8_t)EPS_SAFE_SUB_STATE_COMMUNICATION) {
        // UHF at full power for beacon, ADCS for attitude
        actuator_commands_output->load_enable_flags[EPS_LOAD_ADCS] = 1u;
        actuator_commands_output->load_enable_flags[EPS_LOAD_UHF_RADIO] = 1u;
    } else {
        // REBOOT: keep UHF and ADCS on (rebooting is handled externally)
        actuator_commands_output->load_enable_flags[EPS_LOAD_ADCS] = 1u;
        actuator_commands_output->load_enable_flags[EPS_LOAD_UHF_RADIO] = 1u;
    }
}

// ── Private helper: check if OBC commanded exit from safe mode ──────────────

static void check_if_obc_commanded_exit_from_safe_mode(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);

    // Safe mode exit requires ground command (ECSS-E-ST-70-11, p.26).
    // The OBC relays this by commanding a non-SAFE satellite mode.
    if (sensor_readings_this_iteration->satellite_mode_commanded_by_obc
        != (uint8_t)EPS_SATELLITE_MODE_SAFE)
    {
        eps_persistent_state->safe_mode_is_active = 0u;

        // Re-enable all loads
        for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_COUNT;
             load_index += 1u)
        {
            eps_persistent_state->load_is_currently_enabled[load_index] = 1u;
        }
    }

    // Update sub-state from OBC command if still in safe mode
    if (eps_persistent_state->safe_mode_is_active == 1u) {
        eps_persistent_state->current_safe_mode_sub_state =
            sensor_readings_this_iteration->safe_mode_sub_state_commanded_by_obc;
    }
}

// ── Private helper: transition to a new PCU mode ────────────────────────────

static void transition_pcu_to_new_operating_mode(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    uint8_t new_pcu_mode)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(new_pcu_mode <= (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE);

    eps_persistent_state->current_pcu_operating_mode = new_pcu_mode;
    eps_persistent_state->iterations_in_current_pcu_mode = 0u;
    eps_persistent_state->iterations_with_battery_voltage_below_charge_resume = 0u;

    if (new_pcu_mode == (uint8_t)EPS_PCU_MODE_CV_FLOAT) {
        eps_persistent_state->cv_float_current_substate =
            (uint8_t)EPS_CV_FLOAT_SUBSTATE_NORMAL;
    }

    if (new_pcu_mode == (uint8_t)EPS_PCU_MODE_MPPT_CHARGE) {
        mppt_algorithm_initialize(
            &eps_persistent_state->mppt_algorithm_persistent_state);
    }
}

// ── Private helper: shed lowest priority load still enabled ─────────────────

static void disable_the_lowest_priority_load_that_is_still_enabled(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(actuator_commands_output != (void *)0);

    // Iterate from lowest priority (SPAD=0) toward highest (OBC=4).
    // OBC (index 4) is never shed.
    for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_OBC;
         load_index += 1u)
    {
        if (eps_persistent_state->load_is_currently_enabled[load_index] == 1u) {
            eps_persistent_state->load_is_currently_enabled[load_index] = 0u;
            actuator_commands_output->load_enable_flags[load_index] = 0u;
            return;
        }
    }
}

// ── Private: MPPT_CHARGE mode logic (Figure 3.4.7, p.101) ──────────────────

static void run_mppt_charge_mode_logic(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);

    // Enable solar panel eFuse (panel connected to converter)
    actuator_commands_output->panel_efuse_should_be_enabled = 1u;

    uint16_t duty_cycle = eps_persistent_state->current_duty_cycle_as_fraction_of_65535;

    // Charging forbidden below 0 C (lithium-ion safety)
    if (sensor_readings_this_iteration->battery_temperature_in_decidegrees_celsius
        < eps_configuration_thresholds->temperature_minimum_for_charging_allowed_in_decidegrees)
    {
        duty_cycle = MPPT_MINIMUM_DUTY_CYCLE;
    }
    // Overcurrent protection: reduce duty cycle
    else if (sensor_readings_this_iteration->battery_current_in_milliamps
             > eps_configuration_thresholds->battery_current_maximum_charge_in_milliamps)
    {
        duty_cycle = decrease_duty_cycle_by_specified_step_size(
            duty_cycle, MPPT_DUTY_CYCLE_STEP_SIZE);
    }
    // Overvoltage protection: reduce duty cycle
    else if (sensor_readings_this_iteration->battery_voltage_in_millivolts
             > eps_configuration_thresholds->battery_voltage_maximum_in_millivolts)
    {
        duty_cycle = decrease_duty_cycle_by_specified_step_size(
            duty_cycle, MPPT_DUTY_CYCLE_STEP_SIZE);
    }
    // Normal operation: run MPPT algorithm
    else {
        duty_cycle = mppt_algorithm_run_one_iteration(
            &eps_persistent_state->mppt_algorithm_persistent_state,
            sensor_readings_this_iteration->solar_array_voltage_raw_adc_reading,
            sensor_readings_this_iteration->solar_array_current_raw_adc_reading);
    }

    eps_persistent_state->current_duty_cycle_as_fraction_of_65535 = duty_cycle;
    actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535 = duty_cycle;
    eps_persistent_state->iterations_in_current_pcu_mode += 1u;

    // Check transitions
    uint8_t solar_available = check_if_solar_array_voltage_is_above_minimum_operating_threshold(
        sensor_readings_this_iteration, eps_configuration_thresholds);

    if (solar_available == 0u) {
        transition_pcu_to_new_operating_mode(
            eps_persistent_state, (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE);
        return;
    }

    if (sensor_readings_this_iteration->battery_voltage_in_millivolts
        >= eps_configuration_thresholds->battery_voltage_full_threshold_in_millivolts)
    {
        transition_pcu_to_new_operating_mode(
            eps_persistent_state, (uint8_t)EPS_PCU_MODE_SA_LOAD_FOLLOW);
        return;
    }

    if (eps_persistent_state->iterations_in_current_pcu_mode
        > eps_configuration_thresholds->mppt_charge_timeout_for_insufficient_buffer_in_iterations)
    {
        transition_pcu_to_new_operating_mode(
            eps_persistent_state, (uint8_t)EPS_PCU_MODE_CV_FLOAT);
    }
}

// ── Private: CV_FLOAT mode logic (Figure 3.4.8, p.101-102) ─────────────────

static void run_cv_float_mode_logic(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);

    actuator_commands_output->panel_efuse_should_be_enabled = 1u;

    uint16_t duty_cycle = eps_persistent_state->current_duty_cycle_as_fraction_of_65535;
    uint8_t sub = eps_persistent_state->cv_float_current_substate;

    // Check solar availability first
    uint8_t solar_available = check_if_solar_array_voltage_is_above_minimum_operating_threshold(
        sensor_readings_this_iteration, eps_configuration_thresholds);

    if (solar_available == 0u) {
        transition_pcu_to_new_operating_mode(
            eps_persistent_state, (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE);
        return;
    }

    if (sub == (uint8_t)EPS_CV_FLOAT_SUBSTATE_TEMP_MPPT) {
        // Temporary MPPT: maximize extraction during load spike
        duty_cycle = mppt_algorithm_run_one_iteration(
            &eps_persistent_state->mppt_algorithm_persistent_state,
            sensor_readings_this_iteration->solar_array_voltage_raw_adc_reading,
            sensor_readings_this_iteration->solar_array_current_raw_adc_reading);

        // Return to NORMAL when battery current is no longer heavily negative
        if (sensor_readings_this_iteration->battery_current_in_milliamps >= 0) {
            eps_persistent_state->cv_float_current_substate =
                (uint8_t)EPS_CV_FLOAT_SUBSTATE_NORMAL;
        }
    } else {
        // NORMAL sub-state: constant voltage regulation

        // Large discharge detected → enter TEMP_MPPT
        if (sensor_readings_this_iteration->battery_current_in_milliamps
            < eps_configuration_thresholds->battery_current_maximum_discharge_in_milliamps)
        {
            eps_persistent_state->cv_float_current_substate =
                (uint8_t)EPS_CV_FLOAT_SUBSTATE_TEMP_MPPT;
            mppt_algorithm_initialize(
                &eps_persistent_state->mppt_algorithm_persistent_state);
        }
        // Check if battery voltage dropped below charge resume threshold
        else if (sensor_readings_this_iteration->battery_voltage_in_millivolts
                 < eps_configuration_thresholds->battery_voltage_charge_resume_threshold_in_millivolts)
        {
            eps_persistent_state->iterations_with_battery_voltage_below_charge_resume += 1u;

            if (eps_persistent_state->iterations_with_battery_voltage_below_charge_resume
                > eps_configuration_thresholds->cv_float_low_voltage_wait_timeout_in_iterations)
            {
                transition_pcu_to_new_operating_mode(
                    eps_persistent_state, (uint8_t)EPS_PCU_MODE_MPPT_CHARGE);
                return;
            }
        } else {
            eps_persistent_state->iterations_with_battery_voltage_below_charge_resume = 0u;
        }

        // Bang-bang voltage regulation on charging rail
        uint16_t target_voltage =
            eps_configuration_thresholds->battery_voltage_maximum_in_millivolts;
        uint16_t step =
            eps_configuration_thresholds->cv_float_duty_cycle_adjustment_step_size;

        if (sensor_readings_this_iteration->charging_rail_voltage_in_millivolts
            < target_voltage)
        {
            duty_cycle = increase_duty_cycle_by_specified_step_size(duty_cycle, step);
        } else if (sensor_readings_this_iteration->charging_rail_voltage_in_millivolts
                   > target_voltage)
        {
            duty_cycle = decrease_duty_cycle_by_specified_step_size(duty_cycle, step);
        }
    }

    duty_cycle = clamp_duty_cycle_to_safe_operating_range(duty_cycle);
    eps_persistent_state->current_duty_cycle_as_fraction_of_65535 = duty_cycle;
    actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535 = duty_cycle;
    eps_persistent_state->iterations_in_current_pcu_mode += 1u;
}

// ── Private: SA_LOAD_FOLLOW mode logic (Figure 3.4.9, p.102-103) ───────────

static void run_sa_load_follow_mode_logic(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);

    actuator_commands_output->panel_efuse_should_be_enabled = 1u;

    // Check solar first
    uint8_t solar_available = check_if_solar_array_voltage_is_above_minimum_operating_threshold(
        sensor_readings_this_iteration, eps_configuration_thresholds);

    if (solar_available == 0u) {
        transition_pcu_to_new_operating_mode(
            eps_persistent_state, (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE);
        return;
    }

    // Check if battery dropped below full → resume charging
    if (sensor_readings_this_iteration->battery_voltage_in_millivolts
        < eps_configuration_thresholds->battery_voltage_full_threshold_in_millivolts)
    {
        transition_pcu_to_new_operating_mode(
            eps_persistent_state, (uint8_t)EPS_PCU_MODE_MPPT_CHARGE);
        return;
    }

    // Run MPPT to track maximum power point
    uint16_t duty_cycle = mppt_algorithm_run_one_iteration(
        &eps_persistent_state->mppt_algorithm_persistent_state,
        sensor_readings_this_iteration->solar_array_voltage_raw_adc_reading,
        sensor_readings_this_iteration->solar_array_current_raw_adc_reading);

    // Clamp: if battery current exceeds charge threshold, reduce D
    if (sensor_readings_this_iteration->battery_current_in_milliamps
        > eps_configuration_thresholds->battery_current_minimum_charge_threshold_in_milliamps)
    {
        duty_cycle = decrease_duty_cycle_by_specified_step_size(
            duty_cycle, MPPT_DUTY_CYCLE_STEP_SIZE);
    }

    duty_cycle = clamp_duty_cycle_to_safe_operating_range(duty_cycle);
    eps_persistent_state->current_duty_cycle_as_fraction_of_65535 = duty_cycle;
    actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535 = duty_cycle;
    eps_persistent_state->iterations_in_current_pcu_mode += 1u;
}

// ── Private: BATTERY_DISCHARGE mode logic (Figure 3.4.10, p.103) ────────────

static void run_battery_discharge_mode_logic(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);

    // Open solar panel eFuse — no point with no sun
    actuator_commands_output->panel_efuse_should_be_enabled = 0u;

    // Converter off — no solar to convert
    uint16_t duty_cycle = MPPT_MINIMUM_DUTY_CYCLE;

    // Check if solar has returned
    uint8_t solar_available = check_if_solar_array_voltage_is_above_minimum_operating_threshold(
        sensor_readings_this_iteration, eps_configuration_thresholds);

    if (solar_available == 1u) {
        transition_pcu_to_new_operating_mode(
            eps_persistent_state, (uint8_t)EPS_PCU_MODE_MPPT_CHARGE);
        return;
    }

    // Check critically low battery
    uint16_t low_voltage_threshold =
        eps_configuration_thresholds->battery_voltage_minimum_in_millivolts
        + eps_configuration_thresholds->battery_voltage_hysteresis_margin_in_millivolts;

    if (sensor_readings_this_iteration->battery_voltage_in_millivolts
        < low_voltage_threshold)
    {
        // Battery dying and no solar → request safe mode
        eps_persistent_state->safe_mode_is_active = 1u;
        actuator_commands_output->safe_mode_alert_flag_for_obc = 1u;
        actuator_commands_output->safe_mode_alert_reason =
            (uint8_t)EPS_SAFE_REASON_BATTERY_BELOW_MINIMUM;
    }

    // Check overcurrent discharge → load shedding
    if (sensor_readings_this_iteration->battery_current_in_milliamps
        < eps_configuration_thresholds->battery_current_maximum_discharge_in_milliamps)
    {
        disable_the_lowest_priority_load_that_is_still_enabled(
            eps_persistent_state, actuator_commands_output);
    }

    eps_persistent_state->current_duty_cycle_as_fraction_of_65535 = duty_cycle;
    actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535 = duty_cycle;
    eps_persistent_state->iterations_in_current_pcu_mode += 1u;
}

// ── Private: dispatch to current PCU mode handler ───────────────────────────

static void run_pcu_mode_logic_for_current_operating_mode(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);

    uint8_t mode = eps_persistent_state->current_pcu_operating_mode;

    if (mode == (uint8_t)EPS_PCU_MODE_MPPT_CHARGE) {
        run_mppt_charge_mode_logic(
            eps_persistent_state, sensor_readings_this_iteration,
            eps_configuration_thresholds, actuator_commands_output);
    } else if (mode == (uint8_t)EPS_PCU_MODE_CV_FLOAT) {
        run_cv_float_mode_logic(
            eps_persistent_state, sensor_readings_this_iteration,
            eps_configuration_thresholds, actuator_commands_output);
    } else if (mode == (uint8_t)EPS_PCU_MODE_SA_LOAD_FOLLOW) {
        run_sa_load_follow_mode_logic(
            eps_persistent_state, sensor_readings_this_iteration,
            eps_configuration_thresholds, actuator_commands_output);
    } else if (mode == (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE) {
        run_battery_discharge_mode_logic(
            eps_persistent_state, sensor_readings_this_iteration,
            eps_configuration_thresholds, actuator_commands_output);
    } else {
        SATELLITE_ASSERT(0);
    }
}

// ── Private: thermal control ────────────────────────────────────────────────

static void apply_heater_control_and_temperature_safety_checks(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);
    SATELLITE_ASSERT(eps_configuration_thresholds != (void *)0);
    SATELLITE_ASSERT(actuator_commands_output != (void *)0);

    int16_t temperature =
        sensor_readings_this_iteration->battery_temperature_in_decidegrees_celsius;

    // Heater control
    if (temperature
        < eps_configuration_thresholds->temperature_minimum_for_heater_activation_in_decidegrees)
    {
        actuator_commands_output->heater_should_be_enabled = 1u;
    } else {
        actuator_commands_output->heater_should_be_enabled = 0u;
    }

    // Overtemperature: shed non-essential loads
    if (temperature
        > eps_configuration_thresholds->temperature_maximum_for_load_shedding_in_decidegrees)
    {
        disable_the_lowest_priority_load_that_is_still_enabled(
            eps_persistent_state, actuator_commands_output);
    }

    // Charging prohibition below 0 C: override duty cycle to minimum
    if (temperature
        < eps_configuration_thresholds->temperature_minimum_for_charging_allowed_in_decidegrees)
    {
        // Only override if we are in a charging mode (not BATTERY_DISCHARGE)
        if (eps_persistent_state->current_pcu_operating_mode
            != (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE)
        {
            actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535 =
                MPPT_MINIMUM_DUTY_CYCLE;
            eps_persistent_state->current_duty_cycle_as_fraction_of_65535 =
                MPPT_MINIMUM_DUTY_CYCLE;
        }
    }
}

// =============================================================================
// Public: initialize
// =============================================================================

void eps_state_machine_initialize(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    uint8_t initial_pcu_operating_mode)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(eps_configuration_thresholds != (void *)0);
    SATELLITE_ASSERT(initial_pcu_operating_mode
                     <= (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE);

    eps_persistent_state->current_pcu_operating_mode = initial_pcu_operating_mode;
    eps_persistent_state->current_satellite_mode_from_obc =
        (uint8_t)EPS_SATELLITE_MODE_CHARGING;
    eps_persistent_state->safe_mode_is_active = 0u;
    eps_persistent_state->current_safe_mode_sub_state =
        (uint8_t)EPS_SAFE_SUB_STATE_COMMUNICATION;
    eps_persistent_state->cv_float_current_substate =
        (uint8_t)EPS_CV_FLOAT_SUBSTATE_NORMAL;

    eps_persistent_state->iterations_in_current_pcu_mode = 0u;
    eps_persistent_state->iterations_with_battery_voltage_below_charge_resume = 0u;
    eps_persistent_state->iterations_since_last_obc_heartbeat = 0u;

    // Start at 50% duty cycle
    // 0.50 * 65535 = 32768
    eps_persistent_state->current_duty_cycle_as_fraction_of_65535 = 32768u;

    // All loads enabled at startup
    for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_COUNT;
         load_index += 1u)
    {
        eps_persistent_state->load_is_currently_enabled[load_index] = 1u;
    }

    mppt_algorithm_initialize(
        &eps_persistent_state->mppt_algorithm_persistent_state);

    eps_persistent_state->state_machine_has_been_initialized = 1u;

    (void)eps_configuration_thresholds;
}

// =============================================================================
// Public: run one iteration
// =============================================================================

void eps_state_machine_run_one_iteration(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    SATELLITE_ASSERT(eps_persistent_state != (void *)0);
    SATELLITE_ASSERT(sensor_readings_this_iteration != (void *)0);
    SATELLITE_ASSERT(eps_configuration_thresholds != (void *)0);
    SATELLITE_ASSERT(actuator_commands_output != (void *)0);
    SATELLITE_ASSERT(eps_persistent_state->state_machine_has_been_initialized == 1u);

    // Copy current load state to output
    for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_COUNT;
         load_index += 1u)
    {
        actuator_commands_output->load_enable_flags[load_index] =
            eps_persistent_state->load_is_currently_enabled[load_index];
    }

    update_obc_heartbeat_iteration_counter(
        eps_persistent_state, sensor_readings_this_iteration);

    check_for_safe_mode_entry_conditions_and_set_alert_flag(
        eps_persistent_state, sensor_readings_this_iteration,
        eps_configuration_thresholds, actuator_commands_output);

    if (eps_persistent_state->safe_mode_is_active == 1u) {
        apply_safe_mode_load_shedding_for_current_sub_state(
            eps_persistent_state, actuator_commands_output);
        check_if_obc_commanded_exit_from_safe_mode(
            eps_persistent_state, sensor_readings_this_iteration);
    }

    run_pcu_mode_logic_for_current_operating_mode(
        eps_persistent_state, sensor_readings_this_iteration,
        eps_configuration_thresholds, actuator_commands_output);

    apply_heater_control_and_temperature_safety_checks(
        eps_persistent_state, sensor_readings_this_iteration,
        eps_configuration_thresholds, actuator_commands_output);

    // Always sync output duty cycle from persistent state. Mode handlers
    // may return early during transitions without setting the output.
    actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535 =
        eps_persistent_state->current_duty_cycle_as_fraction_of_65535;

    actuator_commands_output->current_pcu_mode_for_telemetry =
        eps_persistent_state->current_pcu_operating_mode;

    SATELLITE_ASSERT(
        actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535
        >= MPPT_MINIMUM_DUTY_CYCLE);
    SATELLITE_ASSERT(
        actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535
        <= MPPT_MAXIMUM_DUTY_CYCLE);
}
