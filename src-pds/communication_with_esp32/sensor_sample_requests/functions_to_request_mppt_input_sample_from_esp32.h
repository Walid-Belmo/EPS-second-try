#ifndef PDS_MPPT_INPUT_SAMPLE_FROM_ESP32_H
#define PDS_MPPT_INPUT_SAMPLE_FROM_ESP32_H

#include <stdint.h>

#include "board_command_contract/board_command_ids_and_payload_layouts.h"

uint8_t request_mppt_input_sample_from_esp32_model(
    uint16_t duty_cycle_as_fraction_of_65535,
    pds_mppt_input_sample_type *sample);

#endif /* PDS_MPPT_INPUT_SAMPLE_FROM_ESP32_H */
