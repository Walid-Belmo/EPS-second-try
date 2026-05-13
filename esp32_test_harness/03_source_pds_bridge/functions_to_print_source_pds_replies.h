#ifndef FUNCTIONS_TO_PRINT_SOURCE_PDS_REPLIES_H
#define FUNCTIONS_TO_PRINT_SOURCE_PDS_REPLIES_H

#include <Arduino.h>
#include "functions_to_send_and_receive_chips_frames.h"

void print_reply_received_from_source_pds_board(
    const chips_frame_type *frame,
    Stream &output);
const char *source_pds_status_name(uint8_t status);

#endif
