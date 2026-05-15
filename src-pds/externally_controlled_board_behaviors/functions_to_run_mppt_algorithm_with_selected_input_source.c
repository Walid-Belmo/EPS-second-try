/*
 * functions_to_run_mppt_algorithm_with_selected_input_source.c
 *
 * MPPT (Maximum Power Point Tracking) test mode runner.
 *
 * Called once per main-loop iteration when the operator has put the
 * firmware into MPPT_TEST mode (the State page or the MPPT page sends
 * `start_mppt_demo`, which sets `runtime_state.requested_mode` to
 * PDS_REQUESTED_MODE_MPPT_TEST). The dispatcher in app/main.c then
 * invokes run_mppt_test_only() every iteration until the mode changes.
 *
 * What MPPT does in one sentence: the algorithm watches the panel's
 * voltage and current, computes the duty cycle that pulls the panel
 * closest to its maximum-power-point, and asks the buck converter to
 * apply that duty cycle. Repeating this every loop iteration makes the
 * duty cycle converge.
 */

#include <stdint.h>

#include "eps_state_machine.h"
#include "mppt_algorithm.h"
#include "externally_controlled_board_behaviors/functions_to_run_mppt_algorithm_with_selected_input_source.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "sensor_inputs/sensor_readings.h"
#include "communication_with_esp32/sensor_sample_requests/functions_to_request_mppt_input_sample_from_esp32.h"

/* Forward declarations of the four private helpers below. They are
 * `static` so the linker keeps them invisible outside this file. */
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

/*
 * Top-level MPPT iteration. Called every pass through the main while(1)
 * loop in app/main.c when requested_mode is MPPT_TEST. The body is
 * deliberately short — each meaningful step is its own helper so a
 * reader can scan top-to-bottom and follow the story.
 */
void run_mppt_test_only(void)
{
    /* The MPPT algorithm is designed to run on a fixed cadence (default
     * 100 ms — see PDS_CONTROL_LOOP_PERIOD_MS in
     * structures_that_describe_pds_runtime_state.h). The main loop
     * spins much faster than that, so we early-out on every pass that
     * isn't due. The helper also updates the "last iteration timestamp"
     * the first time it returns true, so the next due-check measures
     * from this iteration. */
    if (pds_control_loop_period_has_elapsed() == 0u)
    {
        return;
    }

    /* Pointer to the single global runtime state struct. Every module
     * that needs to read or write firmware-wide state goes through this
     * accessor — there are no scattered globals. */
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* The MPPT algorithm needs four numbers per iteration: the panel's
     * voltage and current in both raw 12-bit ADC counts (used by the
     * algorithm itself for precision) and engineering units (mV / mA
     * for telemetry). pds_mppt_input_sample_type holds all four. */
    pds_mppt_input_sample_type sample;

    /* Try to obtain a fresh sample from whichever source the operator
     * selected (ESP32 simulation model vs. real board sensors). The
     * helper returns 0 if no sample is currently available — for the
     * ESP32 model that means the simulated curve hasn't been configured
     * yet; for the real-board path it means the chips couldn't be read.
     * In either case we declare the iteration "no data" and bail out
     * without commanding a new duty cycle. */
    if (read_mppt_input_sample(runtime_state, &sample) == 0u)
    {
        store_missing_mppt_input_sample_for_status(runtime_state);
        return;
    }

    /* Run one iteration of the Incremental Conductance MPPT algorithm
     * (the actual math lives in src/mppt_algorithm.c). It compares this
     * iteration's panel voltage and current against the previous
     * iteration's stored values, decides which way to nudge the duty
     * cycle, and returns the new duty as a fraction of 65535. The
     * persistent algorithm state (previous V, previous I, previous
     * duty, etc.) lives inside runtime_state->mppt_algorithm_state so
     * it survives across iterations. */
    uint16_t next_duty_cycle_as_fraction_of_65535 =
        mppt_algorithm_run_one_iteration(
            &runtime_state->mppt_algorithm_state,
            sample.panel_voltage_raw_adc_reading,
            sample.panel_current_raw_adc_reading);

    /* Push the new duty cycle into runtime state's "requested PWM"
     * field. This does NOT directly write the TCC0 hardware registers —
     * the actual hardware write happens later in apply_outputs_to_board(),
     * which runs after every mode runner so safety checks
     * (block_dangerous_outputs) get a chance to clamp the request first. */
    request_pwm_output_in_runtime_state(
        runtime_state,
        next_duty_cycle_as_fraction_of_65535);

    /* Mirror the iteration's results into the snapshot block of runtime
     * state. The status reply builder reads from the snapshot when the
     * MPPT page polls /api/status, so this is what makes the live
     * voltage / current / power / duty values appear on the page. */
    store_mppt_demo_result_for_status(
        runtime_state,
        &sample,
        next_duty_cycle_as_fraction_of_65535);
}

/*
 * Decides where the panel sample comes from this iteration. Two sources
 * exist today, picked by runtime_state->mppt_input_source:
 *
 *   PDS_MPPT_INPUT_SOURCE_ESP32_MODEL   - ESP32 runs a quadratic I-V
 *       curve simulation and returns the (V, I) point matching the
 *       current duty cycle. Used by the MPPT page demo.
 *
 *   PDS_MPPT_INPUT_SOURCE_BOARD_SENSORS - Read the real panel voltage
 *       and current from the mainboard (or from the injected fallback
 *       on devboard). Used when running against actual hardware.
 *
 * Returns 1 if a valid sample was written into *sample, 0 otherwise.
 */
static uint8_t read_mppt_input_sample(
    pds_runtime_state_type *runtime_state,
    pds_mppt_input_sample_type *sample)
{
    if (runtime_state->mppt_input_source
        == (uint8_t)PDS_MPPT_INPUT_SOURCE_ESP32_MODEL)
    {
        /* The ESP32 owns a simulated panel I-V model. We send it the
         * current duty-cycle value and it replies with the (V, I)
         * point where the simulated panel sits at that operating
         * point. The bridge call is synchronous — it waits for the
         * reply to arrive before returning. */
        return request_mppt_input_sample_from_esp32_model(
            runtime_state->snapshot.mppt_duty_cycle_as_fraction_of_65535,
            sample);
    }

    /* Anything other than ESP32_MODEL is treated as the real-board
     * sensor path. Today only one other value exists
     * (PDS_MPPT_INPUT_SOURCE_BOARD_SENSORS); falling through to it
     * here keeps the code shorter than an explicit if/else if. */
    return read_real_board_sensor_sample(sample);
}

/*
 * Reads the panel voltage and current from real board sensors via the
 * Layer 1 sensor abstraction. The abstraction hides whether the chips
 * are physically present or not — on the dev board the calls return
 * the operator-injected fallback values; on the mainboard they return
 * the INA226 readings.
 */
static uint8_t read_real_board_sensor_sample(
    pds_mppt_input_sample_type *sample)
{
    /* read_panel_voltage() and read_panel_current() are the Layer 1
     * entry points (declared in sensor_inputs/sensor_readings.h).
     * They internally branch on the global pds_sensor_source_type:
     * INJECTED returns operator-typed values, REAL_BOARD_HARDWARE
     * returns INA226 readings on mainboard. The MPPT loop does not
     * need to know which it got. */
    sample->panel_voltage_in_millivolts = read_panel_voltage();
    sample->panel_current_in_milliamps  = read_panel_current();

    /* The MPPT algorithm operates on raw 12-bit ADC counts, not on
     * mV/mA, because raw counts preserve the precision that
     * cross-multiplication inside the algorithm needs. We don't have
     * raw ADC values from Layer 1 yet (the abstraction only exposes
     * engineering units), so we synthesise plausible counts using the
     * same scale factors the legacy state-machine path used. The
     * scale factors live in
     * structures_that_describe_pds_runtime_state.h. */
    uint32_t voltage_raw =
        (uint32_t)sample->panel_voltage_in_millivolts
        / PDS_PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE;
    uint32_t current_raw =
        (uint32_t)sample->panel_current_in_milliamps
        / PDS_PANEL_RAW_ADC_TO_MILLIAMPS_SCALE;

    /* Clamp to the 12-bit ADC range (0..4095). The MPPT algorithm's
     * arithmetic assumes inputs fit in 12 bits — values larger than
     * that would silently overflow some intermediate products. */
    sample->panel_voltage_raw_adc_reading =
        (voltage_raw > 4095u) ? 4095u : (uint16_t)voltage_raw;
    sample->panel_current_raw_adc_reading =
        (current_raw > 4095u) ? 4095u : (uint16_t)current_raw;

    /* The Layer 1 reads always succeed (worst case they return 0), so
     * we never have to report "no sample". */
    return 1u;
}

/*
 * Called when the input-sample fetch failed (e.g. ESP32 hasn't been
 * told what curve to simulate yet). Marks the snapshot as "no valid
 * sample" so the MPPT page can show dashes instead of stale numbers,
 * and forces PWM to zero so the buck converter does nothing dangerous
 * while we have no idea what the panel is doing.
 */
static void store_missing_mppt_input_sample_for_status(
    pds_runtime_state_type *runtime_state)
{
    /* The MPPT page reads mppt_input_sample_is_valid to decide whether
     * to render numbers or "—". */
    runtime_state->snapshot.mppt_input_sample_is_valid = 0u;

    /* Zero out the engineering values so a stale reading from a
     * previous iteration doesn't get confused with a fresh one. */
    runtime_state->snapshot.panel_voltage_in_millivolts = 0u;
    runtime_state->snapshot.panel_current_in_milliamps = 0u;
    runtime_state->snapshot.panel_power_in_milliwatts = 0u;

    /* Without sensor data we can't compute a meaningful duty cycle,
     * so refuse to drive the buck converter at all. The actual
     * hardware write happens later in apply_outputs_to_board(); this
     * just clears the requested-PWM field that apply_outputs reads. */
    request_no_pwm_output_in_runtime_state(runtime_state);
}

/*
 * Called once per successful MPPT iteration to mirror this iteration's
 * results into the snapshot block of runtime state. The status reply
 * builder copies from the snapshot when the MPPT page polls
 * /api/status, so this function is what makes live values appear on
 * the page.
 */
static void store_mppt_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    const pds_mppt_input_sample_type *sample,
    uint16_t next_duty_cycle_as_fraction_of_65535)
{
    /* Iteration counter, useful for the page to detect that fresh
     * data is arriving (the page tracks the last-seen loop_count to
     * avoid duplicating points on the convergence chart). */
    runtime_state->snapshot.loop_count += 1u;

    /* Mark the sample as fresh — the page renders the readings instead
     * of dashes when this is non-zero. */
    runtime_state->snapshot.mppt_input_sample_is_valid = 1u;

    /* Engineering-unit copies of the input sample. Used by both the
     * MPPT page and any general-purpose telemetry consumer. */
    runtime_state->snapshot.panel_voltage_in_millivolts =
        sample->panel_voltage_in_millivolts;
    runtime_state->snapshot.panel_current_in_milliamps =
        sample->panel_current_in_milliamps;

    /* Compute panel power = V × I in milliwatts. The /1000 converts
     * the mV × mA product (microwatts) to milliwatts to keep the
     * snapshot field in a reasonable range. */
    runtime_state->snapshot.panel_power_in_milliwatts =
        ((uint32_t)sample->panel_voltage_in_millivolts
         * (uint32_t)sample->panel_current_in_milliamps)
        / 1000u;

    /* Latest duty cycle the algorithm chose, so the next iteration's
     * read_mppt_input_sample() can pass it back to the ESP32 model
     * (the model needs to know the current operating point to compute
     * the next (V, I) pair). */
    runtime_state->snapshot.mppt_duty_cycle_as_fraction_of_65535 =
        next_duty_cycle_as_fraction_of_65535;

    /* The remaining snapshot fields below are not really part of MPPT,
     * but the State page and any general telemetry consumer also read
     * them. Pinning them to safe constants here keeps the page from
     * showing stale values that were last written by a different mode
     * (e.g. state_test or manual). */
    runtime_state->snapshot.pcu_mode = (uint8_t)EPS_PCU_MODE_MPPT_CHARGE;
    runtime_state->snapshot.safe_mode_is_active = 0u;
    runtime_state->snapshot.safe_mode_reason = (uint8_t)EPS_SAFE_REASON_NONE;
    runtime_state->snapshot.panel_efuse_is_enabled = 1u;
    runtime_state->snapshot.heater_is_enabled = 0u;
    runtime_state->snapshot.safe_mode_alert_for_obc = 0u;
    runtime_state->snapshot.load_enable_mask = PDS_LOAD_ENABLE_MASK_ALL_LOADS;
}
