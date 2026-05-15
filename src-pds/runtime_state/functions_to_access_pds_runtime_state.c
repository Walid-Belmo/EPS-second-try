/*
 * functions_to_access_pds_runtime_state.c
 *
 * Owner of the firmware-wide runtime-state singleton — the one
 * mega-struct that holds every field every mode runner might need to
 * read or write. Plus the small set of helpers other modules use to
 * interrogate or mutate it.
 *
 * Why one shared struct and not many per-module structs: every mode
 * runner needs to read several fields from across the firmware
 * (sensor inputs, mode flag, MPPT algorithm state, state-machine
 * persistent state, snapshot fields for the page) and write to others.
 * Splitting into N per-module structs would require N parallel
 * "get_pointer_to_module_state" accessors and N include-tree edges.
 * One struct keeps the architecture flat and grep-friendly: any field
 * is reachable from `runtime_state->X` and the accessor function is
 * the same one everywhere.
 *
 * Helpers in this file:
 *
 *   set_starting_runtime_state_to_safe_defaults() - called once at
 *       boot to put every field at a known safe value (mode = OFF,
 *       PWM = 0, all manual outputs off, thresholds at their default
 *       values, etc.).
 *
 *   get_pointer_to_pds_runtime_state() - the accessor every other
 *       module uses.
 *
 *   requested_mode_is_*() - one predicate per mode, used by the
 *       dispatcher in app/main.c to pick which runner to call this
 *       iteration.
 *
 *   pds_control_loop_period_has_elapsed() - throttle the control
 *       loop to a deterministic 100 ms cadence regardless of how
 *       fast the main loop spins.
 *
 *   reset_mppt_control_loop_state() / reset_state_demo_runtime_state_after_new_inputs()
 *       - called when entering MPPT_TEST or STATE_TEST mode so the
 *       relevant algorithm state starts from a clean slate.
 *
 *   record_*() / remember_result_of_command_from_esp32() - link-quality
 *       and last-command-tracking counters used by the status reply.
 */

#include <stdint.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "eps_state_machine.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "mppt_algorithm.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"

/* The single static struct that holds the entire firmware-wide
 * runtime state. File-scope `static` keeps it invisible outside this
 * translation unit; the accessor function below is the only way other
 * modules reach it. */
static struct pds_runtime_state_storage {
    pds_runtime_state_type values;
} pds_runtime_state_storage;

static void set_default_threshold_values(pds_runtime_state_type *runtime_state);
static void set_default_injected_state_values(
    pds_runtime_state_type *runtime_state);
static void set_default_snapshot_values(pds_runtime_state_type *runtime_state);
static uint8_t requested_mode_matches(uint8_t requested_mode_to_check);

/*
 * Boot step 8 (called once from main(), after every hardware peripheral
 * has been initialised). Walks every field of the runtime-state
 * singleton and sets it to a safe known value. Without this, the
 * struct would start at whatever the BSS zeros to (which is mostly OK
 * but doesn't initialise the threshold values that the state machine
 * needs, doesn't seed the MPPT algorithm's persistent state, and
 * doesn't stamp the cadence reference timestamps).
 *
 * After this returns, the firmware is ready for the main loop to
 * start: requested_mode = OFF (so the OFF runner is dispatched until
 * the operator picks something else), every output requested = 0,
 * every counter at 0, every threshold at its default, every snapshot
 * field at a sensible "no data yet" value.
 */
void set_starting_runtime_state_to_safe_defaults(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_OFF;
    runtime_state->mppt_input_source =
        (uint8_t)PDS_MPPT_INPUT_SOURCE_ESP32_MODEL;
    runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->manual_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->manual_pv_switch_requested = 0u;
    runtime_state->manual_bat_switch_requested = 0u;
    runtime_state->manual_status_led_requested = 0u;
    set_default_injected_state_values(runtime_state);
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

    reset_mppt_control_loop_state();
    reset_state_demo_runtime_state_after_new_inputs();
}

/*
 * The single global runtime-state singleton lives at
 * pds_runtime_state_storage.values (declared as a file-scope `static`
 * struct above). This accessor is the only legitimate way for any
 * other module to read or write firmware-wide state. Returning a raw
 * pointer is intentional: the runtime state is a mega-struct that
 * contains every field every mode runner might need to touch, so
 * by-value copies would be wasteful. Convention: callers read or
 * mutate fields through `runtime_state->snapshot.X` etc.
 */
pds_runtime_state_type *get_pointer_to_pds_runtime_state(void)
{
    return &pds_runtime_state_storage.values;
}

/* The six requested_mode_is_* predicates below are the per-mode tests
 * the dispatcher in app/main.c uses to pick which runner to call this
 * iteration. Each one is a one-liner around requested_mode_matches().
 * Convention here is "one named function per mode" rather than letting
 * the dispatcher peek directly at runtime_state->requested_mode —
 * because the dispatcher reads as English ("if requested mode is off,
 * run off mode; else if requested mode is flight, run flight ...")
 * which is exactly the readability the project's conventions doc asks
 * for. */

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

uint8_t requested_mode_is_manual(void)
{
    return requested_mode_matches((uint8_t)PDS_REQUESTED_MODE_MANUAL);
}

/*
 * Throttles the control loop to a fixed cadence (default 100 ms — see
 * PDS_CONTROL_LOOP_PERIOD_MS in
 * structures_that_describe_pds_runtime_state.h). The main loop spins
 * much faster than that, so mode runners (run_mppt_test_only,
 * run_state_transition_test_only, etc.) call this first and bail out
 * early on iterations that aren't due. This keeps the actual control
 * math running on a deterministic timetable independent of how fast
 * the rest of the loop happens to run.
 *
 * Returns 1 if at least PDS_CONTROL_LOOP_PERIOD_MS milliseconds have
 * elapsed since the last "true" return, in which case it ALSO updates
 * the stored timestamp (so the next due-check measures from this
 * iteration). Returns 0 otherwise.
 */
uint8_t pds_control_loop_period_has_elapsed(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Read the free-running millisecond tick (driven by the ARM SysTick
     * timer at 48 MHz, see drivers/millisecond_tick_timer_using_arm_systick.c).
     * Wraps every ~49 days, but the unsigned subtraction below
     * handles the wrap correctly as long as the period is short. */
    uint32_t now_ms = millisecond_tick_timer_get_milliseconds_since_boot();
    uint32_t elapsed_ms = now_ms - runtime_state->last_control_loop_timestamp_ms;

    /* Not yet due — caller should skip this iteration. */
    if (elapsed_ms < PDS_CONTROL_LOOP_PERIOD_MS)
    {
        return 0u;
    }

    /* Due. Stamp "now" as the new reference so the next due-check
     * measures from this iteration, not from the previous one. */
    runtime_state->last_control_loop_timestamp_ms = now_ms;
    return 1u;
}

/*
 * Called by the command handler when the operator issues
 * `start_mppt_demo` to enter MPPT_TEST mode. Wipes any state left over
 * from a previous MPPT run so the algorithm starts from a clean slate
 * each time. Without this reset, the algorithm's "previous voltage /
 * current / duty" memory would carry stale values from a prior session,
 * which can throw off the first few iterations of convergence.
 */
void reset_mppt_control_loop_state(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Re-initialise the persistent algorithm state struct (the math
     * lives in src/mppt_algorithm.c). This zeroes the previous-V,
     * previous-I, previous-duty fields the algorithm uses as memory. */
    mppt_algorithm_initialize(&runtime_state->mppt_algorithm_state);

    /* Mark the snapshot as "no valid sample yet" so the MPPT page
     * doesn't show a leftover reading from the previous session. */
    runtime_state->snapshot.mppt_input_sample_is_valid = 0u;
    runtime_state->snapshot.panel_voltage_in_millivolts = 0u;
    runtime_state->snapshot.panel_current_in_milliamps = 0u;
    runtime_state->snapshot.panel_power_in_milliwatts = 0u;

    /* Start the duty cycle at the midpoint (32768 = 50%). The MPPT
     * algorithm needs a starting point to perturb; mid-range gives it
     * the most room to move in either direction without saturating. */
    runtime_state->snapshot.mppt_duty_cycle_as_fraction_of_65535 = 32768u;

    /* Reset the control-loop cadence reference so the FIRST iteration
     * after entering MPPT_TEST mode doesn't fire immediately (which
     * would happen if the timestamp were stale from a previous mode). */
    runtime_state->last_control_loop_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
}

/*
 * Called by the start_state_demo command handler when entering
 * STATE_TEST mode. Re-initialises the EPS state machine's persistent
 * state to a known starting point (PCU mode = MPPT_CHARGE,
 * iteration counters cleared, internal latches reset). Without this,
 * the state machine would start each scenario from wherever the
 * previous scenario left it, which would defeat the whole point of
 * the State page's "run a scenario from a clean baseline" pattern.
 */
void reset_state_demo_runtime_state_after_new_inputs(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    eps_state_machine_initialize(
        &runtime_state->state_machine_state,
        &runtime_state->thresholds,
        (uint8_t)EPS_PCU_MODE_MPPT_CHARGE);

    /* Start the displayed state-machine duty at the midpoint so the
     * State page doesn't show 0 or a stale value before the first
     * iteration runs. */
    runtime_state->snapshot.state_machine_duty_cycle_as_fraction_of_65535 =
        32768u;

    /* Reset the cadence reference so the state machine's first
     * iteration after entering this mode runs ON TIME instead of
     * immediately after the mode-switch command. */
    runtime_state->last_control_loop_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
}

/*
 * Bumped by the CHIPS reader every time a complete, CRC-valid frame
 * arrives. Counter goes into the status reply so the page can show
 * "n valid frames received over the link's lifetime" — useful for
 * confirming the link is healthy and the operator isn't typing into
 * a dead bridge.
 */
void record_valid_chips_frame_from_esp32(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    runtime_state->valid_chips_frame_count += 1u;
}

/*
 * Called by the CHIPS reader when a frame is broken (the parser
 * couldn't validate the CRC, or the frame exceeded the maximum size).
 * Bumps the appropriate counter for the operator to see — but DOES
 * NOT change any output. A broken command must never be allowed to
 * affect mode, PWM, switches, or anything else; that's the whole
 * point of having CRC. The increment is the only effect.
 *
 * Two separate counters because the page distinguishes "noisy line"
 * (CRC errors) from "framing went off the rails" (frame too long).
 */
void record_broken_chips_message_without_changing_outputs(
    chips_parser_result_type parser_result)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    if (parser_result == CHIPS_PARSER_RESULT_ERROR_CRC_MISMATCH)
    {
        runtime_state->crc_error_count += 1u;
    }
    else if (parser_result == CHIPS_PARSER_RESULT_ERROR_FRAME_TOO_LONG)
    {
        runtime_state->too_long_frame_count += 1u;
    }
}

/*
 * Called by the dispatcher AFTER it has finished executing each
 * command. Records the command's ID and the reply's first byte
 * (which by CHIPS convention is the status byte: SUCCESS,
 * INVALID_PAYLOAD_LENGTH, etc.). The page uses these to display
 * "last command sent: 0x37; last result: success" in its diagnostic
 * panel, which is invaluable when a button click silently fails.
 */
void remember_result_of_command_from_esp32(
    const chips_parsed_frame_type *received_command,
    const chips_parsed_frame_type *reply_to_send)
{
    SATELLITE_ASSERT(received_command != (void *)0);
    SATELLITE_ASSERT(reply_to_send != (void *)0);

    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    runtime_state->executed_command_count += 1u;
    runtime_state->last_command_id = received_command->command_id;

    /* Status byte is always the first payload byte of the reply, by
     * CHIPS convention. Skip the read if the reply is empty (which
     * happens for the unknown-command path that build_ack_reply may
     * have skipped). */
    if (reply_to_send->payload_length_in_bytes > 0u)
    {
        runtime_state->last_command_status = reply_to_send->payload_bytes[0];
    }
}

/*
 * Three set_default_* helpers below populate the three sub-blocks of
 * the runtime state with safe starting values. Called from
 * set_starting_runtime_state_to_safe_defaults(). Splitting them into
 * separate functions keeps the boot-init function short and makes
 * it obvious which sub-block each value belongs to.
 *
 * The threshold defaults reflect the CHESS mission spec: 8.4 V
 * battery max, 5.0 V battery min, 8.2 V solar-availability cutoff,
 * heater on below -10°C, load shedding above 60°C, OBC heartbeat
 * timeout = 1200 iterations × 100 ms = 120 seconds (matches the
 * CHIPS protocol spec). All values overrideable later by a future
 * runtime-parameter-update CHIPS command (not yet implemented —
 * see docs/requirements.md COMMS-5).
 */
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

static void set_default_snapshot_values(pds_runtime_state_type *runtime_state)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    runtime_state->snapshot.loop_count = 0u;
    runtime_state->snapshot.panel_voltage_in_millivolts = 0u;
    runtime_state->snapshot.panel_current_in_milliamps = 0u;
    runtime_state->snapshot.panel_power_in_milliwatts = 0u;
    runtime_state->snapshot.mppt_input_sample_is_valid = 0u;
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
    runtime_state->snapshot.status_led_is_on = 0u;
    runtime_state->snapshot.pv_efuse_power_good = 0u;
    runtime_state->snapshot.pv_efuse_fault_active = 0u;
    runtime_state->snapshot.bat_efuse_power_good = 0u;
    runtime_state->snapshot.bat_efuse_fault_active = 0u;
}

static uint8_t requested_mode_matches(uint8_t requested_mode_to_check)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    return (runtime_state->requested_mode == requested_mode_to_check) ? 1u : 0u;
}
