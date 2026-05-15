/*
 * functions_to_run_power_state_machine_with_injected_sensor_values.c
 *
 * State-test mode runner. This is the most powerful demo mode in the
 * firmware: it runs the FULL EPS state machine — every PCU mode,
 * every safe-mode trigger, every load-shedding decision — every
 * iteration, against sensor values the operator injected from the web
 * page.
 *
 * Mode entry: operator types `start_state_demo battery_voltage=...
 * panel_voltage=... ...` on the State page. The handler stores the
 * injected sensor values, sets requested_mode = STATE_TEST, and the
 * dispatcher in app/main.c starts calling this runner every iteration.
 * Subsequent `inject_state ...` commands update the sensor values
 * mid-scenario without leaving the mode, which is how the State page's
 * 12 preset scenarios animate transitions in real time.
 *
 * The state machine itself is pure logic — defined in
 * src/eps_state_machine.h, implementation in
 * src-pds/state_machine_pure_logic/. Given (sensor readings,
 * configuration thresholds, current persistent state), it computes
 * the next persistent state and the actuator commands the EPS should
 * issue. This runner is just the glue: it pulls inputs, calls the
 * pure-logic step, applies the chosen PWM, and mirrors the actuator
 * commands into the snapshot block for the page to display.
 */

#include <stdint.h>

#include "eps_state_machine.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "sensor_inputs/sensor_readings.h"
#include "state_machine_pure_logic/functions_to_compute_next_pcu_state_and_actuator_commands_from_pure_logic.h"
#include "externally_controlled_board_behaviors/functions_to_run_power_state_machine_with_injected_sensor_values.h"

/* Forward declarations of the three private helpers below. */
static uint8_t build_load_enable_mask(
    const struct eps_actuator_output_commands *actuator_commands);
static uint16_t choose_state_demo_pwm_request(
    const struct eps_actuator_output_commands *actuator_commands);
static void store_state_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    const struct eps_actuator_output_commands *actuator_commands);

/*
 * One iteration of the state-test mode. Called every pass through the
 * main loop while requested_mode is STATE_TEST. The body is short on
 * purpose — five steps, top-to-bottom:
 *
 *   1. Period gate (only run on the 100 ms cadence)
 *   2. Pull sensor readings via Layer 1
 *   3. Run the pure-logic state machine
 *   4. Push the chosen PWM into the snapshot
 *   5. Mirror everything else into the snapshot for the page
 */
void run_state_transition_test_only(void)
{
    /* The state machine is designed to run on a fixed cadence (100 ms
     * by default). The main loop spins much faster than that; bail
     * out on iterations that aren't due. The helper also stamps "now"
     * as the new cadence reference when it returns true. */
    if (pds_control_loop_period_has_elapsed() == 0u)
    {
        return;
    }

    /* Pointer to the firmware-wide runtime state. The state machine's
     * persistent state (current PCU mode, iteration counters,
     * cached previous-iteration values) lives in
     * runtime_state->state_machine_state. */
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Two stack-local structs the state machine works with: the
     * sensor readings INPUT, and the actuator commands OUTPUT. The
     * state machine reads from one and writes to the other; nothing
     * else touches them. */
    struct eps_sensor_readings_this_iteration sensor_readings;
    struct eps_actuator_output_commands actuator_commands;

    /* Pull every sensor reading via the Layer 1 abstraction. With the
     * default sensor source (INJECTED), this fills the struct from
     * runtime_state->injected_state_inputs — i.e. the values the
     * operator typed on the State page. With REAL_BOARD_HARDWARE
     * source, the same call returns INA226 readings on the mainboard
     * (and falls back to injected values on the dev board). The
     * state machine doesn't know which it got. */
    read_all_state_machine_sensor_inputs(&sensor_readings);

    /* THE big one. Pure logic. Given current sensor readings,
     * configuration thresholds, and the state machine's persistent
     * state, decide the next persistent state and what every actuator
     * (PWM, panel eFuse, heater, each load) should do this iteration.
     * Implementation lives in state_machine_pure_logic/. */
    compute_next_pcu_state_and_actuator_commands_from_inputs_and_current_state(
        &runtime_state->state_machine_state,
        &sensor_readings,
        &runtime_state->thresholds,
        &actuator_commands);

    /* Push the chosen PWM into the snapshot. The helper consults the
     * panel_efuse_should_be_enabled field — if the state machine
     * decided to disconnect the panel, we don't drive PWM regardless
     * of what the converter calculation said. */
    request_pwm_output_in_runtime_state(
        runtime_state,
        choose_state_demo_pwm_request(&actuator_commands));

    /* Mirror the rest of the actuator commands and the iteration's
     * sensor values into the snapshot block. The status reply
     * builder reads from the snapshot every status reply, so this is
     * what makes the State page's PCU-mode tile, safe-mode pill,
     * load-mask display, etc. update live. */
    store_state_demo_result_for_status(runtime_state, &actuator_commands);
}

/*
 * Pack the per-load enable flags (one bool per load: SPAD camera,
 * GNSS, UHF radio, ADCS, OBC) into a single byte so the status reply
 * can transmit it as a bitmask.
 *
 * Bit 0 = load index 0 = SPAD camera (lowest priority, shed first)
 * Bit 4 = load index 4 = OBC          (highest priority, never shed)
 *
 * The state machine writes one bool per load; the page would prefer
 * one byte. This helper bridges the two.
 */
static uint8_t build_load_enable_mask(
    const struct eps_actuator_output_commands *actuator_commands)
{
    uint8_t load_enable_mask = 0u;

    /* Bounded loop: EPS_LOAD_COUNT is a compile-time constant (5).
     * The conventions doc requires every loop to have a compile-time
     * upper bound — this is one. */
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

/*
 * Decide the actual PWM duty to push into the snapshot. The state
 * machine returns two related values: a buck-converter duty cycle
 * AND a flag saying whether the panel-side eFuse should be enabled
 * at all.
 *
 * If the eFuse is requested OFF (e.g. BATTERY_DISCHARGE mode, or any
 * fault that disconnects the panel), driving any PWM at all is
 * pointless and potentially noisy — the panel isn't physically
 * connected. So this helper returns 0 in that case, regardless of
 * what the converter calculation said.
 *
 * If the eFuse is requested ON, the PWM duty IS what the state
 * machine computed.
 */
static uint16_t choose_state_demo_pwm_request(
    const struct eps_actuator_output_commands *actuator_commands)
{
    if (actuator_commands->panel_efuse_should_be_enabled == 0u)
    {
        return 0u;
    }

    return actuator_commands->buck_converter_duty_cycle_as_fraction_of_65535;
}

/*
 * Mirror the iteration's results into the snapshot block of runtime
 * state. The status reply builder copies from the snapshot when the
 * page polls /api/status, so this function is what makes the live
 * State-page tiles update.
 *
 * Note one detail: the panel V/I values in the snapshot come from
 * runtime_state->injected_state_inputs DIRECTLY (not from
 * sensor_readings) so the page always shows what the operator typed,
 * not whatever the Layer 1 abstraction rounded those values to. That
 * round-trip honesty matters for the State page's "expected vs.
 * observed" comparison.
 */
static void store_state_demo_result_for_status(
    pds_runtime_state_type *runtime_state,
    const struct eps_actuator_output_commands *actuator_commands)
{
    /* Iteration counter, useful for the page to detect that fresh
     * data is arriving. */
    runtime_state->snapshot.loop_count += 1u;

    /* Panel V/I read straight from the injected inputs so the page
     * shows the operator's typed values without any roundtrip
     * rounding. The power = V × I / 1000 stays in milliwatts to keep
     * the snapshot field in a reasonable range. */
    runtime_state->snapshot.panel_voltage_in_millivolts =
        runtime_state->injected_state_inputs.panel_voltage_in_millivolts;
    runtime_state->snapshot.panel_current_in_milliamps =
        runtime_state->injected_state_inputs.panel_current_in_milliamps;
    runtime_state->snapshot.panel_power_in_milliwatts =
        ((uint32_t)runtime_state->snapshot.panel_voltage_in_millivolts
         * (uint32_t)runtime_state->snapshot.panel_current_in_milliamps)
        / 1000u;

    /* The duty cycle the state machine computed — separate from the
     * "applied" PWM that block_dangerous_outputs may have clamped.
     * Two separate fields so the page can show both ("here's what the
     * algorithm wanted, here's what actually came out"). */
    runtime_state->snapshot.state_machine_duty_cycle_as_fraction_of_65535 =
        actuator_commands->buck_converter_duty_cycle_as_fraction_of_65535;

    /* The PCU mode the state machine just decided we're in. Drives
     * the State page's mode pill and the diagram highlight. */
    runtime_state->snapshot.pcu_mode =
        actuator_commands->current_pcu_mode_for_telemetry;

    /* Safe-mode telemetry. Three fields:
     *   - is_active: are we in safe mode right now?
     *   - reason:    which trigger fired?
     *   - alert_for_obc: surface this to the OBC's housekeeping read?
     * The safe_mode_is_active value comes from the state machine's
     * persistent state, the other two from the actuator commands. */
    runtime_state->snapshot.safe_mode_is_active =
        runtime_state->state_machine_state.safe_mode_is_active;
    runtime_state->snapshot.safe_mode_reason =
        actuator_commands->safe_mode_alert_reason;
    runtime_state->snapshot.safe_mode_alert_for_obc =
        actuator_commands->safe_mode_alert_flag_for_obc;

    /* Per-actuator outputs the state machine commanded. */
    runtime_state->snapshot.panel_efuse_is_enabled =
        actuator_commands->panel_efuse_should_be_enabled;
    runtime_state->snapshot.heater_is_enabled =
        actuator_commands->heater_should_be_enabled;

    /* Pack the 5 per-load enable bits into a single byte for the
     * status reply. The page's load-bitmap rendering reads this as
     * one hex value. */
    runtime_state->snapshot.load_enable_mask =
        build_load_enable_mask(actuator_commands);
}
