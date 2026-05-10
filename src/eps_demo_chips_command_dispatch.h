/* =============================================================================
 * eps_demo_chips_command_dispatch.h
 * CHIPS command handlers for the semester demo visibility path.
 *
 * This layer stores injected sensor values, returns telemetry/debug snapshots,
 * and can stream periodic telemetry over the same UART used for commands.
 * =============================================================================
 */

#ifndef EPS_DEMO_CHIPS_COMMAND_DISPATCH_H
#define EPS_DEMO_CHIPS_COMMAND_DISPATCH_H

#include <stdint.h>
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"

#define EPS_DEMO_INJECTED_SENSOR_FRAME_PAYLOAD_LENGTH_IN_BYTES  17u

void eps_demo_chips_command_dispatch_initialize(void);

void eps_demo_chips_note_valid_frame_received(void);

void eps_demo_chips_note_parser_result(
    chips_parser_result_type parser_result);

void eps_demo_chips_dispatch_received_command_and_send_response(
    const chips_parsed_frame_type *received_command_frame);

void eps_demo_chips_run_control_loop_if_due(void);

void eps_demo_chips_send_periodic_telemetry_if_due(void);

#endif /* EPS_DEMO_CHIPS_COMMAND_DISPATCH_H */
