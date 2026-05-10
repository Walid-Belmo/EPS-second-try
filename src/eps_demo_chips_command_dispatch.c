/* =============================================================================
 * eps_demo_chips_command_dispatch.c
 * Demo CHIPS command handlers for injected values and firmware visibility.
 * =============================================================================
 */

#include <stdint.h>

#include "assertion_handler.h"
#include "debug_functions.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "uart_obc.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "eps_demo_chips_command_dispatch.h"
#include "mppt_algorithm.h"
#include "eps_state_machine.h"
#include "pwm_buck_converter.h"

#ifdef __SAMD21J17D__
#include "mainboard_adc_reader.h"
#endif

#define INPUT_SOURCE_INJECTED_VALUES       0u
#define INPUT_SOURCE_REAL_SENSORS          1u

#define CONTROL_MODE_OFF                   0u
#define CONTROL_MODE_FIXED_DUTY            1u
#define CONTROL_MODE_VOLTAGE_REGULATION    2u
#define CONTROL_MODE_MPPT                  3u
#define CONTROL_MODE_FULL_STATE_MACHINE    4u

#define MINIMUM_TELEMETRY_STREAM_PERIOD_MS 100u
#define MAXIMUM_TELEMETRY_STREAM_PERIOD_MS 10000u
#define MINIMUM_DEMO_TIMEOUT_ITERATIONS     1u
#define MAXIMUM_DEMO_TIMEOUT_ITERATIONS     60000u

#define TELEMETRY_PAYLOAD_PROTOCOL_VERSION  2u
#define CONTROL_LOOP_PERIOD_MS              100u
#define PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE   5u
#define PANEL_RAW_ADC_TO_MILLIAMPS_SCALE    2u
#define VOLTAGE_REGULATION_DEADBAND_MV      50u
#define DEMO_SAFE_REASON_INJECTED_FAULT     4u
#define EPS_LOAD_ENABLE_MASK_ALL_LOADS      0x1Fu

typedef struct {
    uint16_t panel_voltage_raw_adc;
    uint16_t panel_current_raw_adc;
    uint16_t battery_voltage_mv;
    int16_t  battery_current_ma;
    uint16_t charging_rail_voltage_mv;
    int16_t  battery_temperature_decicelsius;
    uint8_t  obc_heartbeat_present;
    uint8_t  satellite_mode_commanded_by_obc;
    uint8_t  safe_mode_sub_state_commanded_by_obc;
    uint16_t fault_flags;
} eps_demo_injected_sensor_frame_type;

typedef struct {
    uint32_t control_iteration_count;
    uint32_t input_power_mw;
    uint16_t panel_voltage_mv;
    uint16_t panel_current_ma;
    uint16_t mppt_duty_cycle_as_fraction_of_65535;
    uint16_t state_machine_duty_cycle_as_fraction_of_65535;
    uint16_t applied_pwm_duty_cycle_as_fraction_of_65535;
    uint8_t  pcu_mode;
    uint8_t  safe_mode_active;
    uint8_t  safe_mode_reason;
    uint8_t  panel_efuse_enabled;
    uint8_t  heater_enabled;
    uint8_t  safe_mode_alert;
    uint8_t  load_enable_mask;
    uint8_t  pwm_output_enabled;
    uint8_t  last_control_mode_executed;
} eps_demo_control_snapshot_type;

static struct eps_demo_chips_dispatch_state {
    uint8_t has_processed_at_least_one_command;
    uint8_t last_processed_sequence_number;
    uint8_t last_command_id;
    uint8_t last_command_status;

    uint8_t cached_response_wire_format_bytes[
        CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES];
    uint16_t cached_response_total_length_in_bytes;

    uint8_t periodic_response_wire_format_bytes[
        CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES];
    uint8_t periodic_stream_sequence_number;

    uint32_t valid_frame_count;
    uint32_t crc_error_count;
    uint32_t frame_too_long_error_count;
    uint32_t command_execution_count;

    eps_demo_injected_sensor_frame_type injected_inputs;
    uint8_t input_source;
    uint8_t control_mode;
    uint16_t fixed_duty_cycle_as_fraction_of_65535;

    uint8_t telemetry_stream_enabled;
    uint16_t telemetry_stream_period_ms;
    uint32_t last_telemetry_stream_timestamp_ms;

    struct mppt_algorithm_state standalone_mppt_state;
    struct eps_state_machine_persistent_state eps_state_machine_state;
    struct eps_configuration_thresholds eps_configuration_thresholds;
    eps_demo_control_snapshot_type control_snapshot;
    uint32_t last_control_loop_timestamp_ms;
    uint16_t voltage_regulation_duty_cycle_as_fraction_of_65535;
} dispatch_state;

static uint16_t read_u16_le(const uint8_t *bytes);
static int16_t read_i16_le(const uint8_t *bytes);
static void write_u8(uint8_t *buffer, uint16_t *position, uint8_t value);
static void write_u16_le(uint8_t *buffer, uint16_t *position, uint16_t value);
static void write_i16_le(uint8_t *buffer, uint16_t *position, int16_t value);
static void write_u32_le(uint8_t *buffer, uint16_t *position, uint32_t value);
static void write_injected_frame_to_payload(uint8_t *buffer,
                                            uint16_t *position);
static void write_control_snapshot_to_payload(uint8_t *buffer,
                                              uint16_t *position);
static void initialize_default_eps_configuration_thresholds(void);
static void initialize_demo_control_logic(void);
static void reset_demo_control_state_for_new_mode(void);
static void run_demo_control_loop_once(void);
static void build_state_machine_sensor_readings_from_injected_inputs(
    struct eps_sensor_readings_this_iteration *sensor_readings);
static uint16_t derive_panel_voltage_mv_from_raw_adc(void);
static uint16_t derive_panel_current_ma_from_raw_adc(void);
static uint8_t build_load_enable_mask(
    const struct eps_actuator_output_commands *actuator_commands);
static uint16_t run_voltage_regulation_step(
    uint16_t current_duty_cycle_as_fraction_of_65535);
static uint16_t clamp_demo_duty_cycle_to_mppt_range(uint16_t duty_cycle);
static void build_telemetry_payload(chips_parsed_frame_type *response_to_build,
                                    uint8_t response_status);
static void build_ack_payload(chips_parsed_frame_type *response_to_build,
                              uint8_t response_status);
static void handle_set_injected_sensor_frame(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);
static void handle_get_telemetry(
    chips_parsed_frame_type *response_to_build);
static void handle_get_state(
    chips_parsed_frame_type *response_to_build);
static void handle_set_input_source(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);
static void handle_set_control_mode(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);
static void handle_set_fixed_duty(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);
static void handle_set_telemetry_stream(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);
static void handle_set_demo_timing(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);
static void handle_get_mainboard_adc(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build);
static void build_unknown_command_response(
    chips_parsed_frame_type *response_to_build);
static void send_response_frame_and_update_cache(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_send);
static void send_wire_format_response_over_obc_uart(
    const uint8_t *wire_bytes,
    uint16_t wire_length);
static uint8_t injected_adc_values_are_in_range(
    const eps_demo_injected_sensor_frame_type *candidate_frame);

void eps_demo_chips_command_dispatch_initialize(void)
{
    dispatch_state.has_processed_at_least_one_command = 0u;
    dispatch_state.last_processed_sequence_number = 0u;
    dispatch_state.last_command_id = 0u;
    dispatch_state.last_command_status = (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;
    dispatch_state.cached_response_total_length_in_bytes = 0u;
    dispatch_state.periodic_stream_sequence_number = 0u;

    dispatch_state.valid_frame_count = 0u;
    dispatch_state.crc_error_count = 0u;
    dispatch_state.frame_too_long_error_count = 0u;
    dispatch_state.command_execution_count = 0u;

    dispatch_state.injected_inputs.panel_voltage_raw_adc = 1800u;
    dispatch_state.injected_inputs.panel_current_raw_adc = 1100u;
    dispatch_state.injected_inputs.battery_voltage_mv = 7400u;
    dispatch_state.injected_inputs.battery_current_ma = 250;
    dispatch_state.injected_inputs.charging_rail_voltage_mv = 7600u;
    dispatch_state.injected_inputs.battery_temperature_decicelsius = 220;
    dispatch_state.injected_inputs.obc_heartbeat_present = 1u;
    dispatch_state.injected_inputs.satellite_mode_commanded_by_obc = 1u;
    dispatch_state.injected_inputs.safe_mode_sub_state_commanded_by_obc = 1u;
    dispatch_state.injected_inputs.fault_flags = 0u;

    dispatch_state.input_source = INPUT_SOURCE_INJECTED_VALUES;
    dispatch_state.control_mode = CONTROL_MODE_OFF;
    dispatch_state.fixed_duty_cycle_as_fraction_of_65535 = 32768u;

    dispatch_state.telemetry_stream_enabled = 0u;
    dispatch_state.telemetry_stream_period_ms = 1000u;
    dispatch_state.last_telemetry_stream_timestamp_ms = 0u;

    initialize_demo_control_logic();
}

void eps_demo_chips_note_valid_frame_received(void)
{
    dispatch_state.valid_frame_count += 1u;
}

void eps_demo_chips_note_parser_result(chips_parser_result_type parser_result)
{
    if (parser_result == CHIPS_PARSER_RESULT_ERROR_CRC_MISMATCH)
    {
        dispatch_state.crc_error_count += 1u;
    }
    else if (parser_result == CHIPS_PARSER_RESULT_ERROR_FRAME_TOO_LONG)
    {
        dispatch_state.frame_too_long_error_count += 1u;
    }
}

void eps_demo_chips_dispatch_received_command_and_send_response(
    const chips_parsed_frame_type *received_command_frame)
{
    SATELLITE_ASSERT(received_command_frame != (void *)0);

    if (received_command_frame->response_flag != 0u)
    {
        return;
    }

    if ((dispatch_state.has_processed_at_least_one_command != 0u)
        && (received_command_frame->sequence_number
            == dispatch_state.last_processed_sequence_number))
    {
        send_wire_format_response_over_obc_uart(
            dispatch_state.cached_response_wire_format_bytes,
            dispatch_state.cached_response_total_length_in_bytes);
        return;
    }

    chips_parsed_frame_type response_frame;
    response_frame.sequence_number = received_command_frame->sequence_number;
    response_frame.command_id = received_command_frame->command_id;
    response_frame.response_flag = 1u;
    response_frame.payload_length_in_bytes = 0u;

    switch (received_command_frame->command_id)
    {
    case CHIPS_COMMAND_ID_GET_TELEMETRY:
        handle_get_telemetry(&response_frame);
        break;

    case CHIPS_COMMAND_ID_GET_STATE:
        handle_get_state(&response_frame);
        break;

    case CHIPS_COMMAND_ID_SET_INJECTED_SENSOR_FRAME:
        handle_set_injected_sensor_frame(received_command_frame,
                                         &response_frame);
        break;

    case CHIPS_COMMAND_ID_SET_INPUT_SOURCE:
        handle_set_input_source(received_command_frame, &response_frame);
        break;

    case CHIPS_COMMAND_ID_SET_CONTROL_MODE:
    case CHIPS_COMMAND_ID_SET_MODE:
        handle_set_control_mode(received_command_frame, &response_frame);
        break;

    case CHIPS_COMMAND_ID_SET_FIXED_DUTY:
        handle_set_fixed_duty(received_command_frame, &response_frame);
        break;

    case CHIPS_COMMAND_ID_GET_DEBUG_SNAPSHOT:
        handle_get_telemetry(&response_frame);
        break;

    case CHIPS_COMMAND_ID_SET_TELEMETRY_STREAM:
        handle_set_telemetry_stream(received_command_frame, &response_frame);
        break;

    case CHIPS_COMMAND_ID_SET_DEMO_TIMING:
        handle_set_demo_timing(received_command_frame, &response_frame);
        break;

    case CHIPS_COMMAND_ID_GET_MAINBOARD_ADC:
        handle_get_mainboard_adc(received_command_frame, &response_frame);
        break;

    default:
        build_unknown_command_response(&response_frame);
        break;
    }

    dispatch_state.command_execution_count += 1u;
    dispatch_state.last_command_id = received_command_frame->command_id;
    dispatch_state.last_command_status = response_frame.payload_bytes[0];

    DEBUG_LOG_UINT("CHIPS cmd", (uint32_t)received_command_frame->command_id);
    DEBUG_LOG_UINT("CHIPS status", (uint32_t)response_frame.payload_bytes[0]);

    send_response_frame_and_update_cache(received_command_frame,
                                         &response_frame);
}

void eps_demo_chips_send_periodic_telemetry_if_due(void)
{
    if (dispatch_state.telemetry_stream_enabled == 0u)
    {
        return;
    }

    uint32_t now_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
    uint32_t elapsed_ms =
        now_ms - dispatch_state.last_telemetry_stream_timestamp_ms;

    if (elapsed_ms < dispatch_state.telemetry_stream_period_ms)
    {
        return;
    }

    chips_parsed_frame_type stream_frame;
    stream_frame.sequence_number =
        dispatch_state.periodic_stream_sequence_number;
    dispatch_state.periodic_stream_sequence_number += 1u;
    stream_frame.command_id = CHIPS_COMMAND_ID_GET_TELEMETRY;
    stream_frame.response_flag = 1u;
    build_telemetry_payload(&stream_frame,
                            (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);

    uint16_t wire_length =
        chips_build_stuffed_frame_with_sync_and_crc_into_buffer(
            &stream_frame,
            dispatch_state.periodic_response_wire_format_bytes,
            CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES);

    SATELLITE_ASSERT(wire_length > 0u);
    send_wire_format_response_over_obc_uart(
        dispatch_state.periodic_response_wire_format_bytes,
        wire_length);

    dispatch_state.last_telemetry_stream_timestamp_ms = now_ms;
}

void eps_demo_chips_run_control_loop_if_due(void)
{
    uint32_t now_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
    uint32_t elapsed_ms =
        now_ms - dispatch_state.last_control_loop_timestamp_ms;

    if (elapsed_ms < CONTROL_LOOP_PERIOD_MS)
    {
        return;
    }

    dispatch_state.last_control_loop_timestamp_ms = now_ms;
    run_demo_control_loop_once();
}

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
}

static int16_t read_i16_le(const uint8_t *bytes)
{
    return (int16_t)read_u16_le(bytes);
}

static void write_u8(uint8_t *buffer, uint16_t *position, uint8_t value)
{
    buffer[*position] = value;
    *position = (uint16_t)(*position + 1u);
}

static void write_u16_le(uint8_t *buffer, uint16_t *position, uint16_t value)
{
    buffer[*position] = (uint8_t)(value & 0xFFu);
    *position = (uint16_t)(*position + 1u);
    buffer[*position] = (uint8_t)((value >> 8u) & 0xFFu);
    *position = (uint16_t)(*position + 1u);
}

static void write_i16_le(uint8_t *buffer, uint16_t *position, int16_t value)
{
    write_u16_le(buffer, position, (uint16_t)value);
}

static void write_u32_le(uint8_t *buffer, uint16_t *position, uint32_t value)
{
    buffer[*position] = (uint8_t)(value & 0xFFu);
    *position = (uint16_t)(*position + 1u);
    buffer[*position] = (uint8_t)((value >> 8u) & 0xFFu);
    *position = (uint16_t)(*position + 1u);
    buffer[*position] = (uint8_t)((value >> 16u) & 0xFFu);
    *position = (uint16_t)(*position + 1u);
    buffer[*position] = (uint8_t)((value >> 24u) & 0xFFu);
    *position = (uint16_t)(*position + 1u);
}

static void write_injected_frame_to_payload(uint8_t *buffer,
                                            uint16_t *position)
{
    write_u16_le(buffer, position,
                 dispatch_state.injected_inputs.panel_voltage_raw_adc);
    write_u16_le(buffer, position,
                 dispatch_state.injected_inputs.panel_current_raw_adc);
    write_u16_le(buffer, position,
                 dispatch_state.injected_inputs.battery_voltage_mv);
    write_i16_le(buffer, position,
                 dispatch_state.injected_inputs.battery_current_ma);
    write_u16_le(buffer, position,
                 dispatch_state.injected_inputs.charging_rail_voltage_mv);
    write_i16_le(buffer, position,
                 dispatch_state.injected_inputs.battery_temperature_decicelsius);
    write_u8(buffer, position,
             dispatch_state.injected_inputs.obc_heartbeat_present);
    write_u8(buffer, position,
             dispatch_state.injected_inputs.satellite_mode_commanded_by_obc);
    write_u8(buffer, position,
             dispatch_state.injected_inputs.safe_mode_sub_state_commanded_by_obc);
    write_u16_le(buffer, position,
                 dispatch_state.injected_inputs.fault_flags);
}

static void write_control_snapshot_to_payload(uint8_t *buffer,
                                              uint16_t *position)
{
    write_u32_le(buffer, position,
                 dispatch_state.control_snapshot.control_iteration_count);
    write_u32_le(buffer, position,
                 dispatch_state.control_snapshot.input_power_mw);
    write_u16_le(buffer, position,
                 dispatch_state.control_snapshot.panel_voltage_mv);
    write_u16_le(buffer, position,
                 dispatch_state.control_snapshot.panel_current_ma);
    write_u16_le(buffer, position,
                 dispatch_state.control_snapshot.mppt_duty_cycle_as_fraction_of_65535);
    write_u16_le(buffer, position,
                 dispatch_state.control_snapshot.state_machine_duty_cycle_as_fraction_of_65535);
    write_u16_le(buffer, position,
                 dispatch_state.control_snapshot.applied_pwm_duty_cycle_as_fraction_of_65535);
    write_u8(buffer, position, dispatch_state.control_snapshot.pcu_mode);
    write_u8(buffer, position, dispatch_state.control_snapshot.safe_mode_active);
    write_u8(buffer, position, dispatch_state.control_snapshot.safe_mode_reason);
    write_u8(buffer, position, dispatch_state.control_snapshot.panel_efuse_enabled);
    write_u8(buffer, position, dispatch_state.control_snapshot.heater_enabled);
    write_u8(buffer, position, dispatch_state.control_snapshot.safe_mode_alert);
    write_u8(buffer, position, dispatch_state.control_snapshot.load_enable_mask);
    write_u8(buffer, position, dispatch_state.control_snapshot.pwm_output_enabled);
    write_u8(buffer, position, dispatch_state.control_snapshot.last_control_mode_executed);
}

static void initialize_default_eps_configuration_thresholds(void)
{
    dispatch_state.eps_configuration_thresholds.battery_voltage_maximum_in_millivolts =
        8400u;
    dispatch_state.eps_configuration_thresholds.battery_voltage_full_threshold_in_millivolts =
        8300u;
    dispatch_state.eps_configuration_thresholds.battery_voltage_charge_resume_threshold_in_millivolts =
        8100u;
    dispatch_state.eps_configuration_thresholds.battery_voltage_minimum_in_millivolts =
        5000u;
    dispatch_state.eps_configuration_thresholds.battery_voltage_critical_in_millivolts =
        5500u;
    dispatch_state.eps_configuration_thresholds.battery_voltage_hysteresis_margin_in_millivolts =
        200u;
    dispatch_state.eps_configuration_thresholds.battery_current_maximum_charge_in_milliamps =
        2000;
    dispatch_state.eps_configuration_thresholds.battery_current_maximum_discharge_in_milliamps =
        -2000;
    dispatch_state.eps_configuration_thresholds.battery_current_minimum_charge_threshold_in_milliamps =
        100;
    dispatch_state.eps_configuration_thresholds.solar_array_minimum_voltage_for_availability_in_millivolts =
        8200u;
    dispatch_state.eps_configuration_thresholds.temperature_minimum_for_heater_activation_in_decidegrees =
        -100;
    dispatch_state.eps_configuration_thresholds.temperature_maximum_for_load_shedding_in_decidegrees =
        600;
    dispatch_state.eps_configuration_thresholds.temperature_minimum_for_charging_allowed_in_decidegrees =
        0;
    dispatch_state.eps_configuration_thresholds.mppt_charge_timeout_for_insufficient_buffer_in_iterations =
        1200u;
    dispatch_state.eps_configuration_thresholds.cv_float_low_voltage_wait_timeout_in_iterations =
        1200u;
    dispatch_state.eps_configuration_thresholds.obc_heartbeat_timeout_in_iterations =
        1200u;
    dispatch_state.eps_configuration_thresholds.cv_float_duty_cycle_adjustment_step_size =
        164u;
}

static void initialize_demo_control_logic(void)
{
    initialize_default_eps_configuration_thresholds();
    mppt_algorithm_initialize(&dispatch_state.standalone_mppt_state);
    eps_state_machine_initialize(
        &dispatch_state.eps_state_machine_state,
        &dispatch_state.eps_configuration_thresholds,
        (uint8_t)EPS_PCU_MODE_MPPT_CHARGE);

    dispatch_state.control_snapshot.control_iteration_count = 0u;
    dispatch_state.control_snapshot.input_power_mw = 0u;
    dispatch_state.control_snapshot.panel_voltage_mv =
        derive_panel_voltage_mv_from_raw_adc();
    dispatch_state.control_snapshot.panel_current_ma =
        derive_panel_current_ma_from_raw_adc();
    dispatch_state.control_snapshot.mppt_duty_cycle_as_fraction_of_65535 =
        32768u;
    dispatch_state.control_snapshot.state_machine_duty_cycle_as_fraction_of_65535 =
        32768u;
    dispatch_state.control_snapshot.applied_pwm_duty_cycle_as_fraction_of_65535 =
        0u;
    dispatch_state.control_snapshot.pcu_mode = (uint8_t)EPS_PCU_MODE_MPPT_CHARGE;
    dispatch_state.control_snapshot.safe_mode_active = 0u;
    dispatch_state.control_snapshot.safe_mode_reason = 0u;
    dispatch_state.control_snapshot.panel_efuse_enabled = 0u;
    dispatch_state.control_snapshot.heater_enabled = 0u;
    dispatch_state.control_snapshot.safe_mode_alert = 0u;
    dispatch_state.control_snapshot.load_enable_mask = EPS_LOAD_ENABLE_MASK_ALL_LOADS;
    dispatch_state.control_snapshot.pwm_output_enabled = 0u;
    dispatch_state.control_snapshot.last_control_mode_executed = CONTROL_MODE_OFF;

    dispatch_state.last_control_loop_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();
    dispatch_state.voltage_regulation_duty_cycle_as_fraction_of_65535 =
        32768u;

    pwm_buck_converter_initialize_for_demo();
    pwm_buck_converter_set_duty_cycle(0u);
}

static void reset_demo_control_state_for_new_mode(void)
{
    mppt_algorithm_initialize(&dispatch_state.standalone_mppt_state);
    eps_state_machine_initialize(
        &dispatch_state.eps_state_machine_state,
        &dispatch_state.eps_configuration_thresholds,
        (uint8_t)EPS_PCU_MODE_MPPT_CHARGE);

    dispatch_state.voltage_regulation_duty_cycle_as_fraction_of_65535 =
        dispatch_state.fixed_duty_cycle_as_fraction_of_65535;
    dispatch_state.control_snapshot.mppt_duty_cycle_as_fraction_of_65535 =
        32768u;
    dispatch_state.control_snapshot.state_machine_duty_cycle_as_fraction_of_65535 =
        32768u;
    dispatch_state.control_snapshot.applied_pwm_duty_cycle_as_fraction_of_65535 =
        0u;
    dispatch_state.control_snapshot.pwm_output_enabled = 0u;
    pwm_buck_converter_set_duty_cycle(0u);
}

static void run_demo_control_loop_once(void)
{
    struct eps_sensor_readings_this_iteration sensor_readings;
    struct eps_actuator_output_commands actuator_commands;
    uint16_t applied_duty_cycle_as_fraction_of_65535 = 0u;
    uint16_t mppt_duty_cycle_as_fraction_of_65535 =
        dispatch_state.control_snapshot.mppt_duty_cycle_as_fraction_of_65535;
    uint16_t state_machine_duty_cycle_as_fraction_of_65535 =
        dispatch_state.control_snapshot.state_machine_duty_cycle_as_fraction_of_65535;
    uint8_t pcu_mode = dispatch_state.control_snapshot.pcu_mode;
    uint8_t safe_mode_active =
        dispatch_state.eps_state_machine_state.safe_mode_is_active;
    uint8_t safe_mode_reason = (uint8_t)EPS_SAFE_REASON_NONE;
    uint8_t panel_efuse_enabled = 0u;
    uint8_t heater_enabled = 0u;
    uint8_t safe_mode_alert = 0u;
    uint8_t load_enable_mask = EPS_LOAD_ENABLE_MASK_ALL_LOADS;
    uint8_t pwm_output_enabled = 0u;

    build_state_machine_sensor_readings_from_injected_inputs(&sensor_readings);

    actuator_commands.buck_converter_duty_cycle_as_fraction_of_65535 = 0u;
    actuator_commands.panel_efuse_should_be_enabled = 0u;
    actuator_commands.heater_should_be_enabled = 0u;
    actuator_commands.safe_mode_alert_flag_for_obc = 0u;
    actuator_commands.safe_mode_alert_reason = (uint8_t)EPS_SAFE_REASON_NONE;
    actuator_commands.current_pcu_mode_for_telemetry = pcu_mode;
    for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_COUNT;
         load_index += 1u)
    {
        actuator_commands.load_enable_flags[load_index] = 1u;
    }

    if (dispatch_state.input_source != INPUT_SOURCE_INJECTED_VALUES)
    {
        applied_duty_cycle_as_fraction_of_65535 = 0u;
    }
    else if (dispatch_state.control_mode == CONTROL_MODE_OFF)
    {
        applied_duty_cycle_as_fraction_of_65535 = 0u;
    }
    else if (dispatch_state.control_mode == CONTROL_MODE_FIXED_DUTY)
    {
        applied_duty_cycle_as_fraction_of_65535 =
            clamp_demo_duty_cycle_to_mppt_range(
                dispatch_state.fixed_duty_cycle_as_fraction_of_65535);
        pwm_output_enabled =
            (applied_duty_cycle_as_fraction_of_65535 > 0u) ? 1u : 0u;
        panel_efuse_enabled = pwm_output_enabled;
    }
    else if (dispatch_state.control_mode == CONTROL_MODE_VOLTAGE_REGULATION)
    {
        dispatch_state.voltage_regulation_duty_cycle_as_fraction_of_65535 =
            run_voltage_regulation_step(
                dispatch_state.voltage_regulation_duty_cycle_as_fraction_of_65535);
        applied_duty_cycle_as_fraction_of_65535 =
            dispatch_state.voltage_regulation_duty_cycle_as_fraction_of_65535;
        pwm_output_enabled = 1u;
        panel_efuse_enabled = 1u;
    }
    else if (dispatch_state.control_mode == CONTROL_MODE_MPPT)
    {
        mppt_duty_cycle_as_fraction_of_65535 =
            mppt_algorithm_run_one_iteration(
                &dispatch_state.standalone_mppt_state,
                dispatch_state.injected_inputs.panel_voltage_raw_adc,
                dispatch_state.injected_inputs.panel_current_raw_adc);
        applied_duty_cycle_as_fraction_of_65535 =
            mppt_duty_cycle_as_fraction_of_65535;
        pwm_output_enabled = 1u;
        panel_efuse_enabled = 1u;
    }
    else
    {
        eps_state_machine_run_one_iteration(
            &dispatch_state.eps_state_machine_state,
            &sensor_readings,
            &dispatch_state.eps_configuration_thresholds,
            &actuator_commands);

        state_machine_duty_cycle_as_fraction_of_65535 =
            actuator_commands.buck_converter_duty_cycle_as_fraction_of_65535;
        pcu_mode = actuator_commands.current_pcu_mode_for_telemetry;
        safe_mode_active =
            dispatch_state.eps_state_machine_state.safe_mode_is_active;
        safe_mode_reason = actuator_commands.safe_mode_alert_reason;
        panel_efuse_enabled = actuator_commands.panel_efuse_should_be_enabled;
        heater_enabled = actuator_commands.heater_should_be_enabled;
        safe_mode_alert = actuator_commands.safe_mode_alert_flag_for_obc;
        load_enable_mask = build_load_enable_mask(&actuator_commands);

        if (actuator_commands.panel_efuse_should_be_enabled != 0u)
        {
            applied_duty_cycle_as_fraction_of_65535 =
                state_machine_duty_cycle_as_fraction_of_65535;
            pwm_output_enabled = 1u;
        }
    }

    if (dispatch_state.injected_inputs.fault_flags != 0u)
    {
        applied_duty_cycle_as_fraction_of_65535 = 0u;
        pwm_output_enabled = 0u;
        panel_efuse_enabled = 0u;
        safe_mode_active = 1u;
        safe_mode_alert = 1u;
        safe_mode_reason = DEMO_SAFE_REASON_INJECTED_FAULT;
    }

    pwm_buck_converter_set_duty_cycle(applied_duty_cycle_as_fraction_of_65535);

    dispatch_state.control_snapshot.control_iteration_count += 1u;
    dispatch_state.control_snapshot.panel_voltage_mv =
        derive_panel_voltage_mv_from_raw_adc();
    dispatch_state.control_snapshot.panel_current_ma =
        derive_panel_current_ma_from_raw_adc();
    dispatch_state.control_snapshot.input_power_mw =
        ((uint32_t)dispatch_state.control_snapshot.panel_voltage_mv
         * (uint32_t)dispatch_state.control_snapshot.panel_current_ma)
        / 1000u;
    dispatch_state.control_snapshot.mppt_duty_cycle_as_fraction_of_65535 =
        mppt_duty_cycle_as_fraction_of_65535;
    dispatch_state.control_snapshot.state_machine_duty_cycle_as_fraction_of_65535 =
        state_machine_duty_cycle_as_fraction_of_65535;
    dispatch_state.control_snapshot.applied_pwm_duty_cycle_as_fraction_of_65535 =
        applied_duty_cycle_as_fraction_of_65535;
    dispatch_state.control_snapshot.pcu_mode = pcu_mode;
    dispatch_state.control_snapshot.safe_mode_active = safe_mode_active;
    dispatch_state.control_snapshot.safe_mode_reason = safe_mode_reason;
    dispatch_state.control_snapshot.panel_efuse_enabled = panel_efuse_enabled;
    dispatch_state.control_snapshot.heater_enabled = heater_enabled;
    dispatch_state.control_snapshot.safe_mode_alert = safe_mode_alert;
    dispatch_state.control_snapshot.load_enable_mask = load_enable_mask;
    dispatch_state.control_snapshot.pwm_output_enabled = pwm_output_enabled;
    dispatch_state.control_snapshot.last_control_mode_executed =
        dispatch_state.control_mode;
}

static void build_state_machine_sensor_readings_from_injected_inputs(
    struct eps_sensor_readings_this_iteration *sensor_readings)
{
    SATELLITE_ASSERT(sensor_readings != (void *)0);

    sensor_readings->battery_voltage_in_millivolts =
        dispatch_state.injected_inputs.battery_voltage_mv;
    sensor_readings->battery_current_in_milliamps =
        dispatch_state.injected_inputs.battery_current_ma;
    sensor_readings->solar_array_voltage_in_millivolts =
        derive_panel_voltage_mv_from_raw_adc();
    sensor_readings->solar_array_voltage_raw_adc_reading =
        dispatch_state.injected_inputs.panel_voltage_raw_adc;
    sensor_readings->solar_array_current_raw_adc_reading =
        dispatch_state.injected_inputs.panel_current_raw_adc;
    sensor_readings->charging_rail_voltage_in_millivolts =
        dispatch_state.injected_inputs.charging_rail_voltage_mv;
    sensor_readings->battery_temperature_in_decidegrees_celsius =
        dispatch_state.injected_inputs.battery_temperature_decicelsius;
    sensor_readings->obc_heartbeat_received_this_iteration =
        dispatch_state.injected_inputs.obc_heartbeat_present;
    sensor_readings->satellite_mode_commanded_by_obc =
        dispatch_state.injected_inputs.satellite_mode_commanded_by_obc;
    sensor_readings->safe_mode_sub_state_commanded_by_obc =
        dispatch_state.injected_inputs.safe_mode_sub_state_commanded_by_obc;
}

static uint16_t derive_panel_voltage_mv_from_raw_adc(void)
{
    return (uint16_t)(
        dispatch_state.injected_inputs.panel_voltage_raw_adc
        * PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE);
}

static uint16_t derive_panel_current_ma_from_raw_adc(void)
{
    return (uint16_t)(
        dispatch_state.injected_inputs.panel_current_raw_adc
        * PANEL_RAW_ADC_TO_MILLIAMPS_SCALE);
}

static uint8_t build_load_enable_mask(
    const struct eps_actuator_output_commands *actuator_commands)
{
    SATELLITE_ASSERT(actuator_commands != (void *)0);

    uint8_t load_enable_mask = 0u;
    for (uint8_t load_index = 0u; load_index < (uint8_t)EPS_LOAD_COUNT;
         load_index += 1u)
    {
        if (actuator_commands->load_enable_flags[load_index] != 0u)
        {
            load_enable_mask |= (uint8_t)(1u << load_index);
        }
    }

    return load_enable_mask;
}

static uint16_t run_voltage_regulation_step(
    uint16_t current_duty_cycle_as_fraction_of_65535)
{
    uint16_t target_voltage_mv =
        dispatch_state.eps_configuration_thresholds.battery_voltage_maximum_in_millivolts;
    uint16_t step =
        dispatch_state.eps_configuration_thresholds.cv_float_duty_cycle_adjustment_step_size;
    uint16_t rail_voltage_mv =
        dispatch_state.injected_inputs.charging_rail_voltage_mv;
    uint16_t next_duty_cycle = current_duty_cycle_as_fraction_of_65535;

    if (rail_voltage_mv < (uint16_t)(target_voltage_mv - VOLTAGE_REGULATION_DEADBAND_MV))
    {
        uint32_t increased_duty_cycle =
            (uint32_t)next_duty_cycle + (uint32_t)step;
        next_duty_cycle =
            (increased_duty_cycle > MPPT_MAXIMUM_DUTY_CYCLE)
            ? MPPT_MAXIMUM_DUTY_CYCLE
            : (uint16_t)increased_duty_cycle;
    }
    else if (rail_voltage_mv > (uint16_t)(target_voltage_mv + VOLTAGE_REGULATION_DEADBAND_MV))
    {
        if (next_duty_cycle <= (uint16_t)(MPPT_MINIMUM_DUTY_CYCLE + step))
        {
            next_duty_cycle = MPPT_MINIMUM_DUTY_CYCLE;
        }
        else
        {
            next_duty_cycle = (uint16_t)(next_duty_cycle - step);
        }
    }

    return clamp_demo_duty_cycle_to_mppt_range(next_duty_cycle);
}

static uint16_t clamp_demo_duty_cycle_to_mppt_range(uint16_t duty_cycle)
{
    if (duty_cycle == 0u)
    {
        return 0u;
    }

    if (duty_cycle < MPPT_MINIMUM_DUTY_CYCLE)
    {
        return MPPT_MINIMUM_DUTY_CYCLE;
    }

    if (duty_cycle > MPPT_MAXIMUM_DUTY_CYCLE)
    {
        return MPPT_MAXIMUM_DUTY_CYCLE;
    }

    return duty_cycle;
}

static void build_telemetry_payload(chips_parsed_frame_type *response_to_build,
                                    uint8_t response_status)
{
    uint16_t position = 0u;

    write_u8(response_to_build->payload_bytes, &position, response_status);
    write_u8(response_to_build->payload_bytes, &position,
             TELEMETRY_PAYLOAD_PROTOCOL_VERSION);
    write_u32_le(response_to_build->payload_bytes, &position,
                 millisecond_tick_timer_get_milliseconds_since_boot());
    write_u32_le(response_to_build->payload_bytes, &position,
                 dispatch_state.valid_frame_count);
    write_u32_le(response_to_build->payload_bytes, &position,
                 dispatch_state.crc_error_count);
    write_u32_le(response_to_build->payload_bytes, &position,
                 dispatch_state.frame_too_long_error_count);
    write_u32_le(response_to_build->payload_bytes, &position,
                 dispatch_state.command_execution_count);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.last_command_id);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.last_command_status);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.input_source);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.control_mode);
    write_u16_le(response_to_build->payload_bytes, &position,
                 dispatch_state.fixed_duty_cycle_as_fraction_of_65535);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.telemetry_stream_enabled);
    write_u16_le(response_to_build->payload_bytes, &position,
                 dispatch_state.telemetry_stream_period_ms);
    write_injected_frame_to_payload(response_to_build->payload_bytes,
                                    &position);
    write_control_snapshot_to_payload(response_to_build->payload_bytes,
                                      &position);

    response_to_build->payload_length_in_bytes = position;
}

static void build_ack_payload(chips_parsed_frame_type *response_to_build,
                              uint8_t response_status)
{
    response_to_build->payload_bytes[0] = response_status;
    response_to_build->payload_length_in_bytes = 1u;
}

static void handle_set_injected_sensor_frame(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes
        != EPS_DEMO_INJECTED_SENSOR_FRAME_PAYLOAD_LENGTH_IN_BYTES)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    const uint8_t *payload = received_command->payload_bytes;
    eps_demo_injected_sensor_frame_type candidate_frame;
    candidate_frame.panel_voltage_raw_adc = read_u16_le(&payload[0]);
    candidate_frame.panel_current_raw_adc = read_u16_le(&payload[2]);
    candidate_frame.battery_voltage_mv = read_u16_le(&payload[4]);
    candidate_frame.battery_current_ma = read_i16_le(&payload[6]);
    candidate_frame.charging_rail_voltage_mv = read_u16_le(&payload[8]);
    candidate_frame.battery_temperature_decicelsius = read_i16_le(&payload[10]);
    candidate_frame.obc_heartbeat_present = payload[12];
    candidate_frame.satellite_mode_commanded_by_obc = payload[13];
    candidate_frame.safe_mode_sub_state_commanded_by_obc = payload[14];
    candidate_frame.fault_flags = read_u16_le(&payload[15]);

    if (injected_adc_values_are_in_range(&candidate_frame) == 0u)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    dispatch_state.injected_inputs = candidate_frame;

    uint16_t position = 0u;
    write_u8(response_to_build->payload_bytes, &position,
             (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
    write_injected_frame_to_payload(response_to_build->payload_bytes,
                                    &position);
    response_to_build->payload_length_in_bytes = position;
}

static void handle_get_telemetry(
    chips_parsed_frame_type *response_to_build)
{
    build_telemetry_payload(response_to_build,
                            (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
}

static void handle_get_state(
    chips_parsed_frame_type *response_to_build)
{
    uint16_t position = 0u;

    write_u8(response_to_build->payload_bytes, &position,
             (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.input_source);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.control_mode);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.injected_inputs.satellite_mode_commanded_by_obc);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.injected_inputs.safe_mode_sub_state_commanded_by_obc);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.control_snapshot.pcu_mode);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.control_snapshot.safe_mode_active);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.control_snapshot.safe_mode_reason);
    write_u16_le(response_to_build->payload_bytes, &position,
                 dispatch_state.control_snapshot.applied_pwm_duty_cycle_as_fraction_of_65535);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.control_snapshot.pwm_output_enabled);
    write_u8(response_to_build->payload_bytes, &position,
             dispatch_state.control_snapshot.load_enable_mask);

    response_to_build->payload_length_in_bytes = position;
}

static void handle_set_input_source(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes != 1u)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    uint8_t requested_source = received_command->payload_bytes[0];
    if ((requested_source != INPUT_SOURCE_INJECTED_VALUES)
        && (requested_source != INPUT_SOURCE_REAL_SENSORS))
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    dispatch_state.input_source = requested_source;
    build_ack_payload(response_to_build,
                      (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
}

static void handle_set_control_mode(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes != 1u)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    uint8_t requested_mode = received_command->payload_bytes[0];
    if (requested_mode > CONTROL_MODE_FULL_STATE_MACHINE)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    dispatch_state.control_mode = requested_mode;
    reset_demo_control_state_for_new_mode();
    build_ack_payload(response_to_build,
                      (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
}

static void handle_set_fixed_duty(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes != 2u)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    dispatch_state.fixed_duty_cycle_as_fraction_of_65535 =
        read_u16_le(received_command->payload_bytes);
    build_ack_payload(response_to_build,
                      (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
}

static void handle_set_telemetry_stream(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes != 3u)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    uint8_t requested_enabled = received_command->payload_bytes[0];
    uint16_t requested_period_ms =
        read_u16_le(&received_command->payload_bytes[1]);

    if ((requested_enabled > 1u)
        || (requested_period_ms < MINIMUM_TELEMETRY_STREAM_PERIOD_MS)
        || (requested_period_ms > MAXIMUM_TELEMETRY_STREAM_PERIOD_MS))
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    dispatch_state.telemetry_stream_enabled = requested_enabled;
    dispatch_state.telemetry_stream_period_ms = requested_period_ms;
    dispatch_state.last_telemetry_stream_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();

    response_to_build->payload_bytes[0] =
        (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS;
    response_to_build->payload_bytes[1] =
        dispatch_state.telemetry_stream_enabled;
    uint16_t position = 2u;
    write_u16_le(response_to_build->payload_bytes,
                 &position,
                 dispatch_state.telemetry_stream_period_ms);
    response_to_build->payload_length_in_bytes = position;
}

static void handle_set_demo_timing(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes != 6u)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

    uint16_t requested_mppt_timeout_iterations =
        read_u16_le(&received_command->payload_bytes[0]);
    uint16_t requested_cv_timeout_iterations =
        read_u16_le(&received_command->payload_bytes[2]);
    uint16_t requested_heartbeat_timeout_iterations =
        read_u16_le(&received_command->payload_bytes[4]);

    if ((requested_mppt_timeout_iterations < MINIMUM_DEMO_TIMEOUT_ITERATIONS)
        || (requested_mppt_timeout_iterations > MAXIMUM_DEMO_TIMEOUT_ITERATIONS)
        || (requested_cv_timeout_iterations < MINIMUM_DEMO_TIMEOUT_ITERATIONS)
        || (requested_cv_timeout_iterations > MAXIMUM_DEMO_TIMEOUT_ITERATIONS)
        || (requested_heartbeat_timeout_iterations
            < MINIMUM_DEMO_TIMEOUT_ITERATIONS)
        || (requested_heartbeat_timeout_iterations
            > MAXIMUM_DEMO_TIMEOUT_ITERATIONS))
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_PARAMETER_OUT_OF_RANGE);
        return;
    }

    dispatch_state.eps_configuration_thresholds.mppt_charge_timeout_for_insufficient_buffer_in_iterations =
        requested_mppt_timeout_iterations;
    dispatch_state.eps_configuration_thresholds.cv_float_low_voltage_wait_timeout_in_iterations =
        requested_cv_timeout_iterations;
    dispatch_state.eps_configuration_thresholds.obc_heartbeat_timeout_in_iterations =
        requested_heartbeat_timeout_iterations;

    uint16_t position = 0u;
    write_u8(response_to_build->payload_bytes, &position,
             (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
    write_u16_le(response_to_build->payload_bytes, &position,
                 requested_mppt_timeout_iterations);
    write_u16_le(response_to_build->payload_bytes, &position,
                 requested_cv_timeout_iterations);
    write_u16_le(response_to_build->payload_bytes, &position,
                 requested_heartbeat_timeout_iterations);
    response_to_build->payload_length_in_bytes = position;
}

static void handle_get_mainboard_adc(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_build)
{
    if (received_command->payload_length_in_bytes != 0u)
    {
        build_ack_payload(response_to_build,
            (uint8_t)CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD_LENGTH);
        return;
    }

#ifdef __SAMD21J17D__
    mainboard_adc_readings_type readings;
    mainboard_adc_reader_read_all_channels(&readings);

    uint16_t position = 0u;
    write_u8(response_to_build->payload_bytes, &position,
             (uint8_t)CHIPS_RESPONSE_STATUS_SUCCESS);
    write_u16_le(response_to_build->payload_bytes, &position,
                 readings.pv_imon_raw_adc);
    write_u16_le(response_to_build->payload_bytes, &position,
                 readings.bat_imon_raw_adc);
    write_u16_le(response_to_build->payload_bytes, &position,
                 readings.outa1_raw_adc);
    write_u16_le(response_to_build->payload_bytes, &position,
                 readings.outa2_raw_adc);
    write_u16_le(response_to_build->payload_bytes, &position,
                 readings.outv1_raw_adc);
    write_u16_le(response_to_build->payload_bytes, &position,
                 readings.outv2_raw_adc);
    response_to_build->payload_length_in_bytes = position;
#else
    build_ack_payload(response_to_build,
                      (uint8_t)CHIPS_RESPONSE_STATUS_COMMAND_NOT_AVAILABLE);
#endif
}

static void build_unknown_command_response(
    chips_parsed_frame_type *response_to_build)
{
    build_ack_payload(response_to_build,
                      (uint8_t)CHIPS_RESPONSE_STATUS_UNKNOWN_COMMAND);
}

static void send_response_frame_and_update_cache(
    const chips_parsed_frame_type *received_command,
    chips_parsed_frame_type *response_to_send)
{
    uint16_t wire_length =
        chips_build_stuffed_frame_with_sync_and_crc_into_buffer(
            response_to_send,
            dispatch_state.cached_response_wire_format_bytes,
            CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES);

    SATELLITE_ASSERT(wire_length > 0u);

    dispatch_state.cached_response_total_length_in_bytes = wire_length;
    dispatch_state.last_processed_sequence_number =
        received_command->sequence_number;
    dispatch_state.has_processed_at_least_one_command = 1u;

    send_wire_format_response_over_obc_uart(
        dispatch_state.cached_response_wire_format_bytes,
        wire_length);
}

static void send_wire_format_response_over_obc_uart(
    const uint8_t *wire_bytes,
    uint16_t wire_length)
{
    SATELLITE_ASSERT(wire_bytes != (void *)0);
    SATELLITE_ASSERT(wire_length > 0u);
    uart_obc_send_bytes(wire_bytes, (uint32_t)wire_length);
}

static uint8_t injected_adc_values_are_in_range(
    const eps_demo_injected_sensor_frame_type *candidate_frame)
{
    SATELLITE_ASSERT(candidate_frame != (void *)0);

    if (candidate_frame->panel_voltage_raw_adc > 4095u)
    {
        return 0u;
    }

    if (candidate_frame->panel_current_raw_adc > 4095u)
    {
        return 0u;
    }

    return 1u;
}
