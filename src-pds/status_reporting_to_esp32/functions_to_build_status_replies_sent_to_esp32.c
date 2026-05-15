/*
 * functions_to_build_status_replies_sent_to_esp32.c
 *
 * The "telemetry packer" — given the firmware-wide runtime state, fills
 * the bytes of a CHIPS reply frame with every status field the operator's
 * web app might want to display. Two public functions:
 *
 *   build_ack_reply_to_esp32()    - one-byte status code only. Used
 *       when a command failed (bad payload length, mode-not-allowed,
 *       etc.) and there's nothing meaningful to send back.
 *
 *   build_values_reply_to_esp32() - the full status payload. Used by
 *       successful commands AND by the streaming path
 *       (send_status_if_needed). The payload is built by calling each
 *       append_* helper in sequence; the order is fixed because the
 *       ESP32 reply printer reads fields in the same order.
 *
 * Payload layout (current version 4):
 *
 *   append_reply_header                          - 10 bytes (status, version, mask, timestamp)
 *   append_command_counters                      - 18 bytes (frame stats, last-command tracking)
 *   append_mode_and_stream_values                -  8 bytes (mode, stream config)
 *   append_pwm_values                            -  7 bytes (fixed/requested/applied PWM, enabled flag)
 *   append_state_demo_inputs                     - 17 bytes (the operator's injected sensor values)
 *   append_mppt_input_status                     -  1 byte  (MPPT sample validity flag)
 *   append_runtime_snapshot_values               - 23 bytes (loop count, panel/duty/PCU mode, faults, loads)
 *   append_manual_outputs_and_efuse_status_values - 10 bytes (manual mode requested + actual readbacks)
 *   append_sensor_reads_block                    - 31 bytes (per-sensor + Layer 1 chosen values)
 *
 * Total: 125 bytes. The ESP32 reply printer's "minimum length to be a
 * status reply" check (84 bytes) is intentionally backwards-compatible:
 * older firmware payloads remain parseable.
 *
 * The append_* helpers do NOT consult the field mask — every block is
 * always emitted. The mask is for the ESP32 printer's benefit (it
 * decides which sections to render based on which bits are set), but
 * the firmware always sends everything because the per-iteration cost
 * of a few extra bytes is trivial.
 */

#include <stdint.h>

#include "assertion_handler.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "shared_helpers/functions_to_read_and_write_little_endian_values.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "sensor_inputs/sensor_readings.h"
#include "status_reporting_to_esp32/functions_to_build_status_replies_sent_to_esp32.h"

#ifdef __SAMD21J17D__
#include "ina226_battery_on_mainboard.h"
#include "ina226_panel_on_mainboard.h"
#include "lt6108_battery_outa2_on_mainboard.h"
#include "lt6108_panel_outa1_on_mainboard.h"
#include "tps25940_imon_battery_on_mainboard.h"
#include "tps25940_imon_panel_on_mainboard.h"
#include "voltage_divider_rails_on_mainboard.h"
#endif

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
static void append_manual_outputs_and_efuse_status_values(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state);
static void append_sensor_reads_block(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position);

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
    append_manual_outputs_and_efuse_status_values(
        reply_to_send, &position, runtime_state);
    append_sensor_reads_block(reply_to_send, &position);
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

/* Block 2: link-quality counters and last-command tracking. The page
 * shows these to indicate UART link health (CRC-error and too-long
 * counters bumping means bad cable, ground noise, or the ESP32 sketch
 * is out of sync with the firmware). 18 bytes total. */
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

/* Block 3: which mode the firmware is in and the streaming config the
 * page asked for. The page reads the requested-mode byte to colour its
 * mode pill, and reads the stream fields to show the operator how
 * often the firmware is pushing telemetry. 8 bytes total. */
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

/* Block 4: the four PWM-related values. fixed_pwm is what the operator
 * typed in run_pwm; requested_pwm is what the active mode runner asked
 * for; applied_pwm is what actually went to the hardware (may differ
 * from requested if block_dangerous_outputs clamped); enabled_flag is
 * a derived "is anything driving" bool for the page. 7 bytes. */
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

/* Block 5: the operator-injected sensor values from the State page.
 * NOT real sensor readings — the values the operator typed via
 * inject_state. The page uses these to show "input" tiles separate
 * from "what the state machine decided". 17 bytes. */
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

/* Block 6: a single bool saying whether the most recent MPPT iteration
 * had a valid input sample. The MPPT page renders "—" when this is 0
 * (no curve configured yet, or ESP32 timed out) and the live numbers
 * when it's 1. 1 byte. */
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

/* Block 7: the per-iteration snapshot of what the active mode runner
 * computed: loop count, latest panel V/I/P, MPPT vs state-machine duty
 * cycles, current PCU mode, safe-mode flags, panel-eFuse bool, heater
 * bool, alert flag for OBC, packed load mask. This is the bulk of the
 * State page's tiles. 23 bytes. */
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

static void append_manual_outputs_and_efuse_status_values(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position,
    const pds_runtime_state_type *runtime_state)
{
    /* Always appended (not gated by the requested field mask) so the manual
     * control page can render its readouts even when it asks for a small
     * subset of telemetry. The whole block is 11 bytes. */
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->manual_pwm_duty_cycle_as_fraction_of_65535);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->manual_pv_switch_requested);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->manual_bat_switch_requested);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->manual_status_led_requested);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.status_led_is_on);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.pv_efuse_power_good);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.pv_efuse_fault_active);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.bat_efuse_power_good);
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        runtime_state->snapshot.bat_efuse_fault_active);
}

static void append_sensor_reads_block(
    chips_parsed_frame_type *reply_to_send,
    uint16_t *position)
{
    /* Always-on observability block. Every status reply carries each
     * sensor's reading and what Layer 1 chose, regardless of the active
     * firmware mode. The block is 31 bytes:
     *
     *   1  source enum (0=INJECTED, 1=REAL_BOARD_HARDWARE)
     *  10  Layer 1 chosen values (5 readings)
     *  20  Layer 2 raw per-sensor readings (10 readings)
     *
     * On the dev board the Layer 2 modules are not compiled in, so the
     * per-sensor fields are written as 0. Layer 1 fields work on both
     * builds because sensor_readings.c compiles unconditionally. */

    /* Source. */
    write_uint8_to_payload(
        reply_to_send->payload_bytes,
        position,
        (uint8_t)get_current_sensor_source());

    /* Layer 1 chosen values — what the state machine and MPPT actually see. */
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position, read_battery_voltage());
    write_int16_to_little_endian_payload(
        reply_to_send->payload_bytes, position, read_battery_current());
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position, read_panel_voltage());
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position, read_panel_current());
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position, read_charging_rail_voltage());

#ifdef __SAMD21J17D__
    /* Layer 2 raw per-sensor readings. Each call goes to the chip
     * (I²C for INA226, ADC for the others). At 250 ms stream cadence
     * the total of 4 I²C reads + 6 ADC sweeps is roughly 3 ms per reply,
     * about 1% CPU. A future shared per-iteration cache would cut this
     * to one ADC sweep total but adds plumbing — defer until needed. */
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        battery_ina226_read_voltage_mv());
    write_int16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        battery_ina226_read_current_ma());
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        panel_ina226_read_voltage_mv());
    write_int16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        panel_ina226_read_current_ma());
    write_int16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        battery_lt6108_read_current_ma());
    write_int16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        panel_lt6108_read_current_ma());
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        battery_tps25940_read_imon_ma());
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        panel_tps25940_read_imon_ma());
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        rail_divider_read_charging_rail_voltage_mv());
    write_uint16_to_little_endian_payload(
        reply_to_send->payload_bytes, position,
        rail_divider_read_panel_bus_voltage_mv());
#else
    /* Dev board: no real chips, write 20 zero bytes. The page renders
     * 0s in those tiles; Layer 1's chosen value (above) still reflects
     * the injected source. */
    for (uint8_t zero_index = 0u; zero_index < 10u; zero_index += 1u)
    {
        write_uint16_to_little_endian_payload(
            reply_to_send->payload_bytes, position, 0u);
    }
#endif
}
