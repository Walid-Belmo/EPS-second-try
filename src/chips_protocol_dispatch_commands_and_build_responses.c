/* =============================================================================
 * chips_protocol_dispatch_commands_and_build_responses.c
 * Dispatches received CHIPS commands to per-command handler functions,
 * builds response frames, and sends them over the OBC UART.
 *
 * Category: APPLICATION CODE (uses UART driver for sending, pure logic
 *           for command handling and response building)
 *
 * Idempotency: the CHIPS protocol requires that commands with the same
 * sequence number are executed exactly once, but a response is sent every
 * time. This module caches the last response in wire format. If a
 * duplicate sequence number arrives, the cached response is re-sent
 * without re-executing the command.
 *
 * Telemetry: all 219 bytes are placeholder zeros in Phase 4. Phase 6
 * sensor drivers will fill them with real ADC/I2C/SPI readings.
 * =============================================================================
 */

#include <stdint.h>
#include <stdbool.h>

#include "assertion_handler.h"
#include "debug_functions.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "chips_protocol_dispatch_commands_and_build_responses.h"
#include "uart_obc_sercom0_pa04_pa05.h"
#include "millisecond_tick_timer_using_arm_systick.h"

/* ── Constants ────────────────────────────────────────────────────────────── */

/* Maximum wire-format frame size for cached responses. A telemetry response
 * is ~230 bytes after framing. 512 bytes handles worst-case stuffing. */
#define MAXIMUM_CACHED_RESPONSE_SIZE_IN_BYTES  512u

/* Chunk size for sending large frames through the 256-byte UART TX ring
 * buffer. After sending one chunk, we wait for it to drain before sending
 * the next. At 115200 baud, 200 bytes take ~17ms to transmit. */
#define UART_SEND_CHUNK_SIZE_IN_BYTES          200u

/* Milliseconds to wait between send chunks. Must be long enough for
 * the UART interrupt handler to drain the previous chunk. 20ms is
 * conservative for 200 bytes at 115200 baud (~17.4ms actual). */
#define MILLISECONDS_TO_WAIT_BETWEEN_CHUNKS    20u

/* Maximum number of chunks to prevent infinite loops (Rule C2). */
#define MAXIMUM_SEND_CHUNKS                    4u

/* ── Module state ─────────────────────────────────────────────────────────── */

static struct chips_command_dispatch_module_state {
    /* Idempotency: track last sequence number and cache the response */
    uint8_t  last_processed_sequence_number;
    uint8_t  has_processed_at_least_one_command;

    /* Cached wire-format response for idempotent retransmission. */
    uint8_t  cached_response_wire_format_bytes[MAXIMUM_CACHED_RESPONSE_SIZE_IN_BYTES];
    uint16_t cached_response_total_length_in_bytes;

    /* Telemetry data — placeholder zeros until Phase 6 sensor drivers. */
    eps_telemetry_data_type current_telemetry_readings;
} dispatch_state;

/* ── Private function prototypes ─────────────────────────────────────────── */

static void handle_get_telemetry_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);

static void handle_set_parameter_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);

static void handle_get_state_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);

static void handle_set_mode_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);

static void handle_shed_load_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);

static void handle_deploy_antenna_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);

static void build_error_response_for_unknown_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);

static void send_wire_format_response_over_obc_uart(
    const uint8_t *wire_bytes,
    uint16_t total_wire_length);

/* ── Public functions ─────────────────────────────────────────────────────── */

void chips_command_dispatch_initialize(void)
{
    dispatch_state.last_processed_sequence_number = 0u;
    dispatch_state.has_processed_at_least_one_command = 0u;
    dispatch_state.cached_response_total_length_in_bytes = 0u;

    /* Zero all telemetry fields (placeholder until Phase 6). */
    uint8_t *telemetry_bytes =
        (uint8_t *)&dispatch_state.current_telemetry_readings;
    for (uint16_t i = 0u; i < EPS_TELEMETRY_DATA_SIZE_IN_BYTES; i += 1u)
    {
        telemetry_bytes[i] = 0u;
    }
}

void chips_dispatch_received_command_and_send_response(
    const chips_parsed_frame_type *received_command_frame)
{
    SATELLITE_ASSERT(received_command_frame != (void *)0);

    /* Ignore response frames — the EPS is a slave and should never
     * receive responses (only the OBC sends commands to us). */
    if (received_command_frame->response_flag != 0u)
    {
        DEBUG_LOG_TEXT("CHIPS: ignoring response frame (EPS is slave)");
        return;
    }

    /* ── Idempotency check ────────────────────────────────────────── */
    if (dispatch_state.has_processed_at_least_one_command != 0u)
    {
        if (received_command_frame->sequence_number ==
            dispatch_state.last_processed_sequence_number)
        {
            /* Duplicate sequence number — re-send cached response
             * without re-executing the command. */
            DEBUG_LOG_TEXT("CHIPS: duplicate seq, resending cached response");
            DEBUG_LOG_UINT("  seq", (uint32_t)received_command_frame->sequence_number);

            send_wire_format_response_over_obc_uart(
                dispatch_state.cached_response_wire_format_bytes,
                dispatch_state.cached_response_total_length_in_bytes);
            return;
        }
    }

    /* ── New command — dispatch to the appropriate handler ─────────── */
    chips_parsed_frame_type response_frame;

    /* Response always mirrors the sequence number and command ID. */
    response_frame.sequence_number =
        received_command_frame->sequence_number;
    response_frame.command_id =
        received_command_frame->command_id;
    response_frame.response_flag = 1u;

    switch (received_command_frame->command_id)
    {
    case CHIPS_COMMAND_ID_GET_TELEMETRY:
        handle_get_telemetry_command(received_command_frame,
                                     &response_frame);
        break;

    case CHIPS_COMMAND_ID_SET_PARAMETER:
        handle_set_parameter_command(received_command_frame,
                                     &response_frame);
        break;

    case CHIPS_COMMAND_ID_GET_STATE:
        handle_get_state_command(received_command_frame,
                                 &response_frame);
        break;

    case CHIPS_COMMAND_ID_SET_MODE:
        handle_set_mode_command(received_command_frame,
                                &response_frame);
        break;

    case CHIPS_COMMAND_ID_SHED_LOAD:
        handle_shed_load_command(received_command_frame,
                                 &response_frame);
        break;

    case CHIPS_COMMAND_ID_DEPLOY_ANTENNA:
        handle_deploy_antenna_command(received_command_frame,
                                      &response_frame);
        break;

    default:
        build_error_response_for_unknown_command(received_command_frame,
                                                  &response_frame);
        break;
    }

    /* ── Build wire-format response and cache it ──────────────────── */
    uint16_t wire_length =
        chips_build_stuffed_frame_with_sync_and_crc_into_buffer(
            &response_frame,
            dispatch_state.cached_response_wire_format_bytes,
            MAXIMUM_CACHED_RESPONSE_SIZE_IN_BYTES);

    SATELLITE_ASSERT(wire_length > 0u);

    dispatch_state.cached_response_total_length_in_bytes = wire_length;
    dispatch_state.last_processed_sequence_number =
        received_command_frame->sequence_number;
    dispatch_state.has_processed_at_least_one_command = 1u;

    /* ── Send the response ────────────────────────────────────────── */
    send_wire_format_response_over_obc_uart(
        dispatch_state.cached_response_wire_format_bytes,
        wire_length);
}

/* ── Private functions — command handlers ────────────────────────────────── */

static void handle_get_telemetry_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    (void)received_command;

    DEBUG_LOG_TEXT("CHIPS: executing GET_TELEMETRY");

    /* Payload: status byte + 219 bytes telemetry data. */
    response_to_build->payload_bytes[0] =
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;

    const uint8_t *telemetry_source =
        (const uint8_t *)&dispatch_state.current_telemetry_readings;

    for (uint16_t i = 0u; i < EPS_TELEMETRY_DATA_SIZE_IN_BYTES; i += 1u)
    {
        response_to_build->payload_bytes[1u + i] = telemetry_source[i];
    }

    response_to_build->payload_length_in_bytes =
        1u + EPS_TELEMETRY_DATA_SIZE_IN_BYTES;
}

static void handle_set_parameter_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    /* Expected payload: param_id(1) + value(4) = 5 bytes. */
    if (received_command->payload_length_in_bytes != 5u)
    {
        DEBUG_LOG_TEXT("CHIPS: SET_PARAMETER invalid payload length");
        DEBUG_LOG_UINT("  expected", 5u);
        DEBUG_LOG_UINT("  received",
                       (uint32_t)received_command->payload_length_in_bytes);

        response_to_build->payload_bytes[0] =
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH;
        response_to_build->payload_length_in_bytes = 1u;
        return;
    }

    uint8_t parameter_id = received_command->payload_bytes[0];
    DEBUG_LOG_TEXT("CHIPS: executing SET_PARAMETER");
    DEBUG_LOG_UINT("  param_id", (uint32_t)parameter_id);

    /* Phase 4: accept and log, no actual parameter storage yet. */
    response_to_build->payload_bytes[0] =
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;
    response_to_build->payload_length_in_bytes = 1u;
}

static void handle_get_state_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    (void)received_command;

    DEBUG_LOG_TEXT("CHIPS: executing GET_STATE");

    /* Payload: status(1) + state_id(1) + sub_state(1). */
    response_to_build->payload_bytes[0] =
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;
    response_to_build->payload_bytes[1] = 0x00u; /* state: nominal */
    response_to_build->payload_bytes[2] = 0x00u; /* sub-state: idle */
    response_to_build->payload_length_in_bytes = 3u;
}

static void handle_set_mode_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes != 1u)
    {
        response_to_build->payload_bytes[0] =
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH;
        response_to_build->payload_length_in_bytes = 1u;
        return;
    }

    uint8_t requested_mode = received_command->payload_bytes[0];
    DEBUG_LOG_TEXT("CHIPS: executing SET_MODE");
    DEBUG_LOG_UINT("  mode_id", (uint32_t)requested_mode);

    /* Phase 4: accept and log, no actual mode change yet. */
    response_to_build->payload_bytes[0] =
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;
    response_to_build->payload_length_in_bytes = 1u;
}

static void handle_shed_load_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes != 1u)
    {
        response_to_build->payload_bytes[0] =
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH;
        response_to_build->payload_length_in_bytes = 1u;
        return;
    }

    uint8_t load_mask = received_command->payload_bytes[0];
    DEBUG_LOG_TEXT("CHIPS: executing SHED_LOAD");
    DEBUG_LOG_UINT("  load_mask", (uint32_t)load_mask);

    /* Phase 4: accept and log, no actual load shedding yet. */
    response_to_build->payload_bytes[0] =
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;
    response_to_build->payload_length_in_bytes = 1u;
}

static void handle_deploy_antenna_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    (void)received_command;

    DEBUG_LOG_TEXT("CHIPS: executing DEPLOY_ANTENNA");

    /* Phase 4: accept and log, no actual deployment yet. */
    response_to_build->payload_bytes[0] =
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;
    response_to_build->payload_length_in_bytes = 1u;
}

static void build_error_response_for_unknown_command(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    DEBUG_LOG_TEXT("CHIPS: unknown command");
    DEBUG_LOG_UINT("  cmd_id", (uint32_t)received_command->command_id);

    response_to_build->payload_bytes[0] =
        (uint8_t)CHIPS_RESPONSE_STATUS_UNKNOWN_COMMAND;
    response_to_build->payload_length_in_bytes = 1u;
}

/* ── Private functions — UART sending with chunked flow control ──────────── */

static void send_wire_format_response_over_obc_uart(
    const uint8_t *wire_bytes,
    uint16_t total_wire_length)
{
    SATELLITE_ASSERT(wire_bytes != (void *)0);
    SATELLITE_ASSERT(total_wire_length > 0u);

    /* If the frame fits in one chunk, send it directly. This is the
     * common case — a telemetry response is ~230 bytes, and the UART
     * TX ring buffer holds 255 bytes. */
    if (total_wire_length <= UART_SEND_CHUNK_SIZE_IN_BYTES)
    {
        uart_obc_send_bytes(wire_bytes, (uint32_t)total_wire_length);
        return;
    }

    /* Frame exceeds one chunk — send in pieces with timed waits
     * between them to let the UART interrupt drain the TX buffer. */
    uint16_t bytes_sent = 0u;

    for (uint32_t chunk_number = 0u;
         chunk_number < MAXIMUM_SEND_CHUNKS;
         chunk_number += 1u)
    {
        uint16_t remaining = total_wire_length - bytes_sent;
        if (remaining == 0u)
        {
            break;
        }

        uint16_t chunk_size = remaining;
        if (chunk_size > UART_SEND_CHUNK_SIZE_IN_BYTES)
        {
            chunk_size = UART_SEND_CHUNK_SIZE_IN_BYTES;
        }

        uart_obc_send_bytes(&wire_bytes[bytes_sent],
                            (uint32_t)chunk_size);
        bytes_sent += chunk_size;

        /* Wait for the UART interrupt to drain the chunk before
         * sending the next one. Without this wait, the TX ring buffer
         * would overflow and drop bytes. */
        if (bytes_sent < total_wire_length)
        {
            uint32_t wait_start_ms =
                millisecond_tick_timer_get_milliseconds_since_boot();

            /* Bounded wait — never spin forever (Rule C2). */
            while ((millisecond_tick_timer_get_milliseconds_since_boot()
                    - wait_start_ms)
                   < MILLISECONDS_TO_WAIT_BETWEEN_CHUNKS)
            {
                /* Intentionally empty — waiting for UART to drain. */
            }
        }
    }

    DEBUG_LOG_UINT("CHIPS: response sent, bytes",
                   (uint32_t)bytes_sent);
}
