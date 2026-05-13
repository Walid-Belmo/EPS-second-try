#ifndef PDS_COMMAND_PAYLOADS_H
#define PDS_COMMAND_PAYLOADS_H

#include <stdint.h>

#include "board_command_contract/board_command_ids_and_payload_layouts.h"

pds_state_demo_inputs_type read_state_demo_inputs_from_command_payload(
    const uint8_t *payload);
uint8_t state_demo_inputs_are_valid(
    const pds_state_demo_inputs_type *candidate_inputs);

pds_mppt_demo_curve_type read_mppt_demo_curve_from_command_payload(
    const uint8_t *payload);
uint8_t mppt_demo_curve_is_valid(
    const pds_mppt_demo_curve_type *candidate_curve);

#endif /* PDS_COMMAND_PAYLOADS_H */
