/* =============================================================================
 * chips_protocol_encode_decode_frames_with_crc16_kermit.h
 * CHIPS (CHESS Internal Protocol over Serial) frame encoding and decoding.
 *
 * This module is PURE LOGIC — it touches no hardware registers. It can be
 * compiled and tested on a laptop with no microcontroller involved.
 *
 * It provides three capabilities:
 *   1. CRC-16/KERMIT checksum computation (table-based, 512 bytes flash)
 *   2. A streaming frame parser: feed one byte at a time from UART,
 *      get notified when a complete valid frame is assembled
 *   3. A frame builder: takes command fields and payload, produces a
 *      complete wire-ready frame with CRC and byte stuffing
 *
 * CHIPS frame format (on the wire):
 *   [0x7E] [SEQ] [RSP|CMD] [PAYLOAD...] [CRC-lo] [CRC-hi] [0x7E]
 *    sync   seq   bit7=rsp  0-256 bytes   CRC-16/KERMIT     sync
 *    byte   num   bits6-0                  LSB first         byte
 *                 =cmd_id
 *
 * Byte stuffing (PPP/HDLC style, RFC 1662):
 *   0x7E in data → 0x7D 0x5E
 *   0x7D in data → 0x7D 0x5D
 *   Applied to everything between the two sync bytes.
 * =============================================================================
 */

#ifndef CHIPS_PROTOCOL_ENCODE_DECODE_FRAMES_WITH_CRC16_KERMIT_H
#define CHIPS_PROTOCOL_ENCODE_DECODE_FRAMES_WITH_CRC16_KERMIT_H

#include <stdint.h>

/* ── Protocol constants ──────────────────────────────────────────────────── */

#define CHIPS_FRAME_SYNC_BYTE                      0x7Eu
#define CHIPS_FRAME_ESCAPE_BYTE                    0x7Du
#define CHIPS_ESCAPE_XOR_VALUE                     0x20u

/* Maximum payload we support in this build. The CHIPS spec allows up to
 * 1024 bytes, but all Phase 4 commands use at most 219 bytes (telemetry).
 * Using 256 saves ~768 bytes of RAM vs the full 1024. Increase this
 * constant if future commands need larger payloads. */
#define CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES        256u

/* Maximum unstuffed frame content: SEQ(1) + CMD(1) + PAYLOAD(256) + CRC(2) */
#define CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES  260u

/* Maximum stuffed frame on the wire: worst case every content byte needs
 * escaping (2x) plus 2 sync bytes. */
#define CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES  522u

/* Minimum valid unstuffed frame content: SEQ(1) + CMD(1) + CRC(2) = 4 */
#define CHIPS_MINIMUM_FRAME_CONTENT_SIZE_IN_BYTES  4u

/* Bit masks for byte 2 of the frame (the command/response byte) */
#define CHIPS_RESPONSE_FLAG_BIT_POSITION           7u
#define CHIPS_RESPONSE_FLAG_BIT_MASK               0x80u
#define CHIPS_COMMAND_ID_BIT_MASK                   0x7Fu

/* ── Command IDs (defined by our EPS firmware, NOT in the CHIPS spec) ──── */

typedef enum {
    CHIPS_COMMAND_ID_GET_TELEMETRY  = 0x01,
    CHIPS_COMMAND_ID_SET_PARAMETER  = 0x02,
    CHIPS_COMMAND_ID_GET_STATE      = 0x03,
    CHIPS_COMMAND_ID_SET_MODE       = 0x04,
    CHIPS_COMMAND_ID_SHED_LOAD      = 0x05,
    CHIPS_COMMAND_ID_DEPLOY_ANTENNA = 0x06
} chips_command_id_type;

/* ── Status codes for response payloads ──────────────────────────────────── */

typedef enum {
    CHIPS_RESPONSE_STATUS_SUCCESS                = 0x00,
    CHIPS_RESPONSE_STATUS_UNKNOWN_COMMAND         = 0x01,
    CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH  = 0x02,
    CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE  = 0x03,
    CHIPS_RESPONSE_STATUS_COMMAND_NOT_AVAILABLE    = 0x04
} chips_response_status_type;

/* ── Parsed frame structure ──────────────────────────────────────────────── */

/* After the parser successfully decodes a frame, the fields are stored here.
 * Also used as input to the frame builder when constructing a response. */
typedef struct {
    uint8_t  sequence_number;
    uint8_t  command_id;        /* bits 6-0 of byte 2 (0-127)           */
    uint8_t  response_flag;     /* bit 7 of byte 2 (0=request, 1=response) */
    uint8_t  payload_bytes[CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES];
    uint16_t payload_length_in_bytes;
} chips_parsed_frame_type;

/* ── Parser state machine ────────────────────────────────────────────────── */

typedef enum {
    CHIPS_PARSER_STATE_WAITING_FOR_SYNC_BYTE   = 0,
    CHIPS_PARSER_STATE_COLLECTING_FRAME_DATA   = 1,
    CHIPS_PARSER_STATE_PROCESSING_ESCAPE_BYTE  = 2
} chips_parser_state_type;

typedef enum {
    CHIPS_PARSER_RESULT_INCOMPLETE              = 0,
    CHIPS_PARSER_RESULT_FRAME_READY             = 1,
    CHIPS_PARSER_RESULT_ERROR_CRC_MISMATCH      = 2,
    CHIPS_PARSER_RESULT_ERROR_FRAME_TOO_LONG    = 3
} chips_parser_result_type;

typedef struct {
    chips_parser_state_type current_state;
    uint8_t  accumulation_buffer_for_unstuffed_bytes[CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES];
    uint16_t accumulation_buffer_current_write_position;
} chips_frame_parser_state_type;

/* ── Public function declarations ────────────────────────────────────────── */

/* Compute CRC-16/KERMIT over a byte array. Uses a 256-entry lookup table.
 * Verified at boot against the test vector: CRC("123456789") = 0x2189. */
uint16_t chips_compute_crc16_kermit_checksum_over_byte_array(
    const uint8_t *data_bytes_to_checksum,
    uint16_t number_of_data_bytes);

/* Run the CRC self-test. Computes CRC of "123456789" and asserts the result
 * is 0x2189. Call at boot before using any other CHIPS function. */
void chips_verify_crc16_kermit_lookup_table_is_correct(void);

/* Reset the parser state machine to its initial idle state.
 * Call once at boot before feeding bytes. */
void chips_parser_initialize_state_machine_to_idle(
    chips_frame_parser_state_type *parser_state_to_initialize);

/* Feed one byte from the UART receive buffer into the parser.
 * Returns INCOMPLETE while the frame is still being assembled.
 * Returns FRAME_READY when a complete frame with valid CRC is available
 * in output_frame_if_complete.
 * Returns ERROR_CRC_MISMATCH or ERROR_FRAME_TOO_LONG on errors. */
chips_parser_result_type chips_parser_process_one_received_byte(
    chips_frame_parser_state_type *parser_state,
    uint8_t one_byte_received_from_uart,
    chips_parsed_frame_type *output_frame_if_complete);

/* Build a complete wire-ready CHIPS frame from the given fields.
 * Computes CRC, applies byte stuffing, adds sync bytes.
 * Returns the total number of bytes written to the output buffer.
 * Returns 0 if the output buffer is too small. */
uint16_t chips_build_stuffed_frame_with_sync_and_crc_into_buffer(
    const chips_parsed_frame_type *frame_fields_to_encode,
    uint8_t *output_buffer_for_wire_bytes,
    uint16_t output_buffer_size_in_bytes);

/* Run the frame round-trip self-test. Builds a test frame, parses it back,
 * and asserts all fields match. Call at boot after CRC verification. */
void chips_verify_frame_build_and_parse_round_trip(void);

#endif /* CHIPS_PROTOCOL_ENCODE_DECODE_FRAMES_WITH_CRC16_KERMIT_H */
