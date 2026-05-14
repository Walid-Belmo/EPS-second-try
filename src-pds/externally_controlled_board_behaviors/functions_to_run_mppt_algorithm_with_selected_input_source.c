#include <stdint.h>

#include "eps_state_machine.h"
#include "mppt_algorithm.h"
#include "externally_controlled_board_behaviors/functions_to_run_mppt_algorithm_with_selected_input_source.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "communication_with_esp32/sensor_sample_requests/functions_to_request_mppt_input_sample_from_esp32.h"

static uint8_t read_mppt_input_sample(
    pds_runtime_state_type *runtime_state,
    pds_mppt_input_sample_type *sample);
static uint8_t read_real_board_sensor_sample(
    pds_mppt_input_sample_type *sample);
static void store_missing_mppt_input_sample_for_status(
    pds_runtime_state_type *runtime_state);
static void store_mppt_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    const pds_mppt_input_sample_type *sample,
    uint16_t next_duty_cycle_as_fraction_of_65535);

void run_mppt_test_only(void)
{
    if (pds_control_loop_period_has_elapsed() == 0u)
    {
        return;
    }

    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    pds_mppt_input_sample_type sample;
    if (read_mppt_input_sample(runtime_state, &sample) == 0u)
    {
        store_missing_mppt_input_sample_for_status(runtime_state);
        return;
    }

    uint16_t next_duty_cycle_as_fraction_of_65535 =
        mppt_algorithm_run_one_iteration(
            &runtime_state->mppt_algorithm_state,
            sample.panel_voltage_raw_adc_reading,
            sample.panel_current_raw_adc_reading);

    request_pwm_output_in_runtime_state(
        runtime_state,
        next_duty_cycle_as_fraction_of_65535);
    store_mppt_demo_result_for_status(
        runtime_state,
        &sample,
        next_duty_cycle_as_fraction_of_65535);
}

static uint8_t read_mppt_input_sample(
    pds_runtime_state_type *runtime_state,
    pds_mppt_input_sample_type *sample)
{
    if (runtime_state->mppt_input_source
        == (uint8_t)PDS_MPPT_INPUT_SOURCE_ESP32_MODEL)
    {
        return request_mppt_input_sample_from_esp32_model(
            runtime_state->snapshot.mppt_duty_cycle_as_fraction_of_65535,
            sample);
    }

    return read_real_board_sensor_sample(sample);
}

static uint8_t read_real_board_sensor_sample(
    pds_mppt_input_sample_type *sample)
{
    (void)sample;
    return 0u;
}

static void store_missing_mppt_input_sample_for_status(
    pds_runtime_state_type *runtime_state)
{
    runtime_state->snapshot.mppt_input_sample_is_valid = 0u;
    runtime_state->snapshot.panel_voltage_in_millivolts = 0u;
    runtime_state->snapshot.panel_current_in_milliamps = 0u;
    runtime_state->snapshot.panel_power_in_milliwatts = 0u;
    request_no_pwm_output_in_runtime_state(runtime_state);
}

static void store_mppt_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    const pds_mppt_input_sample_type *sample,
    uint16_t next_duty_cycle_as_fraction_of_65535)
{
    runtime_state->snapshot.loop_count += 1u;
    runtime_state->snapshot.mppt_input_sample_is_valid = 1u;
    runtime_state->snapshot.panel_voltage_in_millivolts =
        sample->panel_voltage_in_millivolts;
    runtime_state->snapshot.panel_current_in_milliamps =
        sample->panel_current_in_milliamps;
    runtime_state->snapshot.panel_power_in_milliwatts =
        ((uint32_t)sample->panel_voltage_in_millivolts
         * (uint32_t)sample->panel_current_in_milliamps)
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
