/*
 * functions_to_read_chips_commands_received_from_esp32.c
 *
 * The "incoming command" half of the firmware's communication path.
 * Called once per main-loop iteration to:
 *   1. Drain whatever bytes have accumulated in the UART receive
 *      buffer since the last iteration.
 *   2. Feed each byte through the CHIPS frame parser, which stitches
 *      bytes into complete frames and validates the CRC.
 *   3. When a complete valid frame arrives, hand it to the command
 *      execution dispatcher and send the dispatcher's reply back.
 *   4. When a frame is broken (CRC mismatch, too long, etc.), bump
 *      the corresponding counter in runtime state but DON'T change
 *      any output — broken commands must never affect mode, PWM,
 *      switches, or LEDs.
 *
 * The data path:
 *
 *     [ESP32 USB serial] -> board UART -> uart_obc receive buffer
 *                                              ↓
 *                            this file (drains buffer, parses frames)
 *                                              ↓
 *                  command_execution dispatcher (decides what to do)
 *                                              ↓
 *                      chips_reply_sending (writes reply to UART)
 *                                              ↓
 *     [ESP32 USB serial] <- board UART <- (reply bytes)
 *
 * The CHIPS parser is stateful — it remembers partial frames between
 * calls — so the same `chips_message_reader` instance is reused across
 * every main-loop iteration. That's why it's a file-scope `static`
 * struct rather than a stack local.
 */

#include "communication_with_esp32/functions_to_read_chips_commands_received_from_esp32.h"

#include <stdint.h>

#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "communication_with_esp32/chips_reply_sending/functions_to_send_chips_replies_to_esp32.h"
#include "communication_with_esp32/command_execution/functions_to_execute_board_commands_received_from_esp32.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "uart_obc.h"

/* The persistent CHIPS parser instance. Holds the partial-frame state
 * that survives between main-loop iterations: the parser may have
 * consumed half a frame on one iteration and the rest on the next. */
static chips_frame_parser_state_type chips_message_reader;

/* Scratch buffer the parser fills in when a complete frame is ready.
 * Re-used every frame; the dispatcher copies anything it needs out
 * before this gets overwritten. */
static chips_parsed_frame_type received_chips_message;

static uint8_t uart_has_unread_bytes_from_esp32(void);
static uint8_t read_one_uart_byte_from_esp32(void);
static chips_parser_result_type update_chips_message_reader_with_received_byte(
    uint8_t received_byte);
static void react_to_chips_message_reader_result(
    chips_parser_result_type parser_result);

static void wait_for_more_uart_bytes_before_doing_anything_else(void);
static void execute_valid_chips_command_and_send_reply(void);

/*
 * Called once at boot from main(). Resets the CHIPS parser to its
 * idle state so it waits for a frame-start byte before accepting any
 * payload data.
 *
 * Without this call the parser's state struct would contain whatever
 * RAM happened to be at that address at boot, which the parser might
 * mistake for a partial frame already in progress — leading to the
 * first real frame being misread or silently discarded.
 */
void start_esp32_command_reader_with_empty_message_state(void)
{
    chips_parser_initialize_state_machine_to_idle(&chips_message_reader);
}

/*
 * Called once per main-loop iteration. Drains the UART receive buffer
 * and dispatches any complete frames. Returns when the buffer is
 * empty so the rest of the main loop (mode runner, safety pass,
 * hardware write, etc.) can run promptly.
 *
 * If a frame straddles two main-loop iterations (the operator's
 * command was longer than what fit in one UART read), the parser's
 * state remembers the first half until the next call here picks up
 * the rest. The main loop keeps running safety checks during that
 * gap — incoming bytes never block anything else.
 */
void read_and_execute_commands_from_esp32(void)
{
    /* Loop bound is the buffer drain. Each iteration consumes one
     * byte. The conventions doc requires a compile-time-bounded loop;
     * here the bound is "the UART buffer's RAM size" (256 bytes,
     * defined in the UART driver). The while-condition exits as soon
     * as the buffer is empty. */
    while (uart_has_unread_bytes_from_esp32())
    {
        /* Pull one byte out of the UART driver's RAM buffer. The
         * underlying SERCOM ISR fills that buffer in the background;
         * this is the consumer side. */
        uint8_t received_byte = read_one_uart_byte_from_esp32();

        /* Feed the byte to the parser. The parser tells us one of:
         *   - INCOMPLETE: more bytes needed before the frame is whole
         *   - FRAME_READY: a complete, CRC-valid frame is in
         *     received_chips_message and is ready to dispatch
         *   - ERROR_CRC_MISMATCH / ERROR_FRAME_TOO_LONG: garbage,
         *     drop it and bump the counter */
        chips_parser_result_type parser_result =
            update_chips_message_reader_with_received_byte(received_byte);

        /* Branch on the parser result. Valid frames flow into the
         * dispatcher; broken frames just bump a counter; partial
         * frames quietly wait. */
        react_to_chips_message_reader_result(parser_result);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Tiny one-line wrappers around the UART driver. Wrapping is
 * cosmetic — it lets the loop body above read like English ("if uart
 * has unread bytes from ESP32, read one byte from ESP32 ...") instead
 * of bare register-driver calls.
 * ────────────────────────────────────────────────────────────────── */

static uint8_t uart_has_unread_bytes_from_esp32(void)
{
    return (uart_obc_number_of_bytes_available_in_receive_buffer() > 0u)
        ? 1u
        : 0u;
}

static uint8_t read_one_uart_byte_from_esp32(void)
{
    return uart_obc_read_one_byte_from_receive_buffer();
}

static chips_parser_result_type update_chips_message_reader_with_received_byte(
    uint8_t received_byte)
{
    return chips_parser_process_one_received_byte(
        &chips_message_reader,
        received_byte,
        &received_chips_message);
}

/*
 * The branch on parser result. Three cases, each with its own helper.
 */
static void react_to_chips_message_reader_result(
    chips_parser_result_type parser_result)
{
    /* Most common case: the byte was consumed but the frame is not
     * yet complete. Do nothing — the next main-loop iteration will
     * drain more bytes and eventually complete the frame. */
    if (parser_result == CHIPS_PARSER_RESULT_INCOMPLETE)
    {
        wait_for_more_uart_bytes_before_doing_anything_else();
        return;
    }

    /* A complete, CRC-valid frame is ready. Bump the counter (so the
     * status reply can show how many valid frames have been received
     * over the link's lifetime) and dispatch. */
    if (parser_result == CHIPS_PARSER_RESULT_FRAME_READY)
    {
        record_valid_chips_frame_from_esp32();
        execute_valid_chips_command_and_send_reply();
        return;
    }

    /* Anything else is an error result: CRC mismatch, frame too long,
     * etc. Bump the appropriate counter so the operator can see the
     * link's quality, but DO NOT change any output. A broken command
     * must not be allowed to change mode, PWM, switches, or
     * anything else — that's the whole point of CRC. */
    record_broken_chips_message_without_changing_outputs(parser_result);
}

/*
 * Intentionally empty. This function exists so the if-branch above
 * can read self-explanatorily ("wait for more uart bytes before doing
 * anything else") instead of having a bare `return;`. The actual
 * "wait" is just letting the outer loop continue and the main loop
 * iterate; we're still doing the safety pass and the mode runner
 * during that wait.
 */
static void wait_for_more_uart_bytes_before_doing_anything_else(void)
{
}

/*
 * The valid-frame path. The dispatcher fills `reply` with the bytes
 * that should go back; the chips_reply_sending module wraps those
 * bytes in a CHIPS frame and writes them to the UART transmit
 * buffer. The reply is always sent — even an "unknown command" reply
 * matters, because the operator's web app waits for SOMETHING and
 * would otherwise time out.
 */
static void execute_valid_chips_command_and_send_reply(void)
{
    chips_parsed_frame_type reply;
    execute_command_from_esp32_and_build_reply(&received_chips_message, &reply);
    send_chips_frame_to_esp32(&reply);
}
