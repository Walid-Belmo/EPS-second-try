/*
 * 02_chips_injection_demo.ino
 * ESP32 bridge and command console for the SAMD21 EPS demo firmware.
 *
 * Wiring:
 *   ESP32 GPIO17 TX2 -----> SAMD21 PA11 RX
 *   ESP32 GPIO16 RX2 <----- SAMD21 PA10 TX
 *   ESP32 GND ------------ SAMD21 GND
 *
 * Open the ESP32 USB serial monitor at 115200 baud and type "help".
 */

#include <Arduino.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>

#define ESP32_UART_RX_PIN                         16
#define ESP32_UART_TX_PIN                         17
#define USB_SERIAL_BAUD                           115200
#define SAMD21_UART_BAUD                          115200

#define CHIPS_FRAME_SYNC_BYTE                     0x7Eu
#define CHIPS_FRAME_ESCAPE_BYTE                   0x7Du
#define CHIPS_ESCAPE_XOR_VALUE                    0x20u
#define CHIPS_RESPONSE_FLAG_BIT_POSITION          7u
#define CHIPS_RESPONSE_FLAG_BIT_MASK              0x80u
#define CHIPS_COMMAND_ID_BIT_MASK                 0x7Fu
#define CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES       256u
#define CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES 260u
#define CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES 522u
#define CHIPS_MINIMUM_FRAME_CONTENT_SIZE_IN_BYTES 4u

#define CHIPS_COMMAND_ID_GET_TELEMETRY            0x01u
#define CHIPS_COMMAND_ID_GET_STATE                0x03u
#define CHIPS_COMMAND_ID_SET_MODE                 0x04u
#define CHIPS_COMMAND_ID_SET_INJECTED_SENSOR_FRAME 0x20u
#define CHIPS_COMMAND_ID_SET_INPUT_SOURCE         0x21u
#define CHIPS_COMMAND_ID_SET_CONTROL_MODE         0x22u
#define CHIPS_COMMAND_ID_SET_FIXED_DUTY           0x23u
#define CHIPS_COMMAND_ID_GET_DEBUG_SNAPSHOT       0x24u
#define CHIPS_COMMAND_ID_SET_TELEMETRY_STREAM     0x25u

#define CHIPS_RESPONSE_STATUS_SUCCESS             0x00u
#define CHIPS_RESPONSE_STATUS_UNKNOWN_COMMAND     0x01u
#define CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD     0x02u
#define CHIPS_RESPONSE_STATUS_OUT_OF_RANGE        0x03u
#define CHIPS_RESPONSE_STATUS_NOT_AVAILABLE       0x04u

#define INJECTED_SENSOR_FRAME_LENGTH              17u
#define TELEMETRY_PAYLOAD_BASE_LENGTH             48u
#define TELEMETRY_PAYLOAD_V2_LENGTH               75u
#define COMMAND_LINE_BUFFER_LENGTH                160u
#define MAXIMUM_TOKENS_PER_COMMAND                14

#define CONTROL_MODE_OFF                          0u
#define CONTROL_MODE_MPPT                         3u

#define MPPT_SIMULATOR_PERIOD_MS                  250u
#define MPPT_SIMULATOR_BATTERY_VOLTAGE_MV         7400u
#define MPPT_SIMULATOR_OPEN_CIRCUIT_VOLTAGE_MV    18000u
#define MPPT_SIMULATOR_SHORT_CIRCUIT_CURRENT_MA   3000u
#define MPPT_SIMULATOR_EXPECTED_MPP_VOLTAGE_MV    10392u
#define MPPT_SIMULATOR_STARTING_DUTY              32768u
#define MPPT_SIMULATOR_TEMP_DECIC                 220
#define PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE         5u
#define PANEL_RAW_ADC_TO_MILLIAMPS_SCALE          2u

typedef struct {
    uint8_t sequence_number;
    uint8_t command_id;
    uint8_t response_flag;
    uint8_t payload_bytes[CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES];
    uint16_t payload_length_in_bytes;
} chips_frame_type;

typedef enum {
    PARSER_STATE_WAITING_FOR_SYNC_BYTE = 0,
    PARSER_STATE_COLLECTING_FRAME_DATA = 1,
    PARSER_STATE_PROCESSING_ESCAPE_BYTE = 2
} parser_state_type;

typedef enum {
    PARSER_RESULT_INCOMPLETE = 0,
    PARSER_RESULT_FRAME_READY = 1,
    PARSER_RESULT_ERROR_CRC_MISMATCH = 2,
    PARSER_RESULT_ERROR_FRAME_TOO_LONG = 3
} parser_result_type;

typedef struct {
    parser_state_type current_state;
    uint8_t accumulation_buffer[CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES];
    uint16_t accumulation_length;
} chips_parser_type;

typedef struct {
    bool valid;
    uint8_t status;
    uint8_t version;
    uint8_t input_source;
    uint8_t control_mode;
    uint16_t battery_mv;
    uint32_t control_iterations;
    uint32_t input_power_mw;
    uint16_t panel_voltage_mv;
    uint16_t panel_current_ma;
    uint16_t mppt_duty;
    uint16_t fsm_duty;
    uint16_t applied_duty;
    uint8_t pcu_mode;
    uint8_t safe_active;
    uint8_t safe_reason;
    uint8_t panel_efuse;
    uint8_t heater;
    uint8_t safe_alert;
    uint8_t load_mask;
    uint8_t pwm_enabled;
    uint8_t last_control_mode;
} telemetry_snapshot_type;

static chips_parser_type samd21_parser;
static uint8_t next_sequence_number = 1u;
static char command_line_buffer[COMMAND_LINE_BUFFER_LENGTH];
static uint16_t command_line_length = 0u;
static telemetry_snapshot_type latest_telemetry;
static bool mppt_simulator_enabled = false;
static bool mppt_simulator_next_telemetry_is_internal = false;
static bool suppress_next_transmit_log = false;
static uint8_t mppt_simulator_internal_response_count_to_suppress = 0u;
static uint32_t mppt_simulator_last_step_ms = 0u;

static void print_help(void);
static void process_usb_serial_input(void);
static void process_complete_command_line(char *line);
static void process_samd21_uart_input(void);
static void run_mppt_simulator_if_due(void);
static void start_mppt_simulator(void);
static void stop_mppt_simulator(void);
static void print_mppt_simulator_status(void);
static void request_internal_mppt_simulator_telemetry(void);
static void inject_next_mppt_simulator_operating_point(void);
static void inject_mppt_simulator_operating_point_from_duty(uint16_t duty);
static void print_mppt_simulator_observation(void);
static bool update_latest_telemetry_snapshot_from_payload(
    const uint8_t *payload,
    uint16_t payload_length);
static uint16_t select_mppt_simulator_duty_for_model(void);
static uint32_t compute_simulated_panel_voltage_mv_from_duty(uint16_t duty);
static uint32_t compute_simulated_panel_current_ma(uint32_t panel_voltage_mv);
static uint32_t compute_power_mw(uint32_t voltage_mv, uint32_t current_ma);
static uint16_t clamp_to_u16_adc_count(uint32_t value);
static uint16_t expected_mppt_simulator_duty(void);
static void send_simple_command(uint8_t command_id);
static void send_one_byte_command(uint8_t command_id, uint8_t value);
static void send_set_fixed_duty_command(uint16_t duty_fraction);
static void send_set_telemetry_stream_command(uint8_t enabled,
                                              uint16_t period_ms);
static void send_injected_sensor_frame(uint16_t panel_voltage_raw_adc,
                                       uint16_t panel_current_raw_adc,
                                       uint16_t battery_voltage_mv,
                                       int16_t battery_current_ma,
                                       uint16_t charging_rail_voltage_mv,
                                       int16_t battery_temperature_decicelsius,
                                       uint8_t obc_heartbeat_present,
                                       uint8_t satellite_mode,
                                       uint8_t safe_mode_sub_state,
                                       uint16_t fault_flags);
static void send_chips_command(uint8_t command_id,
                               const uint8_t *payload_bytes,
                               uint16_t payload_length);
static void print_received_frame(const chips_frame_type *frame);
static void print_telemetry_payload(const uint8_t *payload,
                                    uint16_t payload_length);
static void print_state_payload(const uint8_t *payload,
                                uint16_t payload_length);
static void print_set_injected_response(const uint8_t *payload,
                                        uint16_t payload_length);
static uint16_t build_chips_wire_frame(const chips_frame_type *frame,
                                       uint8_t *wire_buffer,
                                       uint16_t wire_buffer_length);
static uint16_t write_one_stuffed_byte(uint8_t byte_to_write,
                                       uint8_t *wire_buffer,
                                       uint16_t current_position,
                                       uint16_t wire_buffer_length);
static parser_result_type parse_one_chips_byte(chips_parser_type *parser,
                                               uint8_t received_byte,
                                               chips_frame_type *output_frame);
static parser_result_type validate_and_extract_frame(chips_parser_type *parser,
                                                     chips_frame_type *output_frame);
static uint16_t compute_crc16_kermit(const uint8_t *bytes, uint16_t length);
static void write_u16_le(uint8_t *buffer, uint16_t *position, uint16_t value);
static uint16_t read_u16_le(const uint8_t *buffer);
static int16_t read_i16_le(const uint8_t *buffer);
static uint32_t read_u32_le(const uint8_t *buffer);
static int split_tokens(char *line, char **tokens, int maximum_tokens);
static bool parse_u32_token(const char *token, uint32_t minimum_value,
                            uint32_t maximum_value, uint32_t *output_value);
static bool parse_i32_token(const char *token, int32_t minimum_value,
                            int32_t maximum_value, int32_t *output_value);
static bool token_equals_ignore_case(const char *a, const char *b);
static bool parse_source_token(const char *token, uint8_t *source);
static bool parse_mode_token(const char *token, uint8_t *mode);
static const char *command_name(uint8_t command_id);
static const char *status_name(uint8_t status);
static const char *source_name(uint8_t source);
static const char *mode_name(uint8_t mode);
static const char *pcu_mode_name(uint8_t pcu_mode);

void setup()
{
    Serial.begin(USB_SERIAL_BAUD);
    while (!Serial) {
    }

    Serial2.begin(SAMD21_UART_BAUD, SERIAL_8N1,
                  ESP32_UART_RX_PIN, ESP32_UART_TX_PIN);

    samd21_parser.current_state = PARSER_STATE_WAITING_FOR_SYNC_BYTE;
    samd21_parser.accumulation_length = 0u;

    Serial.println();
    Serial.println("=== ESP32 CHIPS Injection Demo ===");
    Serial.print("USB serial: ");
    Serial.println(USB_SERIAL_BAUD);
    Serial.print("SAMD21 UART: RX GPIO");
    Serial.print(ESP32_UART_RX_PIN);
    Serial.print(", TX GPIO");
    Serial.print(ESP32_UART_TX_PIN);
    Serial.println(", 115200 baud");

    uint8_t test_vector[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };
    uint16_t crc = compute_crc16_kermit(test_vector, 9u);
    Serial.print("CRC self-test: 0x");
    Serial.println(crc, HEX);
    if (crc != 0x2189u) {
        Serial.println("CRC self-test FAILED");
    }

    print_help();
    send_simple_command(CHIPS_COMMAND_ID_GET_TELEMETRY);
}

void loop()
{
    process_usb_serial_input();
    process_samd21_uart_input();
    run_mppt_simulator_if_due();
}

static void print_help(void)
{
    Serial.println();
    Serial.println("Commands:");
    Serial.println("  telemetry");
    Serial.println("  debug");
    Serial.println("  state");
    Serial.println("  inject-default");
    Serial.println("  sunny");
    Serial.println("  lowbat");
    Serial.println("  fault");
    Serial.println("  inject pv_adc pi_adc batt_mv batt_ma rail_mv temp_decic heartbeat sat safe faults");
    Serial.println("  source injected|real");
    Serial.println("  mode off|fixed|voltage|mppt|fsm");
    Serial.println("  duty 0..65535");
    Serial.println("  sim-mppt on|off|status");
    Serial.println("  stream on [period_ms]");
    Serial.println("  stream off");
    Serial.println();
}

static void process_usb_serial_input(void)
{
    while (Serial.available() > 0) {
        char received_character = (char)Serial.read();

        if ((received_character == '\n') || (received_character == '\r')) {
            if (command_line_length > 0u) {
                command_line_buffer[command_line_length] = '\0';
                process_complete_command_line(command_line_buffer);
                command_line_length = 0u;
            }
        } else if (command_line_length < (COMMAND_LINE_BUFFER_LENGTH - 1u)) {
            command_line_buffer[command_line_length] = received_character;
            command_line_length += 1u;
        }
    }
}

static void process_complete_command_line(char *line)
{
    char *tokens[MAXIMUM_TOKENS_PER_COMMAND];
    int token_count = split_tokens(line, tokens, MAXIMUM_TOKENS_PER_COMMAND);

    if (token_count == 0) {
        return;
    }

    if (token_equals_ignore_case(tokens[0], "help")) {
        print_help();
        return;
    }

    if (token_equals_ignore_case(tokens[0], "telemetry")) {
        send_simple_command(CHIPS_COMMAND_ID_GET_TELEMETRY);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "debug")) {
        send_simple_command(CHIPS_COMMAND_ID_GET_DEBUG_SNAPSHOT);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "state")) {
        send_simple_command(CHIPS_COMMAND_ID_GET_STATE);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "inject-default")) {
        send_injected_sensor_frame(1800u, 1100u, 7400u, 250,
                                   7600u, 220, 1u, 1u, 1u, 0u);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "sunny")) {
        send_injected_sensor_frame(2600u, 1800u, 7800u, 500,
                                   8200u, 240, 1u, 1u, 1u, 0u);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "lowbat")) {
        send_injected_sensor_frame(1500u, 800u, 6400u, -100,
                                   6900u, 210, 1u, 1u, 1u, 0u);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "fault")) {
        send_injected_sensor_frame(1200u, 200u, 7200u, 0,
                                   0u, 260, 1u, 1u, 1u, 1u);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "inject")) {
        if (token_count != 11) {
            Serial.println("Usage: inject pv_adc pi_adc batt_mv batt_ma rail_mv temp_decic heartbeat sat safe faults");
            return;
        }

        uint32_t pv_adc = 0u;
        uint32_t pi_adc = 0u;
        uint32_t batt_mv = 0u;
        int32_t batt_ma = 0;
        uint32_t rail_mv = 0u;
        int32_t temp_decic = 0;
        uint32_t heartbeat = 0u;
        uint32_t sat = 0u;
        uint32_t safe = 0u;
        uint32_t faults = 0u;

        if (!parse_u32_token(tokens[1], 0u, 4095u, &pv_adc)
            || !parse_u32_token(tokens[2], 0u, 4095u, &pi_adc)
            || !parse_u32_token(tokens[3], 0u, 20000u, &batt_mv)
            || !parse_i32_token(tokens[4], -32768, 32767, &batt_ma)
            || !parse_u32_token(tokens[5], 0u, 20000u, &rail_mv)
            || !parse_i32_token(tokens[6], -32768, 32767, &temp_decic)
            || !parse_u32_token(tokens[7], 0u, 1u, &heartbeat)
            || !parse_u32_token(tokens[8], 0u, 255u, &sat)
            || !parse_u32_token(tokens[9], 0u, 255u, &safe)
            || !parse_u32_token(tokens[10], 0u, 65535u, &faults)) {
            Serial.println("Invalid inject arguments");
            return;
        }

        send_injected_sensor_frame((uint16_t)pv_adc, (uint16_t)pi_adc,
                                   (uint16_t)batt_mv, (int16_t)batt_ma,
                                   (uint16_t)rail_mv, (int16_t)temp_decic,
                                   (uint8_t)heartbeat, (uint8_t)sat,
                                   (uint8_t)safe, (uint16_t)faults);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "source")) {
        if (token_count != 2) {
            Serial.println("Usage: source injected|real");
            return;
        }

        uint8_t source = 0u;
        if (!parse_source_token(tokens[1], &source)) {
            Serial.println("Invalid source");
            return;
        }

        send_one_byte_command(CHIPS_COMMAND_ID_SET_INPUT_SOURCE, source);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "mode")) {
        if (token_count != 2) {
            Serial.println("Usage: mode off|fixed|voltage|mppt|fsm");
            return;
        }

        uint8_t mode = 0u;
        if (!parse_mode_token(tokens[1], &mode)) {
            Serial.println("Invalid mode");
            return;
        }

        send_one_byte_command(CHIPS_COMMAND_ID_SET_CONTROL_MODE, mode);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "duty")) {
        if (token_count != 2) {
            Serial.println("Usage: duty 0..65535");
            return;
        }

        uint32_t duty = 0u;
        if (!parse_u32_token(tokens[1], 0u, 65535u, &duty)) {
            Serial.println("Invalid duty");
            return;
        }

        send_set_fixed_duty_command((uint16_t)duty);
        return;
    }

    if (token_equals_ignore_case(tokens[0], "sim-mppt")) {
        if (token_count != 2) {
            Serial.println("Usage: sim-mppt on|off|status");
            return;
        }

        if (token_equals_ignore_case(tokens[1], "on")) {
            start_mppt_simulator();
            return;
        }

        if (token_equals_ignore_case(tokens[1], "off")) {
            stop_mppt_simulator();
            return;
        }

        if (token_equals_ignore_case(tokens[1], "status")) {
            print_mppt_simulator_status();
            return;
        }

        Serial.println("Usage: sim-mppt on|off|status");
        return;
    }

    if (token_equals_ignore_case(tokens[0], "stream")) {
        if (token_count < 2) {
            Serial.println("Usage: stream on [period_ms] | stream off");
            return;
        }

        if (token_equals_ignore_case(tokens[1], "on")) {
            uint16_t period_ms = 1000u;
            if (token_count >= 3) {
                uint32_t requested_period = 0u;
                if (!parse_u32_token(tokens[2], 100u, 10000u,
                                     &requested_period)) {
                    Serial.println("Invalid stream period");
                    return;
                }
                period_ms = (uint16_t)requested_period;
            }
            send_set_telemetry_stream_command(1u, period_ms);
            return;
        }

        if (token_equals_ignore_case(tokens[1], "off")) {
            send_set_telemetry_stream_command(0u, 1000u);
            return;
        }

        Serial.println("Usage: stream on [period_ms] | stream off");
        return;
    }

    Serial.println("Unknown command. Type help.");
}

static void process_samd21_uart_input(void)
{
    while (Serial2.available() > 0) {
        uint8_t received_byte = (uint8_t)Serial2.read();
        chips_frame_type parsed_frame;

        parser_result_type result =
            parse_one_chips_byte(&samd21_parser, received_byte,
                                 &parsed_frame);

        if (result == PARSER_RESULT_FRAME_READY) {
            print_received_frame(&parsed_frame);
        } else if (result == PARSER_RESULT_ERROR_CRC_MISMATCH) {
            Serial.println("[RX] CHIPS CRC error");
        } else if (result == PARSER_RESULT_ERROR_FRAME_TOO_LONG) {
            Serial.println("[RX] CHIPS frame too long");
        }
    }
}

static void run_mppt_simulator_if_due(void)
{
    if (!mppt_simulator_enabled) {
        return;
    }

    uint32_t now_ms = (uint32_t)millis();
    if ((now_ms - mppt_simulator_last_step_ms) < MPPT_SIMULATOR_PERIOD_MS) {
        return;
    }

    mppt_simulator_last_step_ms = now_ms;
    request_internal_mppt_simulator_telemetry();
}

static void start_mppt_simulator(void)
{
    mppt_simulator_enabled = true;
    latest_telemetry.valid = false;
    mppt_simulator_last_step_ms = (uint32_t)millis();

    Serial.println();
    Serial.println("SIM MPPT: enabled");
    Serial.print("SIM MPPT: IV curve Voc=");
    Serial.print(MPPT_SIMULATOR_OPEN_CIRCUIT_VOLTAGE_MV);
    Serial.print("mV Isc=");
    Serial.print(MPPT_SIMULATOR_SHORT_CIRCUIT_CURRENT_MA);
    Serial.print("mA Vmp~");
    Serial.print(MPPT_SIMULATOR_EXPECTED_MPP_VOLTAGE_MV);
    Serial.print("mV target_duty~");
    Serial.println(expected_mppt_simulator_duty());

    suppress_next_transmit_log = true;
    mppt_simulator_internal_response_count_to_suppress += 1u;
    send_one_byte_command(CHIPS_COMMAND_ID_SET_INPUT_SOURCE, 0u);

    suppress_next_transmit_log = true;
    mppt_simulator_internal_response_count_to_suppress += 1u;
    send_set_telemetry_stream_command(0u, 1000u);

    suppress_next_transmit_log = true;
    mppt_simulator_internal_response_count_to_suppress += 1u;
    send_one_byte_command(CHIPS_COMMAND_ID_SET_CONTROL_MODE, CONTROL_MODE_MPPT);

    inject_mppt_simulator_operating_point_from_duty(
        MPPT_SIMULATOR_STARTING_DUTY);
    request_internal_mppt_simulator_telemetry();
}

static void stop_mppt_simulator(void)
{
    mppt_simulator_enabled = false;
    mppt_simulator_next_telemetry_is_internal = false;
    suppress_next_transmit_log = true;
    mppt_simulator_internal_response_count_to_suppress += 1u;
    send_one_byte_command(CHIPS_COMMAND_ID_SET_CONTROL_MODE, CONTROL_MODE_OFF);
    Serial.println("SIM MPPT: disabled");
}

static void print_mppt_simulator_status(void)
{
    Serial.print("SIM MPPT: ");
    Serial.println(mppt_simulator_enabled ? "enabled" : "disabled");
    Serial.print("SIM MPPT: target_duty~");
    Serial.print(expected_mppt_simulator_duty());
    Serial.print(" period_ms=");
    Serial.println(MPPT_SIMULATOR_PERIOD_MS);

    if (latest_telemetry.valid) {
        print_mppt_simulator_observation();
    } else {
        Serial.println("SIM MPPT: no telemetry captured yet");
    }
}

static void request_internal_mppt_simulator_telemetry(void)
{
    mppt_simulator_next_telemetry_is_internal = true;
    suppress_next_transmit_log = true;
    send_simple_command(CHIPS_COMMAND_ID_GET_TELEMETRY);
}

static void inject_next_mppt_simulator_operating_point(void)
{
    if (!latest_telemetry.valid) {
        inject_mppt_simulator_operating_point_from_duty(
            MPPT_SIMULATOR_STARTING_DUTY);
        return;
    }

    if (latest_telemetry.control_mode != CONTROL_MODE_MPPT) {
        Serial.print("SIM MPPT: SAMD mode is ");
        Serial.print(mode_name(latest_telemetry.control_mode));
        Serial.println(", requesting mppt mode again");
        suppress_next_transmit_log = true;
        mppt_simulator_internal_response_count_to_suppress += 1u;
        send_one_byte_command(CHIPS_COMMAND_ID_SET_CONTROL_MODE,
                              CONTROL_MODE_MPPT);
        return;
    }

    inject_mppt_simulator_operating_point_from_duty(
        select_mppt_simulator_duty_for_model());
}

static void inject_mppt_simulator_operating_point_from_duty(uint16_t duty)
{
    uint32_t panel_voltage_mv =
        compute_simulated_panel_voltage_mv_from_duty(duty);
    uint32_t panel_current_ma =
        compute_simulated_panel_current_ma(panel_voltage_mv);
    uint32_t panel_power_mw =
        compute_power_mw(panel_voltage_mv, panel_current_ma);
    uint32_t battery_current_ma =
        (panel_power_mw * 1000u) / MPPT_SIMULATOR_BATTERY_VOLTAGE_MV;

    if (battery_current_ma > 32767u) {
        battery_current_ma = 32767u;
    }

    uint16_t panel_voltage_adc = clamp_to_u16_adc_count(
        (panel_voltage_mv + (PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE / 2u))
        / PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE);
    uint16_t panel_current_adc = clamp_to_u16_adc_count(
        (panel_current_ma + (PANEL_RAW_ADC_TO_MILLIAMPS_SCALE / 2u))
        / PANEL_RAW_ADC_TO_MILLIAMPS_SCALE);

    suppress_next_transmit_log = true;
    mppt_simulator_internal_response_count_to_suppress += 1u;
    send_injected_sensor_frame(panel_voltage_adc,
                               panel_current_adc,
                               MPPT_SIMULATOR_BATTERY_VOLTAGE_MV,
                               (int16_t)battery_current_ma,
                               MPPT_SIMULATOR_BATTERY_VOLTAGE_MV,
                               MPPT_SIMULATOR_TEMP_DECIC,
                               1u,
                               1u,
                               1u,
                               0u);
}

static void print_mppt_simulator_observation(void)
{
    int32_t duty_error =
        (int32_t)latest_telemetry.applied_duty
        - (int32_t)expected_mppt_simulator_duty();

    Serial.print("SIM MPPT: iter=");
    Serial.print(latest_telemetry.control_iterations);
    Serial.print(" duty=");
    Serial.print(latest_telemetry.applied_duty);
    Serial.print(" target=");
    Serial.print(expected_mppt_simulator_duty());
    Serial.print(" err=");
    Serial.print(duty_error);
    Serial.print(" V=");
    Serial.print(latest_telemetry.panel_voltage_mv);
    Serial.print("mV I=");
    Serial.print(latest_telemetry.panel_current_ma);
    Serial.print("mA P=");
    Serial.print(latest_telemetry.input_power_mw);
    Serial.print("mW pwm=");
    Serial.print(latest_telemetry.pwm_enabled);
    Serial.print(" mode=");
    Serial.println(mode_name(latest_telemetry.control_mode));
}

static bool update_latest_telemetry_snapshot_from_payload(
    const uint8_t *payload,
    uint16_t payload_length)
{
    if (payload_length < TELEMETRY_PAYLOAD_V2_LENGTH) {
        latest_telemetry.valid = false;
        return false;
    }

    latest_telemetry.status = payload[0];
    latest_telemetry.version = payload[1];
    latest_telemetry.input_source = payload[24];
    latest_telemetry.control_mode = payload[25];
    latest_telemetry.battery_mv = read_u16_le(&payload[35]);
    latest_telemetry.control_iterations = read_u32_le(&payload[48]);
    latest_telemetry.input_power_mw = read_u32_le(&payload[52]);
    latest_telemetry.panel_voltage_mv = read_u16_le(&payload[56]);
    latest_telemetry.panel_current_ma = read_u16_le(&payload[58]);
    latest_telemetry.mppt_duty = read_u16_le(&payload[60]);
    latest_telemetry.fsm_duty = read_u16_le(&payload[62]);
    latest_telemetry.applied_duty = read_u16_le(&payload[64]);
    latest_telemetry.pcu_mode = payload[66];
    latest_telemetry.safe_active = payload[67];
    latest_telemetry.safe_reason = payload[68];
    latest_telemetry.panel_efuse = payload[69];
    latest_telemetry.heater = payload[70];
    latest_telemetry.safe_alert = payload[71];
    latest_telemetry.load_mask = payload[72];
    latest_telemetry.pwm_enabled = payload[73];
    latest_telemetry.last_control_mode = payload[74];
    latest_telemetry.valid =
        (latest_telemetry.status == CHIPS_RESPONSE_STATUS_SUCCESS);

    return latest_telemetry.valid;
}

static uint16_t select_mppt_simulator_duty_for_model(void)
{
    if (latest_telemetry.applied_duty > 0u) {
        return latest_telemetry.applied_duty;
    }

    if (latest_telemetry.mppt_duty > 0u) {
        return latest_telemetry.mppt_duty;
    }

    return MPPT_SIMULATOR_STARTING_DUTY;
}

static uint32_t compute_simulated_panel_voltage_mv_from_duty(uint16_t duty)
{
    uint16_t duty_for_model = duty;
    if (duty_for_model == 0u) {
        duty_for_model = MPPT_SIMULATOR_STARTING_DUTY;
    }

    uint32_t panel_voltage_mv =
        ((uint32_t)MPPT_SIMULATOR_BATTERY_VOLTAGE_MV * 65535u)
        / (uint32_t)duty_for_model;

    if (panel_voltage_mv > MPPT_SIMULATOR_OPEN_CIRCUIT_VOLTAGE_MV) {
        panel_voltage_mv = MPPT_SIMULATOR_OPEN_CIRCUIT_VOLTAGE_MV;
    }

    return panel_voltage_mv;
}

static uint32_t compute_simulated_panel_current_ma(uint32_t panel_voltage_mv)
{
    if (panel_voltage_mv >= MPPT_SIMULATOR_OPEN_CIRCUIT_VOLTAGE_MV) {
        return 0u;
    }

    uint64_t voc_squared =
        (uint64_t)MPPT_SIMULATOR_OPEN_CIRCUIT_VOLTAGE_MV
        * (uint64_t)MPPT_SIMULATOR_OPEN_CIRCUIT_VOLTAGE_MV;
    uint64_t voltage_squared =
        (uint64_t)panel_voltage_mv * (uint64_t)panel_voltage_mv;
    uint64_t current_ma =
        ((uint64_t)MPPT_SIMULATOR_SHORT_CIRCUIT_CURRENT_MA
         * (voc_squared - voltage_squared))
        / voc_squared;

    return (uint32_t)current_ma;
}

static uint32_t compute_power_mw(uint32_t voltage_mv, uint32_t current_ma)
{
    return (uint32_t)(((uint64_t)voltage_mv * (uint64_t)current_ma)
                      / 1000u);
}

static uint16_t clamp_to_u16_adc_count(uint32_t value)
{
    if (value > 4095u) {
        return 4095u;
    }

    return (uint16_t)value;
}

static uint16_t expected_mppt_simulator_duty(void)
{
    return (uint16_t)(
        ((uint32_t)MPPT_SIMULATOR_BATTERY_VOLTAGE_MV * 65535u)
        / (uint32_t)MPPT_SIMULATOR_EXPECTED_MPP_VOLTAGE_MV);
}

static void send_simple_command(uint8_t command_id)
{
    send_chips_command(command_id, NULL, 0u);
}

static void send_one_byte_command(uint8_t command_id, uint8_t value)
{
    uint8_t payload[1];
    payload[0] = value;
    send_chips_command(command_id, payload, 1u);
}

static void send_set_fixed_duty_command(uint16_t duty_fraction)
{
    uint8_t payload[2];
    uint16_t position = 0u;
    write_u16_le(payload, &position, duty_fraction);
    send_chips_command(CHIPS_COMMAND_ID_SET_FIXED_DUTY, payload, position);
}

static void send_set_telemetry_stream_command(uint8_t enabled,
                                              uint16_t period_ms)
{
    uint8_t payload[3];
    uint16_t position = 0u;
    payload[position] = enabled;
    position += 1u;
    write_u16_le(payload, &position, period_ms);
    send_chips_command(CHIPS_COMMAND_ID_SET_TELEMETRY_STREAM,
                       payload, position);
}

static void send_injected_sensor_frame(uint16_t panel_voltage_raw_adc,
                                       uint16_t panel_current_raw_adc,
                                       uint16_t battery_voltage_mv,
                                       int16_t battery_current_ma,
                                       uint16_t charging_rail_voltage_mv,
                                       int16_t battery_temperature_decicelsius,
                                       uint8_t obc_heartbeat_present,
                                       uint8_t satellite_mode,
                                       uint8_t safe_mode_sub_state,
                                       uint16_t fault_flags)
{
    uint8_t payload[INJECTED_SENSOR_FRAME_LENGTH];
    uint16_t position = 0u;

    write_u16_le(payload, &position, panel_voltage_raw_adc);
    write_u16_le(payload, &position, panel_current_raw_adc);
    write_u16_le(payload, &position, battery_voltage_mv);
    write_u16_le(payload, &position, (uint16_t)battery_current_ma);
    write_u16_le(payload, &position, charging_rail_voltage_mv);
    write_u16_le(payload, &position,
                 (uint16_t)battery_temperature_decicelsius);
    payload[position] = obc_heartbeat_present;
    position += 1u;
    payload[position] = satellite_mode;
    position += 1u;
    payload[position] = safe_mode_sub_state;
    position += 1u;
    write_u16_le(payload, &position, fault_flags);

    send_chips_command(CHIPS_COMMAND_ID_SET_INJECTED_SENSOR_FRAME,
                       payload, position);
}

static void send_chips_command(uint8_t command_id,
                               const uint8_t *payload_bytes,
                               uint16_t payload_length)
{
    chips_frame_type command_frame;
    command_frame.sequence_number = next_sequence_number;
    next_sequence_number += 1u;
    command_frame.command_id = command_id;
    command_frame.response_flag = 0u;
    command_frame.payload_length_in_bytes = payload_length;

    for (uint16_t i = 0u; i < payload_length; i += 1u) {
        command_frame.payload_bytes[i] = payload_bytes[i];
    }

    uint8_t wire_buffer[CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES];
    uint16_t wire_length =
        build_chips_wire_frame(&command_frame, wire_buffer,
                               CHIPS_MAXIMUM_STUFFED_FRAME_SIZE_IN_BYTES);

    if (wire_length == 0u) {
        Serial.println("[TX] Failed to build CHIPS frame");
        return;
    }

    Serial2.write(wire_buffer, wire_length);
    if (suppress_next_transmit_log) {
        suppress_next_transmit_log = false;
        return;
    }

    Serial.print("[TX] seq=");
    Serial.print(command_frame.sequence_number);
    Serial.print(" cmd=");
    Serial.print(command_name(command_id));
    Serial.print(" payload_len=");
    Serial.println(payload_length);
}

static void print_received_frame(const chips_frame_type *frame)
{
    if ((frame->command_id == CHIPS_COMMAND_ID_GET_TELEMETRY)
        && mppt_simulator_next_telemetry_is_internal) {
        mppt_simulator_next_telemetry_is_internal = false;

        if (update_latest_telemetry_snapshot_from_payload(
                frame->payload_bytes,
                frame->payload_length_in_bytes)) {
            print_mppt_simulator_observation();
            if (mppt_simulator_enabled) {
                inject_next_mppt_simulator_operating_point();
            }
        } else {
            Serial.println("SIM MPPT: telemetry v2 not available");
        }
        return;
    }

    if ((mppt_simulator_internal_response_count_to_suppress > 0u)
        && (frame->command_id != CHIPS_COMMAND_ID_GET_TELEMETRY)) {
        mppt_simulator_internal_response_count_to_suppress -= 1u;
        if ((frame->payload_length_in_bytes > 0u)
            && (frame->payload_bytes[0] != CHIPS_RESPONSE_STATUS_SUCCESS)) {
            Serial.print("SIM MPPT: internal command failed: ");
            Serial.print(command_name(frame->command_id));
            Serial.print(" status=");
            Serial.println(status_name(frame->payload_bytes[0]));
        }
        return;
    }

    Serial.print("[RX] seq=");
    Serial.print(frame->sequence_number);
    Serial.print(" cmd=");
    Serial.print(command_name(frame->command_id));
    Serial.print(" rsp=");
    Serial.print(frame->response_flag);
    Serial.print(" payload_len=");
    Serial.println(frame->payload_length_in_bytes);

    if (frame->payload_length_in_bytes > 0u) {
        uint8_t status = frame->payload_bytes[0];
        Serial.print("     status=");
        Serial.print(status);
        Serial.print(" (");
        Serial.print(status_name(status));
        Serial.println(")");
    }

    if ((frame->command_id == CHIPS_COMMAND_ID_GET_TELEMETRY)
        || (frame->command_id == CHIPS_COMMAND_ID_GET_DEBUG_SNAPSHOT)) {
        print_telemetry_payload(frame->payload_bytes,
                                frame->payload_length_in_bytes);
    } else if (frame->command_id == CHIPS_COMMAND_ID_GET_STATE) {
        print_state_payload(frame->payload_bytes,
                            frame->payload_length_in_bytes);
    } else if (frame->command_id
               == CHIPS_COMMAND_ID_SET_INJECTED_SENSOR_FRAME) {
        print_set_injected_response(frame->payload_bytes,
                                    frame->payload_length_in_bytes);
    }
}

static void print_telemetry_payload(const uint8_t *payload,
                                    uint16_t payload_length)
{
    if (payload_length < TELEMETRY_PAYLOAD_BASE_LENGTH) {
        Serial.println("     telemetry payload too short");
        return;
    }

    uint8_t status = payload[0];
    uint8_t version = payload[1];
    uint32_t uptime_ms = read_u32_le(&payload[2]);
    uint32_t valid_frames = read_u32_le(&payload[6]);
    uint32_t crc_errors = read_u32_le(&payload[10]);
    uint32_t too_long_errors = read_u32_le(&payload[14]);
    uint32_t command_count = read_u32_le(&payload[18]);
    uint8_t last_command = payload[22];
    uint8_t last_status = payload[23];
    uint8_t input_source = payload[24];
    uint8_t control_mode = payload[25];
    uint16_t fixed_duty = read_u16_le(&payload[26]);
    uint8_t stream_enabled = payload[28];
    uint16_t stream_period_ms = read_u16_le(&payload[29]);

    uint16_t panel_voltage_adc = read_u16_le(&payload[31]);
    uint16_t panel_current_adc = read_u16_le(&payload[33]);
    uint16_t battery_mv = read_u16_le(&payload[35]);
    int16_t battery_ma = read_i16_le(&payload[37]);
    uint16_t rail_mv = read_u16_le(&payload[39]);
    int16_t temp_decic = read_i16_le(&payload[41]);
    uint8_t heartbeat = payload[43];
    uint8_t satellite_mode = payload[44];
    uint8_t safe_substate = payload[45];
    uint16_t fault_flags = read_u16_le(&payload[46]);

    Serial.print("     version=");
    Serial.print(version);
    Serial.print(" uptime_ms=");
    Serial.print(uptime_ms);
    Serial.print(" status=");
    Serial.println(status_name(status));

    Serial.print("     frames=");
    Serial.print(valid_frames);
    Serial.print(" crc_errors=");
    Serial.print(crc_errors);
    Serial.print(" too_long=");
    Serial.print(too_long_errors);
    Serial.print(" commands=");
    Serial.println(command_count);

    Serial.print("     last_cmd=");
    Serial.print(command_name(last_command));
    Serial.print(" last_status=");
    Serial.println(status_name(last_status));

    Serial.print("     source=");
    Serial.print(source_name(input_source));
    Serial.print(" control=");
    Serial.print(mode_name(control_mode));
    Serial.print(" duty=");
    Serial.println(fixed_duty);

    Serial.print("     stream=");
    Serial.print(stream_enabled);
    Serial.print(" period_ms=");
    Serial.println(stream_period_ms);

    Serial.print("     injected pv_adc=");
    Serial.print(panel_voltage_adc);
    Serial.print(" pi_adc=");
    Serial.print(panel_current_adc);
    Serial.print(" batt_mv=");
    Serial.print(battery_mv);
    Serial.print(" batt_ma=");
    Serial.println((int)battery_ma);

    Serial.print("     injected rail_mv=");
    Serial.print(rail_mv);
    Serial.print(" temp_decic=");
    Serial.print((int)temp_decic);
    Serial.print(" heartbeat=");
    Serial.print(heartbeat);
    Serial.print(" sat=");
    Serial.print(satellite_mode);
    Serial.print(" safe=");
    Serial.print(safe_substate);
    Serial.print(" faults=0x");
    Serial.println(fault_flags, HEX);

    if (payload_length >= TELEMETRY_PAYLOAD_V2_LENGTH) {
        (void)update_latest_telemetry_snapshot_from_payload(payload,
                                                            payload_length);

        uint32_t control_iterations = read_u32_le(&payload[48]);
        uint32_t input_power_mw = read_u32_le(&payload[52]);
        uint16_t panel_voltage_mv = read_u16_le(&payload[56]);
        uint16_t panel_current_ma = read_u16_le(&payload[58]);
        uint16_t mppt_duty = read_u16_le(&payload[60]);
        uint16_t fsm_duty = read_u16_le(&payload[62]);
        uint16_t applied_duty = read_u16_le(&payload[64]);
        uint8_t pcu_mode = payload[66];
        uint8_t safe_active = payload[67];
        uint8_t safe_reason = payload[68];
        uint8_t panel_efuse = payload[69];
        uint8_t heater = payload[70];
        uint8_t safe_alert = payload[71];
        uint8_t load_mask = payload[72];
        uint8_t pwm_enabled = payload[73];
        uint8_t last_control_mode = payload[74];

        Serial.print("     control iter=");
        Serial.print(control_iterations);
        Serial.print(" panel_mv=");
        Serial.print(panel_voltage_mv);
        Serial.print(" panel_ma=");
        Serial.print(panel_current_ma);
        Serial.print(" input_mw=");
        Serial.println(input_power_mw);

        Serial.print("     duty mppt=");
        Serial.print(mppt_duty);
        Serial.print(" fsm=");
        Serial.print(fsm_duty);
        Serial.print(" applied=");
        Serial.print(applied_duty);
        Serial.print(" pwm=");
        Serial.println(pwm_enabled);

        Serial.print("     pcu=");
        Serial.print(pcu_mode_name(pcu_mode));
        Serial.print(" safe_active=");
        Serial.print(safe_active);
        Serial.print(" safe_reason=");
        Serial.print(safe_reason);
        Serial.print(" alert=");
        Serial.println(safe_alert);

        Serial.print("     panel_efuse=");
        Serial.print(panel_efuse);
        Serial.print(" heater=");
        Serial.print(heater);
        Serial.print(" loads=0x");
        Serial.print(load_mask, HEX);
        Serial.print(" last_control=");
        Serial.println(mode_name(last_control_mode));
    }
}

static void print_state_payload(const uint8_t *payload,
                                uint16_t payload_length)
{
    if (payload_length < 5u) {
        Serial.println("     state payload too short");
        return;
    }

    Serial.print("     source=");
    Serial.print(source_name(payload[1]));
    Serial.print(" control=");
    Serial.print(mode_name(payload[2]));
    Serial.print(" sat=");
    Serial.print(payload[3]);
    Serial.print(" safe=");
    Serial.print(payload[4]);

    if (payload_length >= 12u) {
        Serial.print(" pcu=");
        Serial.print(pcu_mode_name(payload[5]));
        Serial.print(" safe_active=");
        Serial.print(payload[6]);
        Serial.print(" reason=");
        Serial.print(payload[7]);
        Serial.print(" applied_duty=");
        Serial.print(read_u16_le(&payload[8]));
        Serial.print(" pwm=");
        Serial.print(payload[10]);
        Serial.print(" loads=0x");
        Serial.print(payload[11], HEX);
    }

    Serial.println();
}

static void print_set_injected_response(const uint8_t *payload,
                                        uint16_t payload_length)
{
    if (payload_length < (1u + INJECTED_SENSOR_FRAME_LENGTH)) {
        return;
    }

    Serial.print("     echoed pv_adc=");
    Serial.print(read_u16_le(&payload[1]));
    Serial.print(" pi_adc=");
    Serial.print(read_u16_le(&payload[3]));
    Serial.print(" batt_mv=");
    Serial.println(read_u16_le(&payload[5]));
}

static uint16_t build_chips_wire_frame(const chips_frame_type *frame,
                                       uint8_t *wire_buffer,
                                       uint16_t wire_buffer_length)
{
    if (frame->payload_length_in_bytes
        > CHIPS_MAXIMUM_PAYLOAD_SIZE_IN_BYTES) {
        return 0u;
    }

    uint8_t raw_content[CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES];
    uint16_t raw_length = 0u;
    raw_content[raw_length] = frame->sequence_number;
    raw_length += 1u;
    raw_content[raw_length] =
        (uint8_t)((frame->response_flag << CHIPS_RESPONSE_FLAG_BIT_POSITION)
                  | (frame->command_id & CHIPS_COMMAND_ID_BIT_MASK));
    raw_length += 1u;

    for (uint16_t i = 0u; i < frame->payload_length_in_bytes; i += 1u) {
        raw_content[raw_length] = frame->payload_bytes[i];
        raw_length += 1u;
    }

    uint16_t crc = compute_crc16_kermit(raw_content, raw_length);
    raw_content[raw_length] = (uint8_t)(crc & 0xFFu);
    raw_length += 1u;
    raw_content[raw_length] = (uint8_t)((crc >> 8u) & 0xFFu);
    raw_length += 1u;

    uint16_t output_position = 0u;
    wire_buffer[output_position] = CHIPS_FRAME_SYNC_BYTE;
    output_position += 1u;

    for (uint16_t i = 0u; i < raw_length; i += 1u) {
        output_position = write_one_stuffed_byte(raw_content[i],
                                                 wire_buffer,
                                                 output_position,
                                                 wire_buffer_length);
        if (output_position == 0u) {
            return 0u;
        }
    }

    if (output_position >= wire_buffer_length) {
        return 0u;
    }

    wire_buffer[output_position] = CHIPS_FRAME_SYNC_BYTE;
    output_position += 1u;
    return output_position;
}

static uint16_t write_one_stuffed_byte(uint8_t byte_to_write,
                                       uint8_t *wire_buffer,
                                       uint16_t current_position,
                                       uint16_t wire_buffer_length)
{
    if ((byte_to_write == CHIPS_FRAME_SYNC_BYTE)
        || (byte_to_write == CHIPS_FRAME_ESCAPE_BYTE)) {
        if ((current_position + 2u) > wire_buffer_length) {
            return 0u;
        }
        wire_buffer[current_position] = CHIPS_FRAME_ESCAPE_BYTE;
        wire_buffer[current_position + 1u] =
            (uint8_t)(byte_to_write ^ CHIPS_ESCAPE_XOR_VALUE);
        return (uint16_t)(current_position + 2u);
    }

    if ((current_position + 1u) > wire_buffer_length) {
        return 0u;
    }

    wire_buffer[current_position] = byte_to_write;
    return (uint16_t)(current_position + 1u);
}

static parser_result_type parse_one_chips_byte(chips_parser_type *parser,
                                               uint8_t received_byte,
                                               chips_frame_type *output_frame)
{
    if (parser->current_state == PARSER_STATE_WAITING_FOR_SYNC_BYTE) {
        if (received_byte == CHIPS_FRAME_SYNC_BYTE) {
            parser->current_state = PARSER_STATE_COLLECTING_FRAME_DATA;
            parser->accumulation_length = 0u;
        }
        return PARSER_RESULT_INCOMPLETE;
    }

    if (parser->current_state == PARSER_STATE_PROCESSING_ESCAPE_BYTE) {
        if (parser->accumulation_length
            >= CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES) {
            parser->current_state = PARSER_STATE_WAITING_FOR_SYNC_BYTE;
            parser->accumulation_length = 0u;
            return PARSER_RESULT_ERROR_FRAME_TOO_LONG;
        }

        parser->accumulation_buffer[parser->accumulation_length] =
            (uint8_t)(received_byte ^ CHIPS_ESCAPE_XOR_VALUE);
        parser->accumulation_length += 1u;
        parser->current_state = PARSER_STATE_COLLECTING_FRAME_DATA;
        return PARSER_RESULT_INCOMPLETE;
    }

    if (received_byte == CHIPS_FRAME_SYNC_BYTE) {
        if (parser->accumulation_length == 0u) {
            return PARSER_RESULT_INCOMPLETE;
        }

        if (parser->accumulation_length
            < CHIPS_MINIMUM_FRAME_CONTENT_SIZE_IN_BYTES) {
            parser->accumulation_length = 0u;
            return PARSER_RESULT_INCOMPLETE;
        }

        return validate_and_extract_frame(parser, output_frame);
    }

    if (received_byte == CHIPS_FRAME_ESCAPE_BYTE) {
        parser->current_state = PARSER_STATE_PROCESSING_ESCAPE_BYTE;
        return PARSER_RESULT_INCOMPLETE;
    }

    if (parser->accumulation_length
        >= CHIPS_MAXIMUM_FRAME_CONTENT_SIZE_IN_BYTES) {
        parser->current_state = PARSER_STATE_WAITING_FOR_SYNC_BYTE;
        parser->accumulation_length = 0u;
        return PARSER_RESULT_ERROR_FRAME_TOO_LONG;
    }

    parser->accumulation_buffer[parser->accumulation_length] = received_byte;
    parser->accumulation_length += 1u;
    return PARSER_RESULT_INCOMPLETE;
}

static parser_result_type validate_and_extract_frame(chips_parser_type *parser,
                                                     chips_frame_type *output_frame)
{
    uint16_t content_length = parser->accumulation_length;
    uint16_t data_length_without_crc = (uint16_t)(content_length - 2u);
    uint16_t received_crc =
        (uint16_t)parser->accumulation_buffer[content_length - 2u]
        | ((uint16_t)parser->accumulation_buffer[content_length - 1u] << 8u);
    uint16_t computed_crc =
        compute_crc16_kermit(parser->accumulation_buffer,
                             data_length_without_crc);

    parser->current_state = PARSER_STATE_COLLECTING_FRAME_DATA;
    parser->accumulation_length = 0u;

    if (computed_crc != received_crc) {
        return PARSER_RESULT_ERROR_CRC_MISMATCH;
    }

    output_frame->sequence_number = parser->accumulation_buffer[0];
    output_frame->response_flag =
        (parser->accumulation_buffer[1] & CHIPS_RESPONSE_FLAG_BIT_MASK)
        >> CHIPS_RESPONSE_FLAG_BIT_POSITION;
    output_frame->command_id =
        parser->accumulation_buffer[1] & CHIPS_COMMAND_ID_BIT_MASK;
    output_frame->payload_length_in_bytes = (uint16_t)(content_length - 4u);

    for (uint16_t i = 0u; i < output_frame->payload_length_in_bytes; i += 1u) {
        output_frame->payload_bytes[i] =
            parser->accumulation_buffer[2u + i];
    }

    return PARSER_RESULT_FRAME_READY;
}

static uint16_t compute_crc16_kermit(const uint8_t *bytes, uint16_t length)
{
    uint16_t crc = 0x0000u;

    for (uint16_t byte_index = 0u; byte_index < length; byte_index += 1u) {
        crc ^= bytes[byte_index];
        for (uint8_t bit_index = 0u; bit_index < 8u; bit_index += 1u) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((crc >> 1u) ^ 0x8408u);
            } else {
                crc = (uint16_t)(crc >> 1u);
            }
        }
    }

    return crc;
}

static void write_u16_le(uint8_t *buffer, uint16_t *position, uint16_t value)
{
    buffer[*position] = (uint8_t)(value & 0xFFu);
    *position = (uint16_t)(*position + 1u);
    buffer[*position] = (uint8_t)((value >> 8u) & 0xFFu);
    *position = (uint16_t)(*position + 1u);
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8u);
}

static int16_t read_i16_le(const uint8_t *buffer)
{
    return (int16_t)read_u16_le(buffer);
}

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0]
           | ((uint32_t)buffer[1] << 8u)
           | ((uint32_t)buffer[2] << 16u)
           | ((uint32_t)buffer[3] << 24u);
}

static int split_tokens(char *line, char **tokens, int maximum_tokens)
{
    int token_count = 0;
    char *cursor = line;

    while ((*cursor != '\0') && (token_count < maximum_tokens)) {
        while ((*cursor == ' ') || (*cursor == '\t')) {
            cursor += 1;
        }

        if (*cursor == '\0') {
            break;
        }

        tokens[token_count] = cursor;
        token_count += 1;

        while ((*cursor != '\0')
               && (*cursor != ' ')
               && (*cursor != '\t')) {
            cursor += 1;
        }

        if (*cursor != '\0') {
            *cursor = '\0';
            cursor += 1;
        }
    }

    return token_count;
}

static bool parse_u32_token(const char *token, uint32_t minimum_value,
                            uint32_t maximum_value, uint32_t *output_value)
{
    char *end_pointer = NULL;
    unsigned long value = strtoul(token, &end_pointer, 0);

    if ((end_pointer == token) || (*end_pointer != '\0')) {
        return false;
    }

    if ((value < minimum_value) || (value > maximum_value)) {
        return false;
    }

    *output_value = (uint32_t)value;
    return true;
}

static bool parse_i32_token(const char *token, int32_t minimum_value,
                            int32_t maximum_value, int32_t *output_value)
{
    char *end_pointer = NULL;
    long value = strtol(token, &end_pointer, 0);

    if ((end_pointer == token) || (*end_pointer != '\0')) {
        return false;
    }

    if ((value < minimum_value) || (value > maximum_value)) {
        return false;
    }

    *output_value = (int32_t)value;
    return true;
}

static bool token_equals_ignore_case(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a += 1;
        b += 1;
    }

    return (*a == '\0') && (*b == '\0');
}

static bool parse_source_token(const char *token, uint8_t *source)
{
    uint32_t numeric_source = 0u;
    if (parse_u32_token(token, 0u, 1u, &numeric_source)) {
        *source = (uint8_t)numeric_source;
        return true;
    }

    if (token_equals_ignore_case(token, "injected")) {
        *source = 0u;
        return true;
    }

    if (token_equals_ignore_case(token, "real")) {
        *source = 1u;
        return true;
    }

    return false;
}

static bool parse_mode_token(const char *token, uint8_t *mode)
{
    uint32_t numeric_mode = 0u;
    if (parse_u32_token(token, 0u, 4u, &numeric_mode)) {
        *mode = (uint8_t)numeric_mode;
        return true;
    }

    if (token_equals_ignore_case(token, "off")) {
        *mode = 0u;
        return true;
    }

    if (token_equals_ignore_case(token, "fixed")) {
        *mode = 1u;
        return true;
    }

    if (token_equals_ignore_case(token, "voltage")) {
        *mode = 2u;
        return true;
    }

    if (token_equals_ignore_case(token, "mppt")) {
        *mode = 3u;
        return true;
    }

    if (token_equals_ignore_case(token, "fsm")) {
        *mode = 4u;
        return true;
    }

    return false;
}

static const char *command_name(uint8_t command_id)
{
    switch (command_id) {
    case CHIPS_COMMAND_ID_GET_TELEMETRY:
        return "GET_TELEMETRY";
    case CHIPS_COMMAND_ID_GET_STATE:
        return "GET_STATE";
    case CHIPS_COMMAND_ID_SET_MODE:
        return "SET_MODE";
    case CHIPS_COMMAND_ID_SET_INJECTED_SENSOR_FRAME:
        return "SET_INJECTED_SENSOR_FRAME";
    case CHIPS_COMMAND_ID_SET_INPUT_SOURCE:
        return "SET_INPUT_SOURCE";
    case CHIPS_COMMAND_ID_SET_CONTROL_MODE:
        return "SET_CONTROL_MODE";
    case CHIPS_COMMAND_ID_SET_FIXED_DUTY:
        return "SET_FIXED_DUTY";
    case CHIPS_COMMAND_ID_GET_DEBUG_SNAPSHOT:
        return "GET_DEBUG_SNAPSHOT";
    case CHIPS_COMMAND_ID_SET_TELEMETRY_STREAM:
        return "SET_TELEMETRY_STREAM";
    default:
        return "UNKNOWN";
    }
}

static const char *status_name(uint8_t status)
{
    switch (status) {
    case CHIPS_RESPONSE_STATUS_SUCCESS:
        return "SUCCESS";
    case CHIPS_RESPONSE_STATUS_UNKNOWN_COMMAND:
        return "UNKNOWN_COMMAND";
    case CHIPS_RESPONSE_STATUS_INVALID_PAYLOAD:
        return "INVALID_PAYLOAD_LENGTH";
    case CHIPS_RESPONSE_STATUS_OUT_OF_RANGE:
        return "PARAMETER_OUT_OF_RANGE";
    case CHIPS_RESPONSE_STATUS_NOT_AVAILABLE:
        return "COMMAND_NOT_AVAILABLE";
    default:
        return "UNKNOWN_STATUS";
    }
}

static const char *source_name(uint8_t source)
{
    if (source == 0u) {
        return "injected";
    }

    if (source == 1u) {
        return "real";
    }

    return "unknown";
}

static const char *mode_name(uint8_t mode)
{
    switch (mode) {
    case 0u:
        return "off";
    case 1u:
        return "fixed";
    case 2u:
        return "voltage";
    case 3u:
        return "mppt";
    case 4u:
        return "fsm";
    default:
        return "unknown";
    }
}

static const char *pcu_mode_name(uint8_t pcu_mode)
{
    switch (pcu_mode) {
    case 0u:
        return "MPPT_CHARGE";
    case 1u:
        return "CV_FLOAT";
    case 2u:
        return "SA_LOAD_FOLLOW";
    case 3u:
        return "BATTERY_DISCHARGE";
    default:
        return "unknown";
    }
}
