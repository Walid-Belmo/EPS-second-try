#include <stdint.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"
#include "communication_with_esp32/command_execution/payload_parsing/functions_to_parse_payloads_inside_board_commands.h"
#include "communication_with_esp32/command_execution/functions_to_execute_board_commands_received_from_esp32.h"
#include "shared_helpers/functions_to_read_and_write_little_endian_values.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "sensor_inputs/sensor_readings.h"
#include "status_reporting_to_esp32/functions_to_build_status_replies_sent_to_esp32.h"

static void prepare_reply_header(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_off_command(
    pds_runtime_state_type *runtime_state,
    chips_parsed_frame_type *reply_to_send);
static void handle_run_pwm_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_start_mppt_demo_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_start_state_demo_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_inject_state_inputs_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_get_values_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_stream_values_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_enter_manual_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_set_manual_pwm_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_set_manual_pv_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_set_manual_bat_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_set_manual_led_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static void handle_set_sensor_source_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);
static uint8_t reject_if_not_in_manual_mode(
    const pds_runtime_state_type *runtime_state,
    chips_parsed_frame_type *reply_to_send);
static void build_values_reply_with_current_time(
    chips_parsed_frame_type *reply_to_send,
    const pds_runtime_state_type *runtime_state,
    uint8_t status,
    uint32_t requested_field_mask);

void execute_command_from_esp32_and_build_reply(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    SATELLITE_ASSERT(received_command != (void *)0);
    SATELLITE_ASSERT(reply_to_send != (void *)0);

    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    prepare_reply_header(received_command, reply_to_send);

    if (received_command->response_flag != 0u)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_COMMAND_NOT_AVAILABLE);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_OFF)
    {
        handle_off_command(runtime_state, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_RUN_PWM)
    {
        handle_run_pwm_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_START_MPPT_DEMO)
    {
        handle_start_mppt_demo_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_START_STATE_DEMO)
    {
        handle_start_state_demo_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_INJECT_STATE_INPUTS)
    {
        handle_inject_state_inputs_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_GET_VALUES)
    {
        handle_get_values_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_STREAM_VALUES)
    {
        handle_stream_values_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_ENTER_MANUAL)
    {
        handle_enter_manual_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_SET_MANUAL_PWM)
    {
        handle_set_manual_pwm_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_SET_MANUAL_PV)
    {
        handle_set_manual_pv_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_SET_MANUAL_BAT)
    {
        handle_set_manual_bat_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_SET_MANUAL_LED)
    {
        handle_set_manual_led_command(runtime_state, received_command, reply_to_send);
    }
    else if (received_command->command_id == PDS_BOARD_COMMAND_SET_SENSOR_SOURCE)
    {
        handle_set_sensor_source_command(runtime_state, received_command, reply_to_send);
    }
    else
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_UNKNOWN_COMMAND);
    }

    remember_result_of_command_from_esp32(received_command, reply_to_send);
}

/* Pre-fills the reply frame's CHIPS header fields the protocol
 * requires regardless of which handler ends up filling the payload:
 * sequence number echoes the request (so the page can pair them up),
 * command_id echoes the request, response_flag = 1 marks it a reply
 * not a fresh command, payload length starts at 0 and the handler
 * grows it. Called once before the dispatcher picks a handler. */
static void prepare_reply_header(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    reply_to_send->sequence_number = received_command->sequence_number;
    reply_to_send->command_id = received_command->command_id;
    reply_to_send->response_flag = 1u;
    reply_to_send->payload_length_in_bytes = 0u;
}

/* `off` text command from the operator. Switches the firmware to OFF
 * mode (so the dispatcher in main.c will call run_off_mode every
 * iteration) and aggressively zeros every snapshot field that could
 * leave a stale visual on the page. The PWM-zero call is what
 * actually stops the buck converter on the next iteration's
 * apply_outputs_to_board(). */
static void handle_off_command(
    pds_runtime_state_type *runtime_state,
    chips_parsed_frame_type *reply_to_send)
{
    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_OFF;
    runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->snapshot.mppt_input_sample_is_valid = 0u;
    runtime_state->snapshot.panel_voltage_in_millivolts = 0u;
    runtime_state->snapshot.panel_current_in_milliamps = 0u;
    runtime_state->snapshot.panel_power_in_milliwatts = 0u;
    request_no_pwm_output_in_runtime_state(runtime_state);
    build_ack_reply_to_esp32(
        reply_to_send,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
}

/* `run_pwm duty=NNNNN` text command. Validates the payload is exactly
 * the 2 bytes a u16 takes, parses the duty value out of the first two
 * payload bytes (little-endian), stores it in
 * runtime_state.fixed_pwm_duty_cycle_as_fraction_of_65535, and switches
 * the firmware into FIXED_PWM_TEST mode so the next iteration's
 * dispatcher runs run_fixed_pwm_test_only(). */
static void handle_run_pwm_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_RUN_PWM_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535 =
        read_uint16_from_little_endian_bytes(received_command->payload_bytes);
    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_FIXED_PWM_TEST;
    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        (PDS_VALUE_FIELD_PWM | PDS_VALUE_FIELD_MODE));
}

/* `start_mppt_demo curve=quadratic ...` text command. The curve
 * parameters live on the ESP32 (it owns the simulated panel I-V
 * model); this firmware-side command just switches into MPPT_TEST mode
 * with input-source = ESP32_MODEL, and resets the algorithm's
 * persistent state so it starts from the midpoint duty (32768 = 50%).
 * The actual sample-fetch round-trip happens later in
 * run_mppt_test_only(). Payload is empty (0 bytes). */
static void handle_start_mppt_demo_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_MPPT_DEMO_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    runtime_state->mppt_input_source =
        (uint8_t)PDS_MPPT_INPUT_SOURCE_ESP32_MODEL;
    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_MPPT_TEST;
    reset_mppt_control_loop_state();
    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        (PDS_VALUE_FIELD_MPPT | PDS_VALUE_FIELD_MODE));
}

/* `start_state_demo battery_voltage=... ...` text command. Validates
 * the 17-byte payload, parses every injected sensor field, range-checks
 * them, and on success: stores them in runtime_state.injected_state_inputs,
 * switches into STATE_TEST mode, AND resets the state machine's
 * persistent state so the next iteration starts the scenario from a
 * clean slate. */
static void handle_start_state_demo_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_STATE_INPUT_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    pds_state_demo_inputs_type inputs =
        read_state_demo_inputs_from_command_payload(
            received_command->payload_bytes);
    if (state_demo_inputs_are_valid(&inputs) == 0u)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    runtime_state->injected_state_inputs = inputs;
    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_STATE_TEST;
    reset_state_demo_runtime_state_after_new_inputs();
    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        (PDS_VALUE_FIELD_STATE | PDS_VALUE_FIELD_INPUTS));
}

/* `inject_state ...` text command. Same payload shape as
 * start_state_demo but does NOT change mode or reset the state
 * machine — used to update sensor values mid-scenario so the State
 * page's 12 preset scenarios can animate transitions in real time
 * (e.g. "battery_voltage drops below threshold while still in MPPT
 * mode"). The next iteration of run_state_transition_test_only sees
 * the new values and the state machine reacts. */
static void handle_inject_state_inputs_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_STATE_INPUT_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    pds_state_demo_inputs_type inputs =
        read_state_demo_inputs_from_command_payload(
            received_command->payload_bytes);
    if (state_demo_inputs_are_valid(&inputs) == 0u)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    runtime_state->injected_state_inputs = inputs;
    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        (PDS_VALUE_FIELD_INPUTS | PDS_VALUE_FIELD_STATE));
}

/* `get_values fields=...` text command. One-shot snapshot poll — the
 * web pages call this when they first load (before any streaming has
 * been turned on) to populate their tiles. Optional 4-byte payload
 * carries a bitmask of which field groups to include; with no payload
 * (or all-ones), every block in the status reply is sent. */
static void handle_get_values_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    uint32_t requested_field_mask = PDS_VALUE_FIELD_ALL;

    if (received_command->payload_length_in_bytes
        == PDS_GET_VALUES_PAYLOAD_LENGTH_IN_BYTES)
    {
        requested_field_mask =
            read_uint16_from_little_endian_bytes(
                &received_command->payload_bytes[0]);
        requested_field_mask |=
            ((uint32_t)read_uint16_from_little_endian_bytes(
                &received_command->payload_bytes[2]) << 16u);
    }
    else if (received_command->payload_length_in_bytes != 0u)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        requested_field_mask);
}

/* `stream_values on period=NNN fields=...` (or `stream_values off`).
 * Configures the streaming path: stores the on/off flag, the cadence,
 * and the field mask in runtime state. The actual periodic sending
 * happens in send_status_if_needed() in the status_reporting_to_esp32
 * folder, called every main-loop iteration; that function reads these
 * fields and decides whether to fire. Payload is 7 bytes:
 * 1 byte enabled + 2 bytes period_ms + 4 bytes field mask. */
static void handle_stream_values_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_STREAM_VALUES_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    uint8_t requested_enabled = received_command->payload_bytes[0];
    uint16_t requested_period_ms =
        read_uint16_from_little_endian_bytes(
            &received_command->payload_bytes[1]);
    uint32_t requested_field_mask =
        read_uint16_from_little_endian_bytes(&received_command->payload_bytes[3]);
    requested_field_mask |=
        ((uint32_t)read_uint16_from_little_endian_bytes(
            &received_command->payload_bytes[5]) << 16u);

    if ((requested_enabled > 1u)
        || ((requested_enabled != 0u)
            && ((requested_period_ms < PDS_MINIMUM_STREAM_PERIOD_MS)
                || (requested_period_ms > PDS_MAXIMUM_STREAM_PERIOD_MS))))
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    runtime_state->telemetry_stream_is_enabled = requested_enabled;
    runtime_state->telemetry_stream_period_ms = requested_period_ms;
    runtime_state->telemetry_field_mask = requested_field_mask;
    runtime_state->last_stream_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        PDS_VALUE_FIELD_COMMANDS);
}

static void handle_enter_manual_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_ENTER_MANUAL_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    /* Switch into manual mode and force every operator-controlled output to
     * a known safe state. The user explicitly turns each thing on later via
     * the four set_manual_* commands; until then the eFuses stay disabled,
     * the LED stays off, and PWM holds at zero. */
    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_MANUAL;
    runtime_state->manual_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->manual_pv_switch_requested = 0u;
    runtime_state->manual_bat_switch_requested = 0u;
    runtime_state->manual_status_led_requested = 0u;
    request_no_pwm_output_in_runtime_state(runtime_state);

    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        (PDS_VALUE_FIELD_MODE | PDS_VALUE_FIELD_PWM | PDS_VALUE_FIELD_MANUAL));
}

static void handle_set_manual_pwm_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_SET_MANUAL_PWM_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }
    if (reject_if_not_in_manual_mode(runtime_state, reply_to_send) != 0u)
    {
        return;
    }

    runtime_state->manual_pwm_duty_cycle_as_fraction_of_65535 =
        read_uint16_from_little_endian_bytes(received_command->payload_bytes);

    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        (PDS_VALUE_FIELD_PWM | PDS_VALUE_FIELD_MANUAL));
}

static void handle_set_manual_pv_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_SET_MANUAL_PV_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }
    if (reject_if_not_in_manual_mode(runtime_state, reply_to_send) != 0u)
    {
        return;
    }

    /* Anything non-zero counts as "on" so we never end up with a stale value
     * other than the canonical 0 or 1 in the runtime state. */
    runtime_state->manual_pv_switch_requested =
        (received_command->payload_bytes[0] != 0u) ? 1u : 0u;

    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        PDS_VALUE_FIELD_MANUAL);
}

static void handle_set_manual_bat_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_SET_MANUAL_BAT_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }
    if (reject_if_not_in_manual_mode(runtime_state, reply_to_send) != 0u)
    {
        return;
    }

    runtime_state->manual_bat_switch_requested =
        (received_command->payload_bytes[0] != 0u) ? 1u : 0u;

    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        PDS_VALUE_FIELD_MANUAL);
}

static void handle_set_manual_led_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_SET_MANUAL_LED_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }
    if (reject_if_not_in_manual_mode(runtime_state, reply_to_send) != 0u)
    {
        return;
    }

    runtime_state->manual_status_led_requested =
        (received_command->payload_bytes[0] != 0u) ? 1u : 0u;

    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        PDS_VALUE_FIELD_MANUAL);
}

static void handle_set_sensor_source_command(
    pds_runtime_state_type *runtime_state,
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    if (received_command->payload_length_in_bytes
        != PDS_SET_SENSOR_SOURCE_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    /* Only the two enum values are legal. The page sends 0 or 1; anything
     * else is a caller bug we want to flag rather than silently coerce. */
    uint8_t requested_source = received_command->payload_bytes[0];
    if ((requested_source != (uint8_t)PDS_SENSOR_SOURCE_INJECTED)
        && (requested_source != (uint8_t)PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE))
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    /* Mode-agnostic — accepted from any active mode. The source choice
     * is global firmware state and applies to every read_X() call from
     * the next iteration onward. */
    set_sensor_source((pds_sensor_source_type)requested_source);

    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        PDS_VALUE_FIELD_SENSOR_READS);
}

static uint8_t reject_if_not_in_manual_mode(
    const pds_runtime_state_type *runtime_state,
    chips_parsed_frame_type *reply_to_send)
{
    if (runtime_state->requested_mode == (uint8_t)PDS_REQUESTED_MODE_MANUAL)
    {
        return 0u;
    }
    /* The four set_manual_* commands are tied to the manual-mode runner
     * because nothing else copies their RAM values into the hardware drivers.
     * Reject them with a clear status code so the operator's UI can prompt
     * them to enter manual mode first instead of silently dropping. */
    build_ack_reply_to_esp32(
        reply_to_send,
        (uint8_t)CHIPS_RESPONSE_STATUS_COMMAND_NOT_AVAILABLE);
    return 1u;
}

static void build_values_reply_with_current_time(
    chips_parsed_frame_type *reply_to_send,
    const pds_runtime_state_type *runtime_state,
    uint8_t status,
    uint32_t requested_field_mask)
{
    build_values_reply_to_esp32(
        reply_to_send,
        runtime_state,
        status,
        requested_field_mask,
        millisecond_tick_timer_get_milliseconds_since_boot());
}
