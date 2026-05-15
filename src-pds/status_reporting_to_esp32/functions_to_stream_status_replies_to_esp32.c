/*
 * functions_to_stream_status_replies_to_esp32.c
 *
 * The "outgoing telemetry" half of the firmware's communication path.
 * Called once per main-loop iteration. Decides whether enough time
 * has elapsed since the last status push and, if so, builds and sends
 * one status reply.
 *
 * Telemetry streaming is on-demand: the firmware does NOT push
 * unsolicited status replies by default. The operator's web page
 * starts the stream by sending the `stream_values on period=NNN`
 * CHIPS command, which sets:
 *   - runtime_state.telemetry_stream_is_enabled = 1
 *   - runtime_state.telemetry_stream_period_ms = NNN
 *   - runtime_state.telemetry_field_mask        = which fields to include
 *
 * From then on, this function fires one reply every NNN milliseconds
 * until the page sends `stream_values off`.
 *
 * The reply uses the same payload shape as a `get_values` response
 * (built by build_values_reply_to_esp32 in the sibling file). The
 * sequence number is independent — it counts STREAM messages, not
 * answers to commands — so the page can detect dropped frames if it
 * wants to.
 */

#include <stdint.h>

#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"
#include "communication_with_esp32/chips_reply_sending/functions_to_send_chips_replies_to_esp32.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "status_reporting_to_esp32/functions_to_build_status_replies_sent_to_esp32.h"
#include "status_reporting_to_esp32/functions_to_stream_status_replies_to_esp32.h"

/*
 * Called every main-loop iteration. Returns immediately on iterations
 * where streaming is off or the period hasn't elapsed yet, so it's
 * cheap to call unconditionally.
 *
 * On iterations where it does fire, it builds the same status payload
 * a `get_values` command would produce — so the page receives the
 * same byte layout whether it polled or subscribed.
 */
void send_status_if_needed(void)
{
    /* Pointer to the firmware-wide runtime state. The streaming
     * config lives in three fields here:
     *   telemetry_stream_is_enabled  - 0 if the page hasn't asked
     *   telemetry_stream_period_ms   - cadence the page requested
     *   telemetry_field_mask         - which field bits to include
     *   last_stream_timestamp_ms     - when we last fired
     */
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Streaming off: do nothing. The page can still poll
     * /api/status which calls get_values, so this isn't the only way
     * to get telemetry — just the push-style way. */
    if (runtime_state->telemetry_stream_is_enabled == 0u)
    {
        return;
    }

    /* Period gate. Read the free-running millisecond tick (driven by
     * the ARM SysTick at 48 MHz) and compare against the timestamp
     * of the last stream firing. The unsigned subtraction wraps
     * correctly across the 32-bit ms counter rollover (~49 days). */
    uint32_t now_ms = millisecond_tick_timer_get_milliseconds_since_boot();
    uint32_t elapsed_ms = now_ms - runtime_state->last_stream_timestamp_ms;

    /* Not yet due — wait for the next iteration. */
    if (elapsed_ms < runtime_state->telemetry_stream_period_ms)
    {
        return;
    }

    /* Due. Build a CHIPS frame with response_flag=1 (it's a "reply"
     * even though no command provoked it — that's how the ESP32
     * bridge knows to forward it as telemetry rather than confuse
     * it with an unsolicited request). The command_id is set to
     * GET_VALUES so the same printer code on the ESP32 side that
     * handles `get_values` replies can decode this one. */
    chips_parsed_frame_type stream_frame;
    stream_frame.sequence_number = runtime_state->stream_sequence_number;
    stream_frame.command_id = PDS_BOARD_COMMAND_GET_VALUES;
    stream_frame.response_flag = 1u;

    /* Bump the stream-only sequence counter so the next stream frame
     * gets a different number. The page can use this to detect
     * dropped frames (gap in sequence). Wraps at 256 (uint8_t),
     * which is fine for drop detection since we only care about
     * adjacent values. */
    runtime_state->stream_sequence_number += 1u;

    /* Pour the entire status payload into the frame. The field mask
     * controls which sections are included — the page typically
     * asks for `fields=all` so every block is present, but it can
     * also subscribe to a smaller subset for bandwidth reasons. */
    build_values_reply_to_esp32(
        &stream_frame,
        runtime_state,
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS,
        runtime_state->telemetry_field_mask,
        now_ms);

    /* Push the bytes onto the UART transmit buffer. This is
     * non-blocking — the SERCOM ISR drains the buffer in the
     * background while the main loop continues. */
    send_chips_frame_to_esp32(&stream_frame);

    /* Stamp "now" as the new period reference so the NEXT due-check
     * measures from this firing, not the previous one. */
    runtime_state->last_stream_timestamp_ms = now_ms;
}
