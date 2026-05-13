#include <stdint.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"
#include "communication_with_esp32/command_execution/payload_parsing/functions_to_parse_payloads_inside_board_commands.h"
#include "communication_with_esp32/command_execution/functions_to_execute_board_commands_received_from_esp32.h"
#include "shared_helpers/functions_to_read_and_write_little_endian_values.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"
#include "command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.h"
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
        get_pointer_to_pds_runtime_ram_values();
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
    else
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_UNKNOWN_COMMAND);
    }

    remember_result_of_command_from_esp32(received_command, reply_to_send);
}

static void prepare_reply_header(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send)
{
    reply_to_send->sequence_number = received_command->sequence_number;
    reply_to_send->command_id = received_command->command_id;
    reply_to_send->response_flag = 1u;
    reply_to_send->payload_length_in_bytes = 0u;
}

static void handle_off_command(
    pds_runtime_state_type *runtime_state,
    chips_parsed_frame_type *reply_to_send)
{
    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_OFF;
    runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    request_no_pwm_output_in_runtime_state(runtime_state);
    build_ack_reply_to_esp32(
        reply_to_send,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
}

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

    pds_mppt_demo_curve_type curve =
        read_mppt_demo_curve_from_command_payload(
            received_command->payload_bytes);
    if (mppt_demo_curve_is_valid(&curve) == 0u)
    {
        build_ack_reply_to_esp32(
            reply_to_send,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    runtime_state->mppt_curve = curve;
    runtime_state->requested_mode = (uint8_t)PDS_REQUESTED_MODE_MPPT_TEST;
    reset_mppt_demo_ram_values_after_new_curve();
    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        (PDS_VALUE_FIELD_MPPT | PDS_VALUE_FIELD_MODE));
}

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
    reset_state_demo_ram_values_after_new_inputs();
    build_values_reply_with_current_time(
        reply_to_send,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        (PDS_VALUE_FIELD_STATE | PDS_VALUE_FIELD_INPUTS));
}

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
