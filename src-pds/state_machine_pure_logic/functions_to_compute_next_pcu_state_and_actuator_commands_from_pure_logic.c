// =============================================================================
// functions_to_compute_next_pcu_state_and_actuator_commands_from_pure_logic.c
//
// Implementation of the bench-simulator pure dispatcher. Public function plus
// four private helpers, one per concern (heartbeat counter, safety override
// decision, normal-operation mode selection, actuator command synthesis).
//
// Numeric thresholds come from *thresholds (eps_configuration_thresholds).
// Enum values come from eps_state_machine.h. See state-transitions.md for the
// open issues and the eventual flight-grade target.
// =============================================================================

#include <stdint.h>

#include "eps_state_machine.h"
#include "eps_configuration_parameters.h"

#include "state_machine_pure_logic/functions_to_compute_next_pcu_state_and_actuator_commands_from_pure_logic.h"

// =============================================================================
// Local constants — only used to give the PWM/load defaults clear names.
// =============================================================================

// Default PWM duty cycle while the converter is actively running (any sun mode).
// 32768 = 50% of 65535. Placeholder until the per-mode duty algorithms exist.
#define DEFAULT_RUNNING_DUTY_CYCLE_AS_FRACTION_OF_65535 32768u

// Minimum duty cycle while the buck converter is parked (BATTERY_DISCHARGE).
// Spec is ambiguous (see state-transitions.md open issue §C). We use 0 here
// because the eFuse is open and the converter has nothing to drive.
#define PARKED_DUTY_CYCLE_AS_FRACTION_OF_65535 0u

// =============================================================================
// Private helper declarations
// =============================================================================

static void update_obc_heartbeat_iteration_counter_from_this_iteration_input(
    struct eps_state_machine_persistent_state *persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings);

static uint8_t decide_whether_any_safety_condition_should_override_normal_operation(
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_state_machine_persistent_state *persistent_state,
    const struct eps_configuration_thresholds *thresholds,
    uint8_t *safe_mode_reason_output);

static uint8_t decide_safe_mode_substate_when_obc_heartbeat_was_lost(
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds);

static uint8_t decide_normal_operation_pcu_mode_from_solar_availability_and_battery_state(
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds);

static void fill_actuator_commands_for_chosen_pcu_mode_and_safe_substate(
    uint8_t pcu_mode,
    uint8_t safe_mode_is_active,
    uint8_t safe_mode_reason,
    uint8_t safe_mode_substate,
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds,
    struct eps_actuator_output_commands *actuator_commands_output);

static void fill_load_enable_flags_for_normal_operation(
    uint8_t load_enable_flags_output[EPS_LOAD_COUNT]);

static void fill_load_enable_flags_for_safe_mode_substate(
    uint8_t safe_mode_substate,
    uint8_t load_enable_flags_output[EPS_LOAD_COUNT]);

// =============================================================================
// Public function
// =============================================================================

void compute_next_pcu_state_and_actuator_commands_from_inputs_and_current_state(
    struct eps_state_machine_persistent_state *persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    // Always update the heartbeat counter first — its current value feeds into
    // the safety check below.
    update_obc_heartbeat_iteration_counter_from_this_iteration_input(
        persistent_state, sensor_readings);

    // Decide whether any safety condition should override normal operation
    // on this iteration. The "should override" answer is independent of
    // whether we are already in safe mode — that distinction lives in the
    // latch logic just below.
    uint8_t safety_override_reason_this_iteration = (uint8_t)EPS_SAFE_REASON_NONE;
    uint8_t any_safety_condition_is_currently_active =
        decide_whether_any_safety_condition_should_override_normal_operation(
            sensor_readings,
            persistent_state,
            thresholds,
            &safety_override_reason_this_iteration);

    // Latch logic: once safe mode is set, only an OBC command to leave SAFE
    // satellite mode releases it, AND only if no safety condition is currently
    // active. Otherwise the latch holds.
    if (any_safety_condition_is_currently_active != 0u)
    {
        persistent_state->safe_mode_is_active = 1u;
    }
    else if (persistent_state->safe_mode_is_active != 0u)
    {
        if (sensor_readings->satellite_mode_commanded_by_obc
            != (uint8_t)EPS_SATELLITE_MODE_SAFE)
        {
            persistent_state->safe_mode_is_active = 0u;
        }
    }

    // Pick the PCU mode and (if in safe) the sub-state.
    uint8_t chosen_pcu_mode;
    uint8_t chosen_safe_substate;

    if (persistent_state->safe_mode_is_active != 0u)
    {
        // Stay parked in BATTERY_DISCHARGE for the PCU mode reporting while
        // safe-mode load shedding owns the actuator outputs. This matches the
        // diagrammed BATTERY_DISCHARGE → SAFE_MODE relationship in Fig 3.4.10
        // and keeps the buck converter off whenever safety is engaged.
        chosen_pcu_mode = (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE;

        // Sub-state: OBC commanded value if the heartbeat is alive, else
        // autonomous selection (CHARGING if battery critical, else COMM).
        if (sensor_readings->obc_heartbeat_received_this_iteration != 0u)
        {
            chosen_safe_substate =
                sensor_readings->safe_mode_sub_state_commanded_by_obc;
        }
        else
        {
            chosen_safe_substate =
                decide_safe_mode_substate_when_obc_heartbeat_was_lost(
                    sensor_readings, thresholds);
        }
    }
    else
    {
        chosen_pcu_mode =
            decide_normal_operation_pcu_mode_from_solar_availability_and_battery_state(
                sensor_readings, thresholds);
        chosen_safe_substate = persistent_state->current_safe_mode_sub_state;
    }

    // Persist the chosen mode/sub-state so streaming telemetry reflects it.
    persistent_state->current_pcu_operating_mode = chosen_pcu_mode;
    persistent_state->current_safe_mode_sub_state = chosen_safe_substate;
    persistent_state->current_satellite_mode_from_obc =
        sensor_readings->satellite_mode_commanded_by_obc;

    // Emit actuator commands based on the chosen mode and sub-state.
    fill_actuator_commands_for_chosen_pcu_mode_and_safe_substate(
        chosen_pcu_mode,
        persistent_state->safe_mode_is_active,
        safety_override_reason_this_iteration,
        chosen_safe_substate,
        sensor_readings,
        thresholds,
        actuator_commands_output);

    // Mirror persistent state into the per-iteration command struct for
    // telemetry — the existing wrapper reads from actuator_commands when it
    // builds the snapshot.
    actuator_commands_output->current_pcu_mode_for_telemetry = chosen_pcu_mode;
}

// =============================================================================
// Private helpers
// =============================================================================

static void update_obc_heartbeat_iteration_counter_from_this_iteration_input(
    struct eps_state_machine_persistent_state *persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings)
{
    if (sensor_readings->obc_heartbeat_received_this_iteration != 0u)
    {
        persistent_state->iterations_since_last_obc_heartbeat = 0u;
    }
    else if (persistent_state->iterations_since_last_obc_heartbeat
             < 0xFFFFFFFFu)
    {
        persistent_state->iterations_since_last_obc_heartbeat += 1u;
    }
}

static uint8_t decide_whether_any_safety_condition_should_override_normal_operation(
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_state_machine_persistent_state *persistent_state,
    const struct eps_configuration_thresholds *thresholds,
    uint8_t *safe_mode_reason_output)
{
    // Battery below absolute minimum — highest priority.
    if (sensor_readings->battery_voltage_in_millivolts
        < thresholds->battery_voltage_minimum_in_millivolts)
    {
        *safe_mode_reason_output =
            (uint8_t)EPS_SAFE_REASON_BATTERY_BELOW_MINIMUM;
        return 1u;
    }

    // Temperature outside the operational envelope.
    if ((sensor_readings->battery_temperature_in_decidegrees_celsius
         < thresholds->temperature_minimum_for_heater_activation_in_decidegrees)
        || (sensor_readings->battery_temperature_in_decidegrees_celsius
            > thresholds->temperature_maximum_for_load_shedding_in_decidegrees))
    {
        *safe_mode_reason_output =
            (uint8_t)EPS_SAFE_REASON_TEMPERATURE_OUT_OF_RANGE;
        return 1u;
    }

    // OBC heartbeat lost beyond the timeout window.
    if (persistent_state->iterations_since_last_obc_heartbeat
        > thresholds->obc_heartbeat_timeout_in_iterations)
    {
        *safe_mode_reason_output =
            (uint8_t)EPS_SAFE_REASON_OBC_HEARTBEAT_TIMEOUT;
        return 1u;
    }

    *safe_mode_reason_output = (uint8_t)EPS_SAFE_REASON_NONE;
    return 0u;
}

static uint8_t decide_safe_mode_substate_when_obc_heartbeat_was_lost(
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds)
{
    // Battery critical → CHARGING (maximum load shedding). Otherwise try to
    // raise the OBC over the radio link, i.e. COMMUNICATION.
    if (sensor_readings->battery_voltage_in_millivolts
        < thresholds->battery_voltage_critical_in_millivolts)
    {
        return (uint8_t)EPS_SAFE_SUB_STATE_CHARGING;
    }
    return (uint8_t)EPS_SAFE_SUB_STATE_COMMUNICATION;
}

static uint8_t decide_normal_operation_pcu_mode_from_solar_availability_and_battery_state(
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds)
{
    // No sun → BATTERY_DISCHARGE.
    if (sensor_readings->solar_array_voltage_in_millivolts
        < thresholds->solar_array_minimum_voltage_for_availability_in_millivolts)
    {
        return (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE;
    }

    // Sun + battery not near max → MPPT_CHARGE.
    uint16_t battery_voltage_threshold_for_near_max =
        (uint16_t)(thresholds->battery_voltage_maximum_in_millivolts
                   - thresholds->battery_voltage_hysteresis_margin_in_millivolts);
    if (sensor_readings->battery_voltage_in_millivolts
        < battery_voltage_threshold_for_near_max)
    {
        return (uint8_t)EPS_PCU_MODE_MPPT_CHARGE;
    }

    // Battery near max — distinguish SA_LOAD_FOLLOW (truly full) from
    // CV_FLOAT (near max but still tapering). "battery_full" is defined
    // as voltage at/above the full threshold AND battery current close to
    // zero (charge has tapered).
    {
        int16_t taper_band = (int16_t)thresholds->battery_current_minimum_charge_threshold_in_milliamps;
        int16_t battery_current = sensor_readings->battery_current_in_milliamps;
        int16_t absolute_battery_current = (battery_current < 0)
            ? (int16_t)(-battery_current)
            : battery_current;

        uint8_t battery_is_truly_full =
            ((sensor_readings->battery_voltage_in_millivolts
              >= thresholds->battery_voltage_full_threshold_in_millivolts)
             && (absolute_battery_current < taper_band))
            ? 1u : 0u;

        if (battery_is_truly_full != 0u)
        {
            return (uint8_t)EPS_PCU_MODE_SA_LOAD_FOLLOW;
        }
    }
    return (uint8_t)EPS_PCU_MODE_CV_FLOAT;
}

static void fill_actuator_commands_for_chosen_pcu_mode_and_safe_substate(
    uint8_t pcu_mode,
    uint8_t safe_mode_is_active,
    uint8_t safe_mode_reason,
    uint8_t safe_mode_substate,
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds,
    struct eps_actuator_output_commands *actuator_commands_output)
{
    // Panel eFuse: open in BATTERY_DISCHARGE (sun unavailable or safe-parked),
    // closed otherwise.
    actuator_commands_output->panel_efuse_should_be_enabled =
        (pcu_mode == (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE) ? 0u : 1u;

    // PWM duty: parked when the eFuse is open, otherwise the default running
    // duty (placeholder — the per-mode duty algorithms will replace this).
    actuator_commands_output->buck_converter_duty_cycle_as_fraction_of_65535 =
        (pcu_mode == (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE)
            ? (uint16_t)PARKED_DUTY_CYCLE_AS_FRACTION_OF_65535
            : (uint16_t)DEFAULT_RUNNING_DUTY_CYCLE_AS_FRACTION_OF_65535;

    // Heater: on whenever temperature is below the heater activation threshold.
    actuator_commands_output->heater_should_be_enabled =
        (sensor_readings->battery_temperature_in_decidegrees_celsius
         < thresholds->temperature_minimum_for_heater_activation_in_decidegrees)
        ? 1u : 0u;

    // Load mask: all on in normal operation; per-substate mask while in safe.
    if (safe_mode_is_active != 0u)
    {
        fill_load_enable_flags_for_safe_mode_substate(
            safe_mode_substate,
            actuator_commands_output->load_enable_flags);
    }
    else
    {
        fill_load_enable_flags_for_normal_operation(
            actuator_commands_output->load_enable_flags);
    }

    // Safe mode telemetry fields.
    actuator_commands_output->safe_mode_alert_flag_for_obc = safe_mode_is_active;
    actuator_commands_output->safe_mode_alert_reason = safe_mode_reason;
}

static void fill_load_enable_flags_for_normal_operation(
    uint8_t load_enable_flags_output[EPS_LOAD_COUNT])
{
    for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_COUNT;
         load_index = (uint8_t)(load_index + 1u))
    {
        load_enable_flags_output[load_index] = 1u;
    }
}

static void fill_load_enable_flags_for_safe_mode_substate(
    uint8_t safe_mode_substate,
    uint8_t load_enable_flags_output[EPS_LOAD_COUNT])
{
    // Defaults — start everything off, then re-enable per sub-state.
    for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_COUNT;
         load_index = (uint8_t)(load_index + 1u))
    {
        load_enable_flags_output[load_index] = 0u;
    }

    // OBC is the highest-priority load. It is on in every safe sub-state.
    load_enable_flags_output[EPS_LOAD_OBC] = 1u;

    // UHF radio is on in every safe sub-state — used either to take ground
    // command or to beacon during autonomy.
    load_enable_flags_output[EPS_LOAD_UHF_RADIO] = 1u;

    // ADCS is on in every sub-state EXCEPT CHARGING (CHARGING drops ADCS to
    // shed as much load as possible).
    if (safe_mode_substate != (uint8_t)EPS_SAFE_SUB_STATE_CHARGING)
    {
        load_enable_flags_output[EPS_LOAD_ADCS] = 1u;
    }

    // SPAD camera and GNSS receiver are never enabled in safe mode — they are
    // the two lowest-priority loads and shed first.
}
