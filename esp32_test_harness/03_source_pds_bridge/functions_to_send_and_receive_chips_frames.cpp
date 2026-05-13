#include "functions_to_send_and_receive_chips_frames.h"

static uint16_t append_stuffed_byte(
    uint8_t byte_to_write,
    uint8_t *wire_bytes,
    uint16_t position,
    uint16_t buffer_size);
static chips_parse_result_type validate_complete_frame(
    chips_parser_type *parser,
    chips_frame_type *output_frame);
static void extract_frame_fields(
    const uint8_t *content,
    uint16_t content_length,
    chips_frame_type *output_frame);

void reset_chips_parser(chips_parser_type *parser)
{
    parser->current_state = CHIPS_PARSER_WAITING_FOR_SYNC_BYTE;
    parser->write_position = 0u;
}

uint16_t compute_chips_crc16_kermit(const uint8_t *bytes, uint16_t length)
{
    uint16_t crc = 0u;
    for (uint16_t byte_index = 0u; byte_index < length; byte_index += 1u)
    {
        crc ^= bytes[byte_index];
        for (uint8_t bit_index = 0u; bit_index < 8u; bit_index += 1u)
        {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1u) ^ 0x8408u)
                             : (uint16_t)(crc >> 1u);
        }
    }
    return crc;
}

uint16_t build_chips_frame_for_uart(
    const chips_frame_type *frame,
    uint8_t *wire_bytes,
    uint16_t wire_buffer_size)
{
    uint8_t content[CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES];
    uint16_t content_length = 0u;
    content[content_length++] = frame->sequence_number;
    content[content_length++] =
        (uint8_t)((frame->response_flag << CHIPS_RESPONSE_FLAG_BIT_POSITION)
                  | (frame->command_id & CHIPS_COMMAND_ID_BIT_MASK));

    for (uint16_t i = 0u; i < frame->payload_length_in_bytes; i += 1u)
    {
        content[content_length++] = frame->payload_bytes[i];
    }

    uint16_t crc = compute_chips_crc16_kermit(content, content_length);
    content[content_length++] = (uint8_t)(crc & 0xFFu);
    content[content_length++] = (uint8_t)((crc >> 8u) & 0xFFu);

    uint16_t position = 0u;
    wire_bytes[position++] = CHIPS_FRAME_SYNC_BYTE;
    for (uint16_t i = 0u; i < content_length; i += 1u)
    {
        position = append_stuffed_byte(
            content[i], wire_bytes, position, wire_buffer_size);
        if (position == 0u)
        {
            return 0u;
        }
    }

    if (position >= wire_buffer_size)
    {
        return 0u;
    }
    wire_bytes[position++] = CHIPS_FRAME_SYNC_BYTE;
    return position;
}

void send_chips_frame_to_board(Stream &board_uart, const chips_frame_type *frame)
{
    uint8_t wire_bytes[CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES];
    uint16_t wire_length =
        build_chips_frame_for_uart(frame, wire_bytes, sizeof(wire_bytes));
    if (wire_length > 0u)
    {
        board_uart.write(wire_bytes, wire_length);
    }
}

chips_parse_result_type parse_one_chips_byte(
    chips_parser_type *parser,
    uint8_t byte_received,
    chips_frame_type *output_frame)
{
    if (parser->current_state == CHIPS_PARSER_WAITING_FOR_SYNC_BYTE)
    {
        if (byte_received == CHIPS_FRAME_SYNC_BYTE)
        {
            parser->current_state = CHIPS_PARSER_COLLECTING_FRAME_DATA;
            parser->write_position = 0u;
        }
        return CHIPS_PARSE_INCOMPLETE;
    }

    if (parser->current_state == CHIPS_PARSER_PROCESSING_ESCAPE_BYTE)
    {
        byte_received ^= CHIPS_ESCAPE_XOR_VALUE;
        parser->current_state = CHIPS_PARSER_COLLECTING_FRAME_DATA;
    }
    else if (byte_received == CHIPS_FRAME_ESCAPE_BYTE)
    {
        parser->current_state = CHIPS_PARSER_PROCESSING_ESCAPE_BYTE;
        return CHIPS_PARSE_INCOMPLETE;
    }
    else if (byte_received == CHIPS_FRAME_SYNC_BYTE)
    {
        if (parser->write_position >= CHIPS_MINIMUM_FRAME_CONTENT_SIZE_IN_BYTES)
        {
            return validate_complete_frame(parser, output_frame);
        }
        parser->write_position = 0u;
        return CHIPS_PARSE_INCOMPLETE;
    }

    if (parser->write_position >= CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES)
    {
        reset_chips_parser(parser);
        return CHIPS_PARSE_FRAME_TOO_LONG;
    }
    parser->unstuffed_bytes[parser->write_position++] = byte_received;
    return CHIPS_PARSE_INCOMPLETE;
}

static uint16_t append_stuffed_byte(
    uint8_t byte_to_write,
    uint8_t *wire_bytes,
    uint16_t position,
    uint16_t buffer_size)
{
    if ((byte_to_write == CHIPS_FRAME_SYNC_BYTE)
        || (byte_to_write == CHIPS_FRAME_ESCAPE_BYTE))
    {
        if ((uint16_t)(position + 2u) > buffer_size)
        {
            return 0u;
        }
        wire_bytes[position++] = CHIPS_FRAME_ESCAPE_BYTE;
        wire_bytes[position++] = byte_to_write ^ CHIPS_ESCAPE_XOR_VALUE;
        return position;
    }

    if ((uint16_t)(position + 1u) > buffer_size)
    {
        return 0u;
    }
    wire_bytes[position++] = byte_to_write;
    return position;
}

static chips_parse_result_type validate_complete_frame(
    chips_parser_type *parser,
    chips_frame_type *output_frame)
{
    uint16_t content_length = parser->write_position;
    uint16_t data_length = content_length - 2u;
    uint16_t received_crc =
        (uint16_t)parser->unstuffed_bytes[content_length - 2u]
        | ((uint16_t)parser->unstuffed_bytes[content_length - 1u] << 8u);
    uint16_t computed_crc =
        compute_chips_crc16_kermit(parser->unstuffed_bytes, data_length);

    parser->current_state = CHIPS_PARSER_COLLECTING_FRAME_DATA;
    parser->write_position = 0u;

    if (computed_crc != received_crc)
    {
        return CHIPS_PARSE_CRC_MISMATCH;
    }
    extract_frame_fields(parser->unstuffed_bytes, content_length, output_frame);
    return CHIPS_PARSE_FRAME_READY;
}

static void extract_frame_fields(
    const uint8_t *content,
    uint16_t content_length,
    chips_frame_type *output_frame)
{
    output_frame->sequence_number = content[0];
    output_frame->response_flag =
        (content[1] & CHIPS_RESPONSE_FLAG_BIT_MASK) ? 1u : 0u;
    output_frame->command_id = content[1] & CHIPS_COMMAND_ID_BIT_MASK;
    output_frame->payload_length_in_bytes = content_length - 4u;

    for (uint16_t i = 0u; i < output_frame->payload_length_in_bytes; i += 1u)
    {
        output_frame->payload_bytes[i] = content[2u + i];
    }
}
