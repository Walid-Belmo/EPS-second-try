#include <stdint.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "communication_with_esp32/chips_reply_sending/functions_to_send_chips_replies_to_esp32.h"
#include "uart_obc.h"

void send_chips_frame_to_esp32(
    const chips_parsed_frame_type *frame_to_send)
{
    SATELLITE_ASSERT(frame_to_send != (void *)0);

    uint8_t wire_bytes[CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES];
    uint16_t wire_length =
        chips_build_stuffed_frame_with_sync_and_crc_into_buffer(
            frame_to_send,
            wire_bytes,
            CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES);

    if (wire_length == 0u)
    {
        return;
    }

    uart_obc_send_bytes(wire_bytes, (uint32_t)wire_length);
}
