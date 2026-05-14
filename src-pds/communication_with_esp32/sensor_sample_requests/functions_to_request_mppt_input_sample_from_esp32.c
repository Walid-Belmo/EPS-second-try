#include <stdint.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "shared_helpers/functions_to_read_and_write_little_endian_values.h"
#include "uart_obc.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"
#include "communication_with_esp32/chips_reply_sending/functions_to_send_chips_replies_to_esp32.h"
#include "communication_with_esp32/command_execution/functions_to_execute_board_commands_received_from_esp32.h"
#include "communication_with_esp32/sensor_sample_requests/functions_to_request_mppt_input_sample_from_esp32.h"

#define PDS_MPPT_INPUT_SAMPLE_REPLY_TIMEOUT_MS 20u

static uint8_t next_sample_request_sequence_number;

static void build_mppt_input_sample_request(
    uint16_t duty_cycle_as_fraction_of_65535,
    chips_parsed_frame_type *request);
static uint8_t wait_for_matching_mppt_input_sample_reply(
    const chips_parsed_frame_type *request,
    pds_mppt_input_sample_type *sample);
static uint8_t frame_is_matching_sample_reply(
    const chips_parsed_frame_type *request,
    const chips_parsed_frame_type *candidate_reply);
static uint8_t read_sample_from_reply_payload(
    const chips_parsed_frame_type *reply,
    pds_mppt_input_sample_type *sample);
static void execute_command_that_arrived_while_waiting_for_sample(
    const chips_parsed_frame_type *received_command);

uint8_t request_mppt_input_sample_from_esp32_model(
    uint16_t duty_cycle_as_fraction_of_65535,
    pds_mppt_input_sample_type *sample)
{
    SATELLITE_ASSERT(sample != (void *)0);

    chips_parsed_frame_type request;
    build_mppt_input_sample_request(duty_cycle_as_fraction_of_65535, &request);
    send_chips_frame_to_esp32(&request);

    return wait_for_matching_mppt_input_sample_reply(&request, sample);
}

static void build_mppt_input_sample_request(
    uint16_t duty_cycle_as_fraction_of_65535,
    chips_parsed_frame_type *request)
{
    SATELLITE_ASSERT(request != (void *)0);

    uint16_t position = 0u;
    request->sequence_number = next_sample_request_sequence_number;
    next_sample_request_sequence_number =
        (uint8_t)(next_sample_request_sequence_number + 1u);
    request->command_id = PDS_LINK_COMMAND_READ_MPPT_INPUT_SAMPLE;
    request->response_flag = 0u;
    write_uint16_to_little_endian_payload(
        request->payload_bytes,
        &position,
        duty_cycle_as_fraction_of_65535);
    request->payload_length_in_bytes = position;
}

static uint8_t wait_for_matching_mppt_input_sample_reply(
    const chips_parsed_frame_type *request,
    pds_mppt_input_sample_type *sample)
{
    SATELLITE_ASSERT(request != (void *)0);
    SATELLITE_ASSERT(sample != (void *)0);

    chips_frame_parser_state_type parser;
    chips_parsed_frame_type received_frame;
    chips_parser_initialize_state_machine_to_idle(&parser);

    uint32_t start_ms = millisecond_tick_timer_get_milliseconds_since_boot();
    while ((millisecond_tick_timer_get_milliseconds_since_boot() - start_ms)
           < PDS_MPPT_INPUT_SAMPLE_REPLY_TIMEOUT_MS)
    {
        while (uart_obc_number_of_bytes_available_in_receive_buffer() > 0u)
        {
            chips_parser_result_type result =
                chips_parser_process_one_received_byte(
                    &parser,
                    uart_obc_read_one_byte_from_receive_buffer(),
                    &received_frame);

            if (result != CHIPS_PARSER_RESULT_FRAME_READY)
            {
                continue;
            }

            if (frame_is_matching_sample_reply(request, &received_frame) != 0u)
            {
                return read_sample_from_reply_payload(&received_frame, sample);
            }

            if (received_frame.response_flag == 0u)
            {
                execute_command_that_arrived_while_waiting_for_sample(
                    &received_frame);
            }
        }
    }

    return 0u;
}

static uint8_t frame_is_matching_sample_reply(
    const chips_parsed_frame_type *request,
    const chips_parsed_frame_type *candidate_reply)
{
    return ((candidate_reply->response_flag != 0u)
        && (candidate_reply->command_id == PDS_LINK_COMMAND_READ_MPPT_INPUT_SAMPLE)
        && (candidate_reply->sequence_number == request->sequence_number))
        ? 1u
        : 0u;
}

static uint8_t read_sample_from_reply_payload(
    const chips_parsed_frame_type *reply,
    pds_mppt_input_sample_type *sample)
{
    SATELLITE_ASSERT(reply != (void *)0);
    SATELLITE_ASSERT(sample != (void *)0);

    if (reply->payload_length_in_bytes
        != PDS_MPPT_INPUT_SAMPLE_REPLY_LENGTH_IN_BYTES)
    {
        return 0u;
    }

    if (reply->payload_bytes[0] != (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS)
    {
        return 0u;
    }

    sample->panel_voltage_in_millivolts =
        read_uint16_from_little_endian_bytes(&reply->payload_bytes[1]);
    sample->panel_current_in_milliamps =
        read_uint16_from_little_endian_bytes(&reply->payload_bytes[3]);
    sample->panel_voltage_raw_adc_reading =
        read_uint16_from_little_endian_bytes(&reply->payload_bytes[5]);
    sample->panel_current_raw_adc_reading =
        read_uint16_from_little_endian_bytes(&reply->payload_bytes[7]);
    return 1u;
}

static void execute_command_that_arrived_while_waiting_for_sample(
    const chips_parsed_frame_type *received_command)
{
    chips_parsed_frame_type reply;
    execute_command_from_esp32_and_build_reply(received_command, &reply);
    send_chips_frame_to_esp32(&reply);
}
