/* =============================================================================
 * chips_protocol_dispatch_commands_and_build_responses.h
 * Dispatches received CHIPS commands to handler functions and builds
 * response frames. Sits above the frame codec layer.
 *
 * This module:
 *   - Receives parsed CHIPS frames from the frame parser
 *   - Identifies the command by its command ID
 *   - Executes the command (e.g., collects telemetry data)
 *   - Builds a response frame with status byte + response data
 *   - Sends the response over the OBC UART
 *   - Tracks idempotency: if a duplicate sequence number arrives,
 *     re-sends the cached response without re-executing the command
 *
 * Telemetry data is placeholder zeros in Phase 4. Real sensor readings
 * will be filled in by Phase 6 sensor drivers.
 * =============================================================================
 */

#ifndef CHIPS_PROTOCOL_DISPATCH_COMMANDS_AND_BUILD_RESPONSES_H
#define CHIPS_PROTOCOL_DISPATCH_COMMANDS_AND_BUILD_RESPONSES_H

#include <stdint.h>
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"

/* ── EPS telemetry data structure (from PDF p.35-36, Table 2.2.1) ──────── */

/* Total: 219 bytes. All fields are placeholder zeros until Phase 6
 * sensor drivers fill them with real ADC/I2C/SPI readings. */
typedef struct {
    uint8_t bus_voltages_for_each_power_rail[16];
    uint8_t bus_currents_for_each_power_rail[16];
    uint8_t battery_info_soc_dod_cycles[40];
    uint8_t solar_panel_voltage_and_current[8];
    uint8_t hold_down_release_mechanism_status;
    uint8_t mcu_internal_temperature[4];
    uint8_t battery_pack_temperature[16];
    uint8_t solar_panel_temperature[8];
    uint8_t command_execution_log[100];
    uint8_t active_alert_flags[10];
} eps_telemetry_data_type;

/* Size of the telemetry struct (should be 219 bytes). */
#define EPS_TELEMETRY_DATA_SIZE_IN_BYTES  219u

/* ── Public function declarations ────────────────────────────────────────── */

/* Initialize the command dispatch module. Zeroes all state including
 * the telemetry struct and idempotency tracking. Call once at boot. */
void chips_command_dispatch_initialize(void);

/* Process a received CHIPS command frame. Executes the command,
 * builds a response frame, and sends it over the OBC UART.
 * If the sequence number matches the last processed command,
 * the cached response is re-sent without re-executing. */
void chips_dispatch_received_command_and_send_response(
    const chips_parsed_frame_type *received_command_frame);

#endif /* CHIPS_PROTOCOL_DISPATCH_COMMANDS_AND_BUILD_RESPONSES_H */
