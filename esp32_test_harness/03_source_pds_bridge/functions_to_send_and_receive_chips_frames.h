#ifndef FUNCTIONS_TO_SEND_AND_RECEIVE_CHIPS_FRAMES_H
#define FUNCTIONS_TO_SEND_AND_RECEIVE_CHIPS_FRAMES_H

#include <Arduino.h>
#include <stdint.h>

#define CHIPS_FRAME_SYNC_BYTE 0x7Eu
#define CHIPS_FRAME_ESCAPE_BYTE 0x7Du
#define CHIPS_ESCAPE_XOR_VALUE 0x20u
#define CHIPS_RESPONSE_FLAG_BIT_POSITION 7u
#define CHIPS_RESPONSE_FLAG_BIT_MASK 0x80u
#define CHIPS_COMMAND_ID_BIT_MASK 0x7Fu
#define CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES 256u
#define CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES 260u
#define CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES 522u
#define CHIPS_MINIMUM_FRAME_CONTENT_SIZE_IN_BYTES 4u

typedef struct {
    uint8_t sequence_number;
    uint8_t command_id;
    uint8_t response_flag;
    uint8_t payload_bytes[CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES];
    uint16_t payload_length_in_bytes;
} chips_frame_type;

typedef enum {
    CHIPS_PARSER_WAITING_FOR_SYNC_BYTE = 0,
    CHIPS_PARSER_COLLECTING_FRAME_DATA = 1,
    CHIPS_PARSER_PROCESSING_ESCAPE_BYTE = 2
} chips_parser_state_name_type;

typedef enum {
    CHIPS_PARSE_INCOMPLETE = 0,
    CHIPS_PARSE_FRAME_READY = 1,
    CHIPS_PARSE_CRC_MISMATCH = 2,
    CHIPS_PARSE_FRAME_TOO_LONG = 3
} chips_parse_result_type;

typedef struct {
    chips_parser_state_name_type current_state;
    uint8_t unstuffed_bytes[CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES];
    uint16_t write_position;
} chips_parser_type;

void reset_chips_parser(chips_parser_type *parser);
uint16_t compute_chips_crc16_kermit(const uint8_t *bytes, uint16_t length);
uint16_t build_chips_frame_for_uart(
    const chips_frame_type *frame,
    uint8_t *wire_bytes,
    uint16_t wire_buffer_size);
void send_chips_frame_to_board(Stream &board_uart, const chips_frame_type *frame);
chips_parse_result_type parse_one_chips_byte(
    chips_parser_type *parser,
    uint8_t byte_received,
    chips_frame_type *output_frame);

#endif
