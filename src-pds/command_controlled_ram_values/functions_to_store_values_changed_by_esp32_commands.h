#ifndef PDS_RUNTIME_STORE_H
#define PDS_RUNTIME_STORE_H

#include <stdint.h>

#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "command_controlled_ram_values/structures_that_describe_values_changed_by_esp32_commands.h"

void set_starting_ram_values_to_safe_defaults(void);

pds_runtime_state_type *get_pointer_to_pds_runtime_ram_values(void);

uint8_t requested_mode_is_off(void);
uint8_t requested_mode_is_flight(void);
uint8_t requested_mode_is_mppt_test(void);
uint8_t requested_mode_is_state_test(void);
uint8_t requested_mode_is_fixed_pwm_test(void);

uint8_t pds_control_loop_period_has_elapsed(void);
void reset_mppt_demo_ram_values_after_new_curve(void);
void reset_state_demo_ram_values_after_new_inputs(void);

void record_valid_chips_frame_from_esp32(void);
void record_broken_chips_message_without_changing_outputs(
    chips_parser_result_type parser_result);
void remember_result_of_command_from_esp32(
    const chips_parsed_frame_type *received_command,
    const chips_parsed_frame_type *reply_to_send);

#endif /* PDS_RUNTIME_STORE_H */
