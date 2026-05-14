#include "communication_with_esp32/functions_to_read_chips_commands_received_from_esp32.h"

#include <stdint.h>

#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "communication_with_esp32/chips_reply_sending/functions_to_send_chips_replies_to_esp32.h"
#include "communication_with_esp32/command_execution/functions_to_execute_board_commands_received_from_esp32.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "uart_obc.h"

static chips_frame_parser_state_type chips_message_reader;
static chips_parsed_frame_type received_chips_message;

static uint8_t uart_has_unread_bytes_from_esp32(void);
static uint8_t read_one_uart_byte_from_esp32(void);
static chips_parser_result_type update_chips_message_reader_with_received_byte(
    uint8_t received_byte);
static void react_to_chips_message_reader_result(
    chips_parser_result_type parser_result);

static void wait_for_more_uart_bytes_before_doing_anything_else(void);
static void execute_valid_chips_command_and_send_reply(void);

void start_esp32_command_reader_with_empty_message_state(void)
{
    // Requirement: after reset, the board must not treat random RAM as a
    // partly received command. This makes the CHIPS reader wait for the first
    // frame start byte before accepting any command from the ESP32.
    chips_parser_initialize_state_machine_to_idle(&chips_message_reader);
}

void read_and_execute_commands_from_esp32(void)
{
    // Requirement: commands travel from computer -> ESP32 -> board.
    // A CHIPS command arrives as several UART bytes, so the board must read
    // every byte waiting in the UART receive buffer before returning to the
    // rest of the main loop.
    while (uart_has_unread_bytes_from_esp32())
    {
        // Requirement: the board must not miss command bytes.
        // This removes exactly one byte from the UART driver's RAM buffer.
        uint8_t received_byte = read_one_uart_byte_from_esp32();

        // Requirement: board commands must use the CHIPS packet format.
        // The CHIPS reader remembers partial messages across calls and says
        // when the bytes form a complete command or a broken command.
        chips_parser_result_type parser_result =
            update_chips_message_reader_with_received_byte(received_byte);

        // Requirement: valid commands may update runtime state, but broken
        // commands must not change mode, PWM requests, load requests, or
        // heater requests.
        react_to_chips_message_reader_result(parser_result);
    }
}

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

static void react_to_chips_message_reader_result(
    chips_parser_result_type parser_result)
{
    if (parser_result == CHIPS_PARSER_RESULT_INCOMPLETE)
    {
        wait_for_more_uart_bytes_before_doing_anything_else();
        return;
    }

    if (parser_result == CHIPS_PARSER_RESULT_FRAME_READY)
    {
        record_valid_chips_frame_from_esp32();
        execute_valid_chips_command_and_send_reply();
        return;
    }

    record_broken_chips_message_without_changing_outputs(parser_result);
}

static void wait_for_more_uart_bytes_before_doing_anything_else(void)
{
    // No complete command exists yet.
    // The right action for this byte is to change nothing. The outer loop
    // may read more waiting bytes, and the main loop will keep running safety
    // checks while the rest of the CHIPS message arrives.
}

static void execute_valid_chips_command_and_send_reply(void)
{
    chips_parsed_frame_type reply;
    execute_command_from_esp32_and_build_reply(&received_chips_message, &reply);
    send_chips_frame_to_esp32(&reply);
}
