/* =============================================================================
 * chips_protocol_encode_decode_frames_with_crc16_kermit.c
 * CHIPS (CHESS Internal Protocol over Serial) frame encoding and decoding.
 * Provides CRC-16/KERMIT, byte stuffing, a streaming frame parser, and
 * a frame builder. All functions are pure logic — no hardware access.
 *
 * Category: PURE LOGIC (no hardware registers, testable on a laptop)
 *
 * CRC-16/KERMIT parameters (verified against RevEng CRC catalogue):
 *   Polynomial: 0x1021 (reflected: 0x8408)
 *   Initial value: 0x0000
 *   Input reflected: YES
 *   Output reflected: YES
 *   Final XOR: 0x0000
 *   Check value: CRC("123456789") = 0x2189
 * =============================================================================
 */

#include <stdint.h>
#include <stdbool.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "debug_functions.h"

/* ── CRC-16/KERMIT lookup table (512 bytes in flash) ─────────────────────── */

/* Each entry answers: "if I feed just this one byte value (0x00-0xFF) through
 * the reflected CRC-16 polynomial 0x8408, what is the contribution?"
 *
 * Generated with the reflected polynomial 0x8408 (which is 0x1021 bit-reversed).
 * For each byte value b: start with b, for each of 8 bits, if LSB is 1 then
 * shift right and XOR with 0x8408, else just shift right. The result is the
 * table entry.
 *
 * Verified: using this table, CRC("123456789") = 0x2189. */
static const uint16_t crc16_kermit_lookup_table[256] = {
    0x0000u, 0x1189u, 0x2312u, 0x329Bu, 0x4624u, 0x57ADu, 0x6536u, 0x74BFu,
    0x8C48u, 0x9DC1u, 0xAF5Au, 0xBED3u, 0xCA6Cu, 0xDBE5u, 0xE97Eu, 0xF8F7u,
    0x1081u, 0x0108u, 0x3393u, 0x221Au, 0x56A5u, 0x472Cu, 0x75B7u, 0x643Eu,
    0x9CC9u, 0x8D40u, 0xBFDBu, 0xAE52u, 0xDAEDu, 0xCB64u, 0xF9FFu, 0xE876u,
    0x2102u, 0x308Bu, 0x0210u, 0x1399u, 0x6726u, 0x76AFu, 0x4434u, 0x55BDu,
    0xAD4Au, 0xBCC3u, 0x8E58u, 0x9FD1u, 0xEB6Eu, 0xFAE7u, 0xC87Cu, 0xD9F5u,
    0x3183u, 0x200Au, 0x1291u, 0x0318u, 0x77A7u, 0x662Eu, 0x54B5u, 0x453Cu,
    0xBDCBu, 0xAC42u, 0x9ED9u, 0x8F50u, 0xFBEFu, 0xEA66u, 0xD8FDu, 0xC974u,
    0x4204u, 0x538Du, 0x6116u, 0x709Fu, 0x0420u, 0x15A9u, 0x2732u, 0x36BBu,
    0xCE4Cu, 0xDFC5u, 0xED5Eu, 0xFCD7u, 0x8868u, 0x99E1u, 0xAB7Au, 0xBAF3u,
    0x5285u, 0x430Cu, 0x7197u, 0x601Eu, 0x14A1u, 0x0528u, 0x37B3u, 0x263Au,
    0xDECDu, 0xCF44u, 0xFDDFu, 0xEC56u, 0x98E9u, 0x8960u, 0xBBFBu, 0xAA72u,
    0x6306u, 0x728Fu, 0x4014u, 0x519Du, 0x2522u, 0x34ABu, 0x0630u, 0x17B9u,
    0xEF4Eu, 0xFEC7u, 0xCC5Cu, 0xDDD5u, 0xA96Au, 0xB8E3u, 0x8A78u, 0x9BF1u,
    0x7387u, 0x620Eu, 0x5095u, 0x411Cu, 0x35A3u, 0x242Au, 0x16B1u, 0x0738u,
    0xFFCFu, 0xEE46u, 0xDCDDu, 0xCD54u, 0xB9EBu, 0xA862u, 0x9AF9u, 0x8B70u,
    0x8408u, 0x9581u, 0xA71Au, 0xB693u, 0xC22Cu, 0xD3A5u, 0xE13Eu, 0xF0B7u,
    0x0840u, 0x19C9u, 0x2B52u, 0x3ADBu, 0x4E64u, 0x5FEDu, 0x6D76u, 0x7CFFu,
    0x9489u, 0x8500u, 0xB79Bu, 0xA612u, 0xD2ADu, 0xC324u, 0xF1BFu, 0xE036u,
    0x18C1u, 0x0948u, 0x3BD3u, 0x2A5Au, 0x5EE5u, 0x4F6Cu, 0x7DF7u, 0x6C7Eu,
    0xA50Au, 0xB483u, 0x8618u, 0x9791u, 0xE32Eu, 0xF2A7u, 0xC03Cu, 0xD1B5u,
    0x2942u, 0x38CBu, 0x0A50u, 0x1BD9u, 0x6F66u, 0x7EEFu, 0x4C74u, 0x5DFDu,
    0xB58Bu, 0xA402u, 0x9699u, 0x8710u, 0xF3AFu, 0xE226u, 0xD0BDu, 0xC134u,
    0x39C3u, 0x284Au, 0x1AD1u, 0x0B58u, 0x7FE7u, 0x6E6Eu, 0x5CF5u, 0x4D7Cu,
    0xC60Cu, 0xD785u, 0xE51Eu, 0xF497u, 0x8028u, 0x91A1u, 0xA33Au, 0xB2B3u,
    0x4A44u, 0x5BCDu, 0x6956u, 0x78DFu, 0x0C60u, 0x1DE9u, 0x2F72u, 0x3EFBu,
    0xD68Du, 0xC704u, 0xF59Fu, 0xE416u, 0x90A9u, 0x8120u, 0xB3BBu, 0xA232u,
    0x5AC5u, 0x4B4Cu, 0x79D7u, 0x685Eu, 0x1CE1u, 0x0D68u, 0x3FF3u, 0x2E7Au,
    0xE70Eu, 0xF687u, 0xC41Cu, 0xD595u, 0xA12Au, 0xB0A3u, 0x8238u, 0x93B1u,
    0x6B46u, 0x7ACFu, 0x4854u, 0x59DDu, 0x2D62u, 0x3CEBu, 0x0E70u, 0x1FF9u,
    0xF78Fu, 0xE606u, 0xD49Du, 0xC514u, 0xB1ABu, 0xA022u, 0x92B9u, 0x8330u,
    0x7BC7u, 0x6A4Eu, 0x58D5u, 0x495Cu, 0x3DE3u, 0x2C6Au, 0x1EF1u, 0x0F78u
};

/* ── Private function prototypes ─────────────────────────────────────────── */

static uint16_t write_one_byte_with_stuffing_into_buffer(
    uint8_t byte_to_stuff,
    uint8_t *output_buffer,
    uint16_t current_position,
    uint16_t buffer_size);

static void extract_fields_from_validated_accumulation_buffer(
    const uint8_t *accumulation_buffer,
    uint16_t buffer_length,
    chips_parsed_frame_type *output_frame);

static chips_parser_result_type validate_and_extract_complete_frame(
    chips_frame_parser_state_type *parser_state,
    chips_parsed_frame_type *output_frame);

/* ── CRC-16/KERMIT computation ───────────────────────────────────────────── */

uint16_t chips_compute_crc16_kermit_checksum_over_byte_array(
    const uint8_t *data_bytes_to_checksum,
    uint16_t number_of_data_bytes)
{
    /* A null pointer with non-zero length is a caller bug. Zero-length
     * with null is allowed (CRC of empty data = 0x0000). */
    if (number_of_data_bytes > 0u)
    {
        SATELLITE_ASSERT(data_bytes_to_checksum != (void *)0);
    }

    /* CRC-16/KERMIT starts with initial value 0x0000. For each byte,
     * XOR it with the low byte of the current CRC, use that as an
     * index into the lookup table, and XOR the result with the CRC
     * shifted right by 8. */
    uint16_t crc_accumulator = 0x0000u;

    for (uint16_t byte_index = 0u; byte_index < number_of_data_bytes;
         byte_index += 1u)
    {
        uint8_t table_index =
            (uint8_t)((crc_accumulator ^ data_bytes_to_checksum[byte_index])
                      & 0xFFu);
        crc_accumulator =
            (crc_accumulator >> 8u) ^ crc16_kermit_lookup_table[table_index];
    }

    /* No final XOR (XOR value = 0x0000 for CRC-16/KERMIT). */
    return crc_accumulator;
}

void chips_verify_crc16_kermit_lookup_table_is_correct(void)
{
    /* The universally published test vector for CRC-16/KERMIT. */
    static const uint8_t test_vector_bytes[] = {
        0x31u, 0x32u, 0x33u, 0x34u, 0x35u,
        0x36u, 0x37u, 0x38u, 0x39u
    };
    /* "123456789" in ASCII */

    uint16_t computed_crc = chips_compute_crc16_kermit_checksum_over_byte_array(
        test_vector_bytes, 9u);

    SATELLITE_ASSERT(computed_crc == 0x2189u);

    DEBUG_LOG_UINT("CRC self-test result", (uint32_t)computed_crc);
    DEBUG_LOG_TEXT("CRC self-test: 0x2189 PASS");
}

/* ── Parser state machine ────────────────────────────────────────────────── */

void chips_parser_initialize_state_machine_to_idle(
    chips_frame_parser_state_type *parser_state_to_initialize)
{
    SATELLITE_ASSERT(parser_state_to_initialize != (void *)0);

    parser_state_to_initialize->current_state =
        CHIPS_PARSER_STATE_WAITING_FOR_SYNC_BYTE;
    parser_state_to_initialize->accumulation_buffer_current_write_position = 0u;
}

chips_parser_result_type chips_parser_process_one_received_byte(
    chips_frame_parser_state_type *parser_state,
    uint8_t one_byte_received_from_uart,
    chips_parsed_frame_type *output_frame_if_complete)
{
    SATELLITE_ASSERT(parser_state != (void *)0);
    SATELLITE_ASSERT(output_frame_if_complete != (void *)0);

    uint8_t byte_value = one_byte_received_from_uart;

    /* ── State: WAITING_FOR_SYNC_BYTE ──────────────────────────────── */
    if (parser_state->current_state ==
        CHIPS_PARSER_STATE_WAITING_FOR_SYNC_BYTE)
    {
        if (byte_value == CHIPS_FRAME_SYNC_BYTE)
        {
            /* Received the opening 0x7E — start collecting frame data. */
            parser_state->current_state =
                CHIPS_PARSER_STATE_COLLECTING_FRAME_DATA;
            parser_state->accumulation_buffer_current_write_position = 0u;
        }
        /* Any non-sync byte while waiting is noise — ignore it. */
        return CHIPS_PARSER_RESULT_INCOMPLETE;
    }

    /* ── State: PROCESSING_ESCAPE_BYTE ─────────────────────────────── */
    if (parser_state->current_state ==
        CHIPS_PARSER_STATE_PROCESSING_ESCAPE_BYTE)
    {
        /* The previous byte was 0x7D (escape). XOR this byte with 0x20
         * to recover the original data byte. For example:
         *   0x5E ^ 0x20 = 0x7E (the original was a sync byte in data)
         *   0x5D ^ 0x20 = 0x7D (the original was an escape byte in data) */
        uint8_t unescaped_byte = byte_value ^ CHIPS_ESCAPE_XOR_VALUE;

        uint16_t position =
            parser_state->accumulation_buffer_current_write_position;

        if (position >= CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES)
        {
            /* Frame content exceeds our buffer. Drop it and reset. */
            parser_state->current_state =
                CHIPS_PARSER_STATE_WAITING_FOR_SYNC_BYTE;
            parser_state->accumulation_buffer_current_write_position = 0u;
            return CHIPS_PARSER_RESULT_ERROR_FRAME_TOO_LONG;
        }

        parser_state->accumulation_buffer_for_unstuffed_bytes[position] =
            unescaped_byte;
        parser_state->accumulation_buffer_current_write_position =
            position + 1u;

        parser_state->current_state =
            CHIPS_PARSER_STATE_COLLECTING_FRAME_DATA;
        return CHIPS_PARSER_RESULT_INCOMPLETE;
    }

    /* ── State: COLLECTING_FRAME_DATA ──────────────────────────────── */

    /* Handle sync byte (0x7E) during collection */
    if (byte_value == CHIPS_FRAME_SYNC_BYTE)
    {
        uint16_t position =
            parser_state->accumulation_buffer_current_write_position;

        if (position == 0u)
        {
            /* Back-to-back sync bytes — inter-frame fill per RFC 1662.
             * This is normal. Stay in COLLECTING, ready for real data. */
            return CHIPS_PARSER_RESULT_INCOMPLETE;
        }

        if (position < CHIPS_MINIMUM_FRAME_CONTENT_SIZE_IN_BYTES)
        {
            /* Frame too short (need at least SEQ + CMD + CRC_lo + CRC_hi
             * = 4 bytes). Discard and treat this 0x7E as start of next
             * frame. */
            parser_state->accumulation_buffer_current_write_position = 0u;
            return CHIPS_PARSER_RESULT_INCOMPLETE;
        }

        /* Frame end delimiter with enough data. Validate CRC. */
        return validate_and_extract_complete_frame(
            parser_state, output_frame_if_complete);
    }

    /* Handle escape byte (0x7D) — next byte is XOR'd with 0x20 */
    if (byte_value == CHIPS_FRAME_ESCAPE_BYTE)
    {
        parser_state->current_state =
            CHIPS_PARSER_STATE_PROCESSING_ESCAPE_BYTE;
        return CHIPS_PARSER_RESULT_INCOMPLETE;
    }

    /* Normal data byte — store it in the accumulation buffer */
    {
        uint16_t position =
            parser_state->accumulation_buffer_current_write_position;

        if (position >= CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES)
        {
            parser_state->current_state =
                CHIPS_PARSER_STATE_WAITING_FOR_SYNC_BYTE;
            parser_state->accumulation_buffer_current_write_position = 0u;
            return CHIPS_PARSER_RESULT_ERROR_FRAME_TOO_LONG;
        }

        parser_state->accumulation_buffer_for_unstuffed_bytes[position] =
            byte_value;
        parser_state->accumulation_buffer_current_write_position =
            position + 1u;
    }

    return CHIPS_PARSER_RESULT_INCOMPLETE;
}

/* ── Frame builder ───────────────────────────────────────────────────────── */

uint16_t chips_build_stuffed_frame_with_sync_and_crc_into_buffer(
    const chips_parsed_frame_type *frame_fields_to_encode,
    uint8_t *output_buffer_for_wire_bytes,
    uint16_t output_buffer_size_in_bytes)
{
    SATELLITE_ASSERT(frame_fields_to_encode != (void *)0);
    SATELLITE_ASSERT(output_buffer_for_wire_bytes != (void *)0);
    SATELLITE_ASSERT(frame_fields_to_encode->payload_length_in_bytes
                     <= CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES);

    /* Check minimum output buffer size (at least 2 syncs + 4 content min) */
    if (output_buffer_size_in_bytes < 8u)
    {
        return 0u;
    }

    /* Step 1: Build raw (pre-stuffing) content into a temporary buffer.
     * Raw content = SEQ + CMD/RSP + PAYLOAD.
     * We compute CRC over this raw content. */
    uint8_t raw_content_buffer[CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES];
    uint16_t raw_content_length = 0u;

    /* Byte 0: sequence number */
    raw_content_buffer[raw_content_length] =
        frame_fields_to_encode->sequence_number;
    raw_content_length += 1u;

    /* Byte 1: command/response byte (bit 7 = response flag, bits 6-0 = cmd) */
    raw_content_buffer[raw_content_length] =
        (uint8_t)((frame_fields_to_encode->response_flag
                   << CHIPS_RESPONSE_FLAG_BIT_POSITION)
                  | (frame_fields_to_encode->command_id
                     & CHIPS_COMMAND_ID_BIT_MASK));
    raw_content_length += 1u;

    /* Bytes 2..N: payload */
    for (uint16_t i = 0u;
         i < frame_fields_to_encode->payload_length_in_bytes;
         i += 1u)
    {
        raw_content_buffer[raw_content_length] =
            frame_fields_to_encode->payload_bytes[i];
        raw_content_length += 1u;
    }

    /* Step 2: Compute CRC-16/KERMIT over the raw content
     * (SEQ + CMD/RSP + PAYLOAD, before byte stuffing). */
    uint16_t crc_value = chips_compute_crc16_kermit_checksum_over_byte_array(
        raw_content_buffer, raw_content_length);

    /* Append CRC to raw content, LSB first (CRC-16/KERMIT convention). */
    raw_content_buffer[raw_content_length] = (uint8_t)(crc_value & 0xFFu);
    raw_content_length += 1u;
    raw_content_buffer[raw_content_length] = (uint8_t)((crc_value >> 8u) & 0xFFu);
    raw_content_length += 1u;

    /* Step 3: Write the stuffed frame to the output buffer.
     * Start with opening sync byte, then stuff each content byte,
     * then closing sync byte. */
    uint16_t output_position = 0u;

    /* Opening sync byte (never stuffed). */
    output_buffer_for_wire_bytes[output_position] = CHIPS_FRAME_SYNC_BYTE;
    output_position += 1u;

    /* Stuff each content byte (SEQ, CMD, PAYLOAD, CRC). */
    for (uint16_t i = 0u; i < raw_content_length; i += 1u)
    {
        output_position = write_one_byte_with_stuffing_into_buffer(
            raw_content_buffer[i],
            output_buffer_for_wire_bytes,
            output_position,
            output_buffer_size_in_bytes);

        if (output_position == 0u)
        {
            return 0u; /* Buffer overflow — frame doesn't fit. */
        }
    }

    /* Closing sync byte (never stuffed). */
    if (output_position >= output_buffer_size_in_bytes)
    {
        return 0u;
    }
    output_buffer_for_wire_bytes[output_position] = CHIPS_FRAME_SYNC_BYTE;
    output_position += 1u;

    return output_position;
}

/* ── Self-test: frame round-trip ─────────────────────────────────────────── */

void chips_verify_frame_build_and_parse_round_trip(void)
{
    /* Build a test frame with known values. */
    chips_parsed_frame_type test_frame_to_build;
    test_frame_to_build.sequence_number = 0x42u;
    test_frame_to_build.command_id = 0x01u;
    test_frame_to_build.response_flag = 0u;
    test_frame_to_build.payload_bytes[0] = 0xAAu;
    test_frame_to_build.payload_bytes[1] = 0xBBu;
    test_frame_to_build.payload_length_in_bytes = 2u;

    /* Build the stuffed frame into a wire buffer. */
    uint8_t wire_buffer[64];
    uint16_t wire_length =
        chips_build_stuffed_frame_with_sync_and_crc_into_buffer(
            &test_frame_to_build, wire_buffer, 64u);

    SATELLITE_ASSERT(wire_length > 0u);

    /* Feed the wire bytes one at a time into the parser. */
    chips_frame_parser_state_type test_parser;
    chips_parsed_frame_type parsed_result;
    chips_parser_initialize_state_machine_to_idle(&test_parser);

    chips_parser_result_type final_result = CHIPS_PARSER_RESULT_INCOMPLETE;
    for (uint16_t i = 0u; i < wire_length; i += 1u)
    {
        final_result = chips_parser_process_one_received_byte(
            &test_parser, wire_buffer[i], &parsed_result);
    }

    /* The parser should have produced a complete frame. */
    SATELLITE_ASSERT(final_result == CHIPS_PARSER_RESULT_FRAME_READY);

    /* All fields must match the original. */
    SATELLITE_ASSERT(parsed_result.sequence_number == 0x42u);
    SATELLITE_ASSERT(parsed_result.command_id == 0x01u);
    SATELLITE_ASSERT(parsed_result.response_flag == 0u);
    SATELLITE_ASSERT(parsed_result.payload_length_in_bytes == 2u);
    SATELLITE_ASSERT(parsed_result.payload_bytes[0] == 0xAAu);
    SATELLITE_ASSERT(parsed_result.payload_bytes[1] == 0xBBu);

    DEBUG_LOG_TEXT("Frame round-trip self-test PASS");
}

/* ── Private functions ───────────────────────────────────────────────────── */

static uint16_t write_one_byte_with_stuffing_into_buffer(
    uint8_t byte_to_stuff,
    uint8_t *output_buffer,
    uint16_t current_position,
    uint16_t buffer_size)
{
    if (byte_to_stuff == CHIPS_FRAME_SYNC_BYTE)
    {
        /* 0x7E in data → 0x7D followed by 0x5E (0x7E ^ 0x20). */
        if ((current_position + 2u) > buffer_size)
        {
            return 0u;
        }
        output_buffer[current_position] = CHIPS_FRAME_ESCAPE_BYTE;
        output_buffer[current_position + 1u] =
            CHIPS_FRAME_SYNC_BYTE ^ CHIPS_ESCAPE_XOR_VALUE;
        return current_position + 2u;
    }

    if (byte_to_stuff == CHIPS_FRAME_ESCAPE_BYTE)
    {
        /* 0x7D in data → 0x7D followed by 0x5D (0x7D ^ 0x20). */
        if ((current_position + 2u) > buffer_size)
        {
            return 0u;
        }
        output_buffer[current_position] = CHIPS_FRAME_ESCAPE_BYTE;
        output_buffer[current_position + 1u] =
            CHIPS_FRAME_ESCAPE_BYTE ^ CHIPS_ESCAPE_XOR_VALUE;
        return current_position + 2u;
    }

    /* Normal byte — no escaping needed. */
    if ((current_position + 1u) > buffer_size)
    {
        return 0u;
    }
    output_buffer[current_position] = byte_to_stuff;
    return current_position + 1u;
}

static void extract_fields_from_validated_accumulation_buffer(
    const uint8_t *accumulation_buffer,
    uint16_t buffer_length,
    chips_parsed_frame_type *output_frame)
{
    SATELLITE_ASSERT(accumulation_buffer != (void *)0);
    SATELLITE_ASSERT(output_frame != (void *)0);
    SATELLITE_ASSERT(buffer_length >= CHIPS_MINIMUM_FRAME_CONTENT_SIZE_IN_BYTES);

    /* Byte 0: sequence number */
    output_frame->sequence_number = accumulation_buffer[0];

    /* Byte 1: bit 7 = response flag, bits 6-0 = command ID */
    output_frame->response_flag =
        (accumulation_buffer[1] >> CHIPS_RESPONSE_FLAG_BIT_POSITION) & 0x01u;
    output_frame->command_id =
        accumulation_buffer[1] & CHIPS_COMMAND_ID_BIT_MASK;

    /* Payload is everything between byte 2 and the last 2 bytes (CRC).
     * payload_length = total_length - 2 (header bytes) - 2 (CRC bytes). */
    uint16_t payload_length = buffer_length - 4u;

    /* Clamp to maximum payload size (should not happen if parser enforces
     * buffer limits, but belt-and-suspenders). */
    if (payload_length > CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES)
    {
        payload_length = CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES;
    }

    output_frame->payload_length_in_bytes = payload_length;

    for (uint16_t i = 0u; i < payload_length; i += 1u)
    {
        output_frame->payload_bytes[i] = accumulation_buffer[2u + i];
    }
}

static chips_parser_result_type validate_and_extract_complete_frame(
    chips_frame_parser_state_type *parser_state,
    chips_parsed_frame_type *output_frame)
{
    uint16_t content_length =
        parser_state->accumulation_buffer_current_write_position;

    /* The last 2 bytes in the accumulation buffer are the received CRC.
     * Everything before them is the data the CRC was computed over. */
    uint16_t data_length_without_crc = content_length - 2u;

    /* Extract the received CRC (LSB first). */
    uint16_t received_crc =
        (uint16_t)parser_state->accumulation_buffer_for_unstuffed_bytes[
            content_length - 2u]
        | ((uint16_t)parser_state->accumulation_buffer_for_unstuffed_bytes[
            content_length - 1u] << 8u);

    /* Compute CRC over the data portion (SEQ + CMD + PAYLOAD). */
    uint16_t computed_crc = chips_compute_crc16_kermit_checksum_over_byte_array(
        parser_state->accumulation_buffer_for_unstuffed_bytes,
        data_length_without_crc);

    /* Reset parser for next frame. The closing 0x7E doubles as the
     * opening sync for the next frame, so go to COLLECTING state. */
    parser_state->current_state =
        CHIPS_PARSER_STATE_COLLECTING_FRAME_DATA;
    parser_state->accumulation_buffer_current_write_position = 0u;

    if (computed_crc != received_crc)
    {
        return CHIPS_PARSER_RESULT_ERROR_CRC_MISMATCH;
    }

    /* CRC valid — extract the frame fields. */
    extract_fields_from_validated_accumulation_buffer(
        parser_state->accumulation_buffer_for_unstuffed_bytes,
        content_length,
        output_frame);

    return CHIPS_PARSER_RESULT_FRAME_READY;
}
