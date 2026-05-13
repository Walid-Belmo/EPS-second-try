#ifndef PDS_CHIPS_REPLY_SENDER_H
#define PDS_CHIPS_REPLY_SENDER_H

#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"

void send_chips_frame_to_esp32(
    const chips_parsed_frame_type *frame_to_send);

#endif /* PDS_CHIPS_REPLY_SENDER_H */
