#ifndef PDS_COMMANDS_FROM_ESP32_H
#define PDS_COMMANDS_FROM_ESP32_H

#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"

void execute_command_from_esp32_and_build_reply(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *reply_to_send);

#endif /* PDS_COMMANDS_FROM_ESP32_H */
