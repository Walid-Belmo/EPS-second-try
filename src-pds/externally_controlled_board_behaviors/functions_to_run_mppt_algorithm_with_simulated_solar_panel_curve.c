#include <stdint.h>

#include "eps_state_machine.h"
#include "mppt_algorithm.h"
#include "externally_controlled_board_behaviors/functions_to_run_mppt_algorithm_with_simulated_solar_panel_curve.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"
#include "command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.h"

#define PDS_MILLIVOLTS_PER_DECI_VOLT 100u
#define PDS_QUADRATIC_TERM_DIVISOR 100000
#define PDS_LINEAR_TERM_DIVISOR 10000

static uint16_t estimate_panel_voltage_from_battery_and_pwm(
    const pds_runtime_state_type *runtime_state);
static uint16_t compute_curve_current_for_panel_voltage(
    const pds_runtime_state_type *runtime_state,
    uint16_t panel_voltage_in_millivolts);
static uint16_t clamp_panel_voltage_to_curve_range(
    const pds_runtime_state_type *runtime_state,
    uint16_t panel_voltage_in_millivolts);
static uint16_t convert_panel_voltage_to_raw_adc(
    uint16_t panel_voltage_in_millivolts);
static uint16_t convert_panel_current_to_raw_adc(
    uint16_t panel_current_in_milliamps);
static void store_mppt_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    uint16_t panel_voltage_in_millivolts,
    uint16_t panel_current_in_milliamps,
    uint16_t next_duty_cycle_as_fraction_of_65535);

void run_mppt_test_only(void)
{
    if (pds_control_loop_period_has_elapsed() == 0u)
    {
        return;
    }

    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();
    uint16_t panel_voltage_in_millivolts =
        estimate_panel_voltage_from_battery_and_pwm(runtime_state);
    uint16_t panel_current_in_milliamps =
        compute_curve_current_for_panel_voltage(
            runtime_state,
            panel_voltage_in_millivolts);
    uint16_t panel_voltage_raw_adc =
        convert_panel_voltage_to_raw_adc(panel_voltage_in_millivolts);
    uint16_t panel_current_raw_adc =
        convert_panel_current_to_raw_adc(panel_current_in_milliamps);
    uint16_t next_duty_cycle_as_fraction_of_65535 =
        mppt_algorithm_run_one_iteration(
            &runtime_state->mppt_algorithm_state,
            panel_voltage_raw_adc,
            panel_current_raw_adc);

    request_pwm_output_in_runtime_state(
        runtime_state,
        next_duty_cycle_as_fraction_of_65535);
    store_mppt_demo_result_for_status(
        runtime_state,
        panel_voltage_in_millivolts,
        panel_current_in_milliamps,
        next_duty_cycle_as_fraction_of_65535);
}

static uint16_t estimate_panel_voltage_from_battery_and_pwm(
    const pds_runtime_state_type *runtime_state)
{
    uint16_t duty_cycle =
        runtime_state->snapshot.mppt_duty_cycle_as_fraction_of_65535;

    if (duty_cycle == 0u)
    {
        return runtime_state->mppt_curve.maximum_panel_voltage_in_millivolts;
    }

    uint32_t estimated_panel_voltage =
        ((uint32_t)runtime_state->mppt_curve.battery_voltage_in_millivolts
         * (uint32_t)MPPT_DUTY_CYCLE_FULL_SCALE)
        / (uint32_t)duty_cycle;

    if (estimated_panel_voltage > 65535u)
    {
        estimated_panel_voltage = 65535u;
    }

    return clamp_panel_voltage_to_curve_range(
        runtime_state,
        (uint16_t)estimated_panel_voltage);
}

static uint16_t compute_curve_current_for_panel_voltage(
    const pds_runtime_state_type *runtime_state,
    uint16_t panel_voltage_in_millivolts)
{
    int32_t voltage_in_deci_volts =
        (int32_t)(panel_voltage_in_millivolts / PDS_MILLIVOLTS_PER_DECI_VOLT);
    int64_t quadratic_term =
        ((int64_t)runtime_state->mppt_curve.coefficient_a_scaled
         * (int64_t)voltage_in_deci_volts
         * (int64_t)voltage_in_deci_volts)
        / PDS_QUADRATIC_TERM_DIVISOR;
    int64_t linear_term =
        ((int64_t)runtime_state->mppt_curve.coefficient_b_scaled
         * (int64_t)voltage_in_deci_volts)
        / PDS_LINEAR_TERM_DIVISOR;
    int64_t current_in_milliamps =
        quadratic_term
        + linear_term
        + (int64_t)runtime_state->mppt_curve.coefficient_c_in_milliamps;

    if (current_in_milliamps < 0)
    {
        return 0u;
    }

    if (current_in_milliamps > 65535)
    {
        return 65535u;
    }

    return (uint16_t)current_in_milliamps;
}

static uint16_t clamp_panel_voltage_to_curve_range(
    const pds_runtime_state_type *runtime_state,
    uint16_t panel_voltage_in_millivolts)
{
    if (panel_voltage_in_millivolts
        < runtime_state->mppt_curve.minimum_panel_voltage_in_millivolts)
    {
        return runtime_state->mppt_curve.minimum_panel_voltage_in_millivolts;
    }

    if (panel_voltage_in_millivolts
        > runtime_state->mppt_curve.maximum_panel_voltage_in_millivolts)
    {
        return runtime_state->mppt_curve.maximum_panel_voltage_in_millivolts;
    }

    return panel_voltage_in_millivolts;
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

static void store_mppt_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    uint16_t panel_voltage_in_millivolts,
    uint16_t panel_current_in_milliamps,
    uint16_t next_duty_cycle_as_fraction_of_65535)
{
    runtime_state->snapshot.loop_count += 1u;
    runtime_state->snapshot.panel_voltage_in_millivolts =
        panel_voltage_in_millivolts;
    runtime_state->snapshot.panel_current_in_milliamps =
        panel_current_in_milliamps;
    runtime_state->snapshot.panel_power_in_milliwatts =
        ((uint32_t)panel_voltage_in_millivolts
         * (uint32_t)panel_current_in_milliamps)
        / 1000u;
    runtime_state->snapshot.mppt_duty_cycle_as_fraction_of_65535 =
        next_duty_cycle_as_fraction_of_65535;
    runtime_state->snapshot.pcu_mode = (uint8_t)EPS_PCU_MODE_MPPT_CHARGE;
    runtime_state->snapshot.safe_mode_is_active = 0u;
    runtime_state->snapshot.safe_mode_reason = (uint8_t)EPS_SAFE_REASON_NONE;
    runtime_state->snapshot.panel_efuse_is_enabled = 1u;
    runtime_state->snapshot.heater_is_enabled = 0u;
    runtime_state->snapshot.safe_mode_alert_for_obc = 0u;
    runtime_state->snapshot.load_enable_mask = PDS_LOAD_ENABLE_MASK_ALL_LOADS;
}
