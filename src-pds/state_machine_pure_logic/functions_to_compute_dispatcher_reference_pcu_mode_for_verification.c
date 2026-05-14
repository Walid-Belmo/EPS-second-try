// =============================================================================
// functions_to_compute_dispatcher_reference_pcu_mode_for_verification.c
//
// Implementation of the steady-state dispatcher oracle. Mirrors the decision
// tree in Fig 3.4.6 of the CHESS mission doc plus the three safety overrides
// listed in section §B of src-pds/state-transitions.md.
// =============================================================================

#include <stdint.h>

#include "eps_state_machine.h"
#include "eps_configuration_parameters.h"

#include "state_machine_pure_logic/functions_to_compute_dispatcher_reference_pcu_mode_for_verification.h"

uint8_t compute_dispatcher_reference_pcu_mode_for_verification_only(
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds,
    uint32_t iterations_since_last_obc_heartbeat,
    uint8_t *safe_mode_is_active_output,
    uint8_t *safe_mode_reason_output)
{
    *safe_mode_is_active_output = 0u;
    *safe_mode_reason_output = (uint8_t)EPS_SAFE_REASON_NONE;

    // Safety overrides — highest priority.
    if (sensor_readings->battery_voltage_in_millivolts
        < thresholds->battery_voltage_minimum_in_millivolts)
    {
        *safe_mode_is_active_output = 1u;
        *safe_mode_reason_output =
            (uint8_t)EPS_SAFE_REASON_BATTERY_BELOW_MINIMUM;
        return (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE;
    }
    if ((sensor_readings->battery_temperature_in_decidegrees_celsius
         < thresholds->temperature_minimum_for_heater_activation_in_decidegrees)
        || (sensor_readings->battery_temperature_in_decidegrees_celsius
            > thresholds->temperature_maximum_for_load_shedding_in_decidegrees))
    {
        *safe_mode_is_active_output = 1u;
        *safe_mode_reason_output =
            (uint8_t)EPS_SAFE_REASON_TEMPERATURE_OUT_OF_RANGE;
        return (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE;
    }
    if (iterations_since_last_obc_heartbeat
        > thresholds->obc_heartbeat_timeout_in_iterations)
    {
        *safe_mode_is_active_output = 1u;
        *safe_mode_reason_output =
            (uint8_t)EPS_SAFE_REASON_OBC_HEARTBEAT_TIMEOUT;
        return (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE;
    }

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

    // Near max — distinguish truly full from still-tapering.
    int16_t taper_band = (int16_t)thresholds->battery_current_minimum_charge_threshold_in_milliamps;
    int16_t battery_current = sensor_readings->battery_current_in_milliamps;
    int16_t absolute_battery_current = (battery_current < 0)
        ? (int16_t)(-battery_current)
        : battery_current;

    if ((sensor_readings->battery_voltage_in_millivolts
         >= thresholds->battery_voltage_full_threshold_in_millivolts)
        && (absolute_battery_current < taper_band))
    {
        return (uint8_t)EPS_PCU_MODE_SA_LOAD_FOLLOW;
    }
    return (uint8_t)EPS_PCU_MODE_CV_FLOAT;
}
