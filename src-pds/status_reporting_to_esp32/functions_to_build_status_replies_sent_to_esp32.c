#include <stdint.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "shared_helpers/functions_to_read_and_write_little_endian_values.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "status_reporting_to_esp32/functions_to_build_status_replies_sent_to_esp32.h"

static void append_reply_header(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    uint8_t status,
    uint32_t requested_field_mask,
    uint32_t timestamp_in_milliseconds);
static void append_command_counters(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state);
static void append_mode_and_stream_values(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state);
static void append_pwm_values(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state);
static void append_state_demo_inputs(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state);
static void append_mppt_input_status(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state);
static void append_runtime_snapshot_values(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state);

void build_ack_reply_to_esp32(
    chips_parsed_frame_type *reply_to_send,
    uint8_t status)
{
    SATELLITE_ASSERT(reply_to_send != (void *)0);

    reply_to_send->payload_bytes[0] = status;
    reply_to_send->payload_length_in_bytes = 1u;
}

void build_values_reply_to_esp32(
    chips_parsed_frame_type *reply_to_send,
    const pds_runtime_state_type *runtime_state,
    uint8_t status,
    uint32_t requested_field_mask,
    uint32_t timestamp_in_milliseconds)
{
    SATELLITE_ASSERT(reply_to_send != (void *)0);
    SATELLITE_ASSERT(runtime_state != (void *)0);

    uint16_t position = 0u;
    append_reply_header(
        reply_to_send,
        &position,
        status,
        requested_field_mask,
        timestamp_in_milliseconds);
    append_command_counters(reply_to_send, &position, runtime_state);
    append_mode_and_stream_values(reply_to_send, &position, runtime_state);
    append_pwm_values(reply_to_send, &position, runtime_state);
    append_state_demo_inputs(reply_to_send, &position, runtime_state);
    append_mppt_input_status(reply_to_send, &position, runtime_state);
    append_runtime_snapshot_values(reply_to_send, &position, runtime_state);
    reply_to_send->payload_length_in_bytes = position;
}

static void append_reply_header(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    uint8_t status,
    uint32_t requested_field_mask,
    uint32_t timestamp_in_milliseconds)
{
    write_uint8_to_payload(reply_to_send->payload_bytes, position, status);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        PDS_STATUS_PAYLOAD_VERSION);
    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        requested_field_mask);
    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        timestamp_in_milliseconds);
}

static void append_command_counters(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state)
{
    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->valid_chips_frame_count);
    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->crc_error_count);
    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->too_long_frame_count);
    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->executed_command_count);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->last_command_id);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->last_command_status);
}

static void append_mode_and_stream_values(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state)
{
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->requested_mode);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->telemetry_stream_is_enabled);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->telemetry_stream_period_ms);
    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->telemetry_field_mask);
}

static void append_pwm_values(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state)
{
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.requested_pwm_duty_cycle_as_fraction_of_65535);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.pwm_output_is_enabled);
}

static void append_state_demo_inputs(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state)
{
    const pds_state_demo_inputs_type *inputs =
        &runtime_state->injected_state_inputs;

    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->battery_voltage_in_millivolts);
    write_int16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->battery_current_in_milliamps);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->panel_voltage_in_millivolts);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->panel_current_in_milliamps);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->charging_rail_voltage_in_millivolts);
    write_int16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->battery_temperature_in_decidegrees_celsius);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->heartbeat_received);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->satellite_mode_from_obc);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->safe_mode_substate_from_obc);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        inputs->fault_flags);
}

static void append_mppt_input_status(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state)
{
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.mppt_input_sample_is_valid);
}

static void append_runtime_snapshot_values(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state)
{
    const pds_runtime_snapshot_type *snapshot = &runtime_state->snapshot;

    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->loop_count);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->panel_voltage_in_millivolts);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->panel_current_in_milliamps);
    write_uint32_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->panel_power_in_milliwatts);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->mppt_duty_cycle_as_fraction_of_65535);
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->state_machine_duty_cycle_as_fraction_of_65535);
    write_uint8_to_payload(reply_to_send->payload_bytes, position, snapshot->pcu_mode);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->safe_mode_is_active);
    write_uint8_to_payload(reply_to_send->payload_bytes, position, snapshot->safe_mode_reason);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->panel_efuse_is_enabled);
    write_uint8_to_payload(reply_to_send->payload_bytes, position, snapshot->heater_is_enabled);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        snapshot->safe_mode_alert_for_obc);
    write_uint8_to_payload(reply_to_send->payload_bytes, position, snapshot->load_enable_mask);
}
