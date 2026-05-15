/*
 * functions_to_request_mppt_input_sample_from_esp32.c
 *
 * The MPPT page hosts a simulated solar-panel I-V curve on the ESP32
 * (so we can demo MPPT convergence without a real panel). Every MPPT
 * iteration the firmware needs the panel's current (V, I) operating
 * point at the present duty cycle. This module is the synchronous
 * round-trip that asks the ESP32 for that point and waits for the
 * reply.
 *
 * Conceptually:
 *
 *     [SAMD21] --- request frame, command 0x37, payload = current duty ---> [ESP32]
 *     [SAMD21] <-- reply frame, same command, payload = (V_mV, I_mA, V_raw, I_raw)
 *
 * The exchange is request/response over the same UART link the
 * computer uses for everything else. Because that UART is shared,
 * other commands from the operator (e.g. "off", "stream_values")
 * might arrive while we're waiting for the sample reply — those get
 * dispatched immediately so the operator never feels the link freeze.
 *
 * Timeout is 20 ms. If no matching reply arrives in that window we
 * return 0 so the MPPT runner can mark "no valid sample" and bail
 * cleanly without commanding a new duty cycle.
 */

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

/* Total time we are willing to wait for the ESP32 to reply before
 * giving up. 20 ms is comfortably more than the round-trip on a 115200
 * baud link with a few-byte frame, but small enough that a missed
 * reply doesn't visibly stall the main loop. */
#define PDS_MPPT_INPUT_SAMPLE_REPLY_TIMEOUT_MS 20u

/* Sequence number for the next outgoing sample request. The CHIPS
 * protocol uses sequence numbers to pair a reply with its request when
 * multiple in-flight transactions could be confused; we use it here to
 * filter incoming frames in case other unrelated traffic arrives in the
 * same time window. Wraps at 256 (uint8_t), which is fine since we
 * only ever care about whether THIS reply matches THIS request. */
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

/*
 * Public entry point. Sends one sample-request frame, waits up to
 * 20 ms for the matching reply, fills *sample on success.
 *
 *   duty_cycle_as_fraction_of_65535 - the ESP32 needs to know the
 *       current operating point so it can compute the (V, I) that the
 *       simulated panel would produce at that duty cycle.
 *
 *   sample - filled on success with both engineering units (mV / mA)
 *       and raw 12-bit ADC counts (the MPPT algorithm uses the raw
 *       counts for precision).
 *
 * Returns 1 on a successful round-trip, 0 on timeout or any error.
 */
uint8_t request_mppt_input_sample_from_esp32_model(
    uint16_t duty_cycle_as_fraction_of_65535,
    pds_mppt_input_sample_type *sample)
{
    SATELLITE_ASSERT(sample != (void *)0);

    /* Build the outgoing frame in a local buffer, send it, then wait
     * for the matching reply. Splitting build + send + wait keeps each
     * function single-purpose. */
    chips_parsed_frame_type request;
    build_mppt_input_sample_request(duty_cycle_as_fraction_of_65535, &request);
    send_chips_frame_to_esp32(&request);

    return wait_for_matching_mppt_input_sample_reply(&request, sample);
}

/*
 * Pure-data helper: fill *request with the bytes that need to go on
 * the wire to ask for a sample at the given duty cycle.
 */
static void build_mppt_input_sample_request(
    uint16_t duty_cycle_as_fraction_of_65535,
    chips_parsed_frame_type *request)
{
    SATELLITE_ASSERT(request != (void *)0);

    uint16_t position = 0u;

    /* Take the next sequence number and bump the counter. The reply
     * we eventually get back must have the same sequence number to be
     * accepted as our reply. */
    request->sequence_number = next_sample_request_sequence_number;
    next_sample_request_sequence_number =
        (uint8_t)(next_sample_request_sequence_number + 1u);

    /* Command ID 0x37, defined in the board command contract. The
     * ESP32 sketch knows this ID means "give me a sample at this duty". */
    request->command_id = PDS_LINK_COMMAND_READ_MPPT_INPUT_SAMPLE;

    /* response_flag = 0 means this is a request, not a response.
     * The ESP32 sets this bit on its reply so we can tell the two
     * apart when filtering inbound frames. */
    request->response_flag = 0u;

    /* Payload is just the current duty cycle as a 16-bit little-endian
     * value. The ESP32 needs it to compute the operating point. */
    write_uint16_to_little_endian_payload(
        request->payload_bytes,
        &position,
        duty_cycle_as_fraction_of_65535);
    request->payload_length_in_bytes = position;
}

/*
 * Spin up to 20 ms reading bytes from the OBC UART (which is also the
 * link to the ESP32). Each byte goes through the CHIPS frame parser;
 * when a complete frame arrives we either accept it as our reply (and
 * return), or — if it's a different unrelated command from the
 * operator — execute it inline so the operator's commands aren't
 * delayed by the MPPT round-trip.
 *
 * The early-return on a matching reply means we don't actually wait
 * the full 20 ms in the common case; the timeout only fires when the
 * ESP32 didn't answer.
 */
static uint8_t wait_for_matching_mppt_input_sample_reply(
    const chips_parsed_frame_type *request,
    pds_mppt_input_sample_type *sample)
{
    SATELLITE_ASSERT(request != (void *)0);
    SATELLITE_ASSERT(sample != (void *)0);

    /* Local CHIPS parser instance — we don't share the main parser
     * because we're inside a tight wait loop and don't want to consume
     * partial frames from the main loop's perspective. */
    chips_frame_parser_state_type parser;
    chips_parsed_frame_type received_frame;
    chips_parser_initialize_state_machine_to_idle(&parser);

    uint32_t start_ms = millisecond_tick_timer_get_milliseconds_since_boot();

    while ((millisecond_tick_timer_get_milliseconds_since_boot() - start_ms)
           < PDS_MPPT_INPUT_SAMPLE_REPLY_TIMEOUT_MS)
    {
        /* Drain whatever has accumulated in the UART receive buffer
         * since the last loop iteration. If nothing has arrived yet
         * the inner while exits immediately and we re-check the
         * timeout at the outer while. */
        while (uart_obc_number_of_bytes_available_in_receive_buffer() > 0u)
        {
            chips_parser_result_type result =
                chips_parser_process_one_received_byte(
                    &parser,
                    uart_obc_read_one_byte_from_receive_buffer(),
                    &received_frame);

            /* The parser returns FRAME_READY only when it has a
             * complete, CRC-checked frame. Any other result means
             * "more bytes needed" — keep feeding it. */
            if (result != CHIPS_PARSER_RESULT_FRAME_READY)
            {
                continue;
            }

            /* This is THE reply we wanted — read the sample data
             * and we're done. */
            if (frame_is_matching_sample_reply(request, &received_frame) != 0u)
            {
                return read_sample_from_reply_payload(&received_frame, sample);
            }

            /* Not our reply, but if it's a fresh command from the
             * operator (response_flag == 0) we shouldn't make them
             * wait 20 ms. Dispatch it inline here. */
            if (received_frame.response_flag == 0u)
            {
                execute_command_that_arrived_while_waiting_for_sample(
                    &received_frame);
            }

            /* If response_flag was set but it wasn't OUR reply, just
             * drop it and keep waiting — it was probably a stale
             * response from a previous timed-out request. */
        }
    }

    /* Timed out. Caller treats this as "no sample this iteration". */
    return 0u;
}

/*
 * Predicate: is this incoming frame the reply to OUR request?
 * Three things must match: response flag set, command ID is the
 * sample-request command, sequence number matches the one we sent.
 */
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

/*
 * Decode the 9-byte reply payload into the sample struct.
 * Layout (matches what the ESP32 sketch puts on the wire):
 *
 *   byte  0:    status (0 = SUCCESS, anything else = failure)
 *   bytes 1-2:  panel voltage in mV  (little-endian uint16)
 *   bytes 3-4:  panel current in mA  (little-endian uint16)
 *   bytes 5-6:  panel voltage raw 12-bit ADC count
 *   bytes 7-8:  panel current raw 12-bit ADC count
 */
static uint8_t read_sample_from_reply_payload(
    const chips_parsed_frame_type *reply,
    pds_mppt_input_sample_type *sample)
{
    SATELLITE_ASSERT(reply != (void *)0);
    SATELLITE_ASSERT(sample != (void *)0);

    /* Length sanity check. If the ESP32 sent something other than the
     * 9-byte reply we expect, treat it as garbage. */
    if (reply->payload_length_in_bytes
        != PDS_MPPT_INPUT_SAMPLE_REPLY_LENGTH_IN_BYTES)
    {
        return 0u;
    }

    /* The first byte is the status code. SUCCESS means the ESP32
     * actually had a configured curve and could produce a sample;
     * anything else means it bailed (no curve set yet, etc.). */
    if (reply->payload_bytes[0] != (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS)
    {
        return 0u;
    }

    /* Engineering units first, then raw ADC counts. */
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

/*
 * The OBC UART is shared between MPPT sample requests and operator
 * commands. If the operator presses Off (or any other button) while
 * we're waiting for an MPPT sample reply, the command frame arrives
 * here in the wait loop. We dispatch it immediately through the same
 * command-execution path the main loop uses, so the operator never
 * feels a 20 ms freeze.
 */
static void execute_command_that_arrived_while_waiting_for_sample(
    const chips_parsed_frame_type *received_command)
{
    chips_parsed_frame_type reply;
    execute_command_from_esp32_and_build_reply(received_command, &reply);
    send_chips_frame_to_esp32(&reply);
}
