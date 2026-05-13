#include <stdint.h>

#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"
#include "communication_with_esp32/chips_reply_sending/functions_to_send_chips_replies_to_esp32.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"
#include "command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.h"
#include "status_reporting_to_esp32/functions_to_build_status_replies_sent_to_esp32.h"
#include "status_reporting_to_esp32/functions_to_stream_status_replies_to_esp32.h"

void send_status_if_needed(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_ram_values();

    if (runtime_state->telemetry_stream_is_enabled == 0u)
    {
        return;
    }

    uint32_t now_ms = millisecond_tick_timer_get_milliseconds_since_boot();
    uint32_t elapsed_ms = now_ms - runtime_state->last_stream_timestamp_ms;

    if (elapsed_ms < runtime_state->telemetry_stream_period_ms)
    {
        return;
    }

    chips_parsed_frame_type stream_frame;
    stream_frame.sequence_number = runtime_state->stream_sequence_number;
    stream_frame.command_id = PDS_BOARD_COMMAND_GET_VALUES;
    stream_frame.response_flag = 1u;
    runtime_state->stream_sequence_number += 1u;
    build_values_reply_to_esp32(
        &stream_frame,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        runtime_state->telemetry_field_mask,
        now_ms);
    send_chips_frame_to_esp32(&stream_frame);
    runtime_state->last_stream_timestamp_ms = now_ms;
}
