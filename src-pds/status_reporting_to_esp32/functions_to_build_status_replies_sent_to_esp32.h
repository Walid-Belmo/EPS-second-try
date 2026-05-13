#ifndef PDS_STATUS_REPLY_BUILDER_H
#define PDS_STATUS_REPLY_BUILDER_H

#include <stdint.h>

#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"

void build_ack_reply_to_esp32(
    chips_parsed_frame_type *reply_to_send,
    uint8_t status);
void build_values_reply_to_esp32(
    chips_parsed_frame_type *reply_to_send,
    const pds_runtime_state_type *runtime_state,
    uint8_t status,
    uint32_t requested_field_mask,
    uint32_t timestamp_in_milliseconds);

#endif /* PDS_STATUS_REPLY_BUILDER_H */
