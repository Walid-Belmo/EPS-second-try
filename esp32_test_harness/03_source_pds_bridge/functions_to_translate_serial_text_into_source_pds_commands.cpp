#include "functions_to_translate_serial_text_into_source_pds_commands.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "functions_to_send_and_receive_chips_frames.h"
#include "source_pds_command_contract.h"

#define MAXIMUM_TEXT_TOKENS 20

static uint8_t next_sequence_number = 1u;

static void send_off_command(Stream &usb_serial, Stream &board_uart);
static void send_run_pwm_command(char **tokens, int count, Stream &usb_serial, Stream &board_uart);
static void send_mppt_demo_command(char **tokens, int count, Stream &usb_serial, Stream &board_uart);
static void send_state_command(
    uint8_t command_id,
    char **tokens,
    int count,
    Stream &usb_serial,
    Stream &board_uart);
static void send_get_values_command(char **tokens, int count, Stream &usb_serial, Stream &board_uart);
static void send_stream_values_command(char **tokens, int count, Stream &usb_serial, Stream &board_uart);
static void transmit_command(
    uint8_t command_id,
    const uint8_t *payload,
    uint16_t payload_length,
    Stream &usb_serial,
    Stream &board_uart);
static int split_tokens(char *line, char **tokens, int maximum_tokens);
static const char *value_for_key(char **tokens, int count, const char *key);
static bool read_required_i32(char **tokens, int count, const char *key, int32_t *value, Stream &output);
static bool read_required_u16(char **tokens, int count, const char *key, uint16_t *value, Stream &output);
static bool read_required_i16(char **tokens, int count, const char *key, int16_t *value, Stream &output);
static bool read_required_double(char **tokens, int count, const char *key, double *value, Stream &output);
static bool write_state_payload(char **tokens, int count, uint8_t *payload, Stream &output);
static bool write_mppt_payload(char **tokens, int count, uint8_t *payload, Stream &output);
static bool parse_obc_mode(const char *text, uint8_t *mode);
static bool parse_safe_substate(const char *text, uint8_t *substate);
static uint32_t parse_field_mask(const char *field_text);
static void write_u8(uint8_t *payload, uint16_t *position, uint8_t value);
static void write_u16(uint8_t *payload, uint16_t *position, uint16_t value);
static void write_i16(uint8_t *payload, uint16_t *position, int16_t value);
static void write_i32(uint8_t *payload, uint16_t *position, int32_t value);

void print_source_pds_bridge_help(Stream &output)
{
    output.println();
    output.println("Source PDS ESP32 bridge commands:");
    output.println("  off");
    output.println("  run_pwm duty=32768");
    output.println("  start_mppt_demo curve=quadratic a=-12 b=300 c=2500 v_min=0 v_max=18000 battery_voltage=7400");
    output.println("  start_state_demo battery_voltage=7400 battery_current=0 panel_voltage=12000 panel_current=1000 rail_voltage=7400 temperature=220 heartbeat=1 obc_mode=charging safe_substate=charging faults=0");
    output.println("  inject_state battery_voltage=7200 battery_current=-300 panel_voltage=0 panel_current=0 rail_voltage=7200 temperature=220 heartbeat=1 obc_mode=charging safe_substate=charging faults=0");
    output.println("  get_values fields=all");
    output.println("  stream_values on period=200 fields=mode,pwm,state,loads,faults,mppt,inputs,commands");
    output.println("  stream_values off");
    output.println();
}

void execute_text_command_from_usb(
    char *line,
    Stream &usb_serial,
    Stream &board_uart)
{
    char *tokens[MAXIMUM_TEXT_TOKENS];
    int count = split_tokens(line, tokens, MAXIMUM_TEXT_TOKENS);
    if (count == 0)
    {
        return;
    }

    if ((strcmp(tokens[0], "help") == 0) || (strcmp(tokens[0], "?") == 0))
    {
        print_source_pds_bridge_help(usb_serial);
    }
    else if (strcmp(tokens[0], "off") == 0)
    {
        send_off_command(usb_serial, board_uart);
    }
    else if (strcmp(tokens[0], "run_pwm") == 0)
    {
        send_run_pwm_command(tokens, count, usb_serial, board_uart);
    }
    else if (strcmp(tokens[0], "start_mppt_demo") == 0)
    {
        send_mppt_demo_command(tokens, count, usb_serial, board_uart);
    }
    else if (strcmp(tokens[0], "start_state_demo") == 0)
    {
        send_state_command(PDS_BOARD_COMMAND_START_STATE_DEMO, tokens, count, usb_serial, board_uart);
    }
    else if (strcmp(tokens[0], "inject_state") == 0)
    {
        send_state_command(PDS_BOARD_COMMAND_INJECT_STATE_INPUTS, tokens, count, usb_serial, board_uart);
    }
    else if (strcmp(tokens[0], "get_values") == 0)
    {
        send_get_values_command(tokens, count, usb_serial, board_uart);
    }
    else if (strcmp(tokens[0], "stream_values") == 0)
    {
        send_stream_values_command(tokens, count, usb_serial, board_uart);
    }
    else
    {
        usb_serial.print("Unknown command: ");
        usb_serial.println(tokens[0]);
    }
}

static void send_off_command(Stream &usb_serial, Stream &board_uart)
{
    transmit_command(PDS_BOARD_COMMAND_OFF, NULL, 0u, usb_serial, board_uart);
}

static void send_run_pwm_command(
    char **tokens,
    int count,
    Stream &usb_serial,
    Stream &board_uart)
{
    uint16_t duty = 0u;
    if (!read_required_u16(tokens, count, "duty", &duty, usb_serial))
    {
        return;
    }

    uint8_t payload[2];
    uint16_t position = 0u;
    write_u16(payload, &position, duty);
    transmit_command(PDS_BOARD_COMMAND_RUN_PWM, payload, position, usb_serial, board_uart);
}

static void send_mppt_demo_command(
    char **tokens,
    int count,
    Stream &usb_serial,
    Stream &board_uart)
{
    uint8_t payload[17];
    if (!write_mppt_payload(tokens, count, payload, usb_serial))
    {
        return;
    }

    transmit_command(
        PDS_BOARD_COMMAND_START_MPPT_DEMO,
        payload,
        sizeof(payload),
        usb_serial,
        board_uart);
}

static void send_state_command(
    uint8_t command_id,
    char **tokens,
    int count,
    Stream &usb_serial,
    Stream &board_uart)
{
    uint8_t payload[17];
    if (!write_state_payload(tokens, count, payload, usb_serial))
    {
        return;
    }

    transmit_command(command_id, payload, sizeof(payload), usb_serial, board_uart);
}

static void send_get_values_command(
    char **tokens,
    int count,
    Stream &usb_serial,
    Stream &board_uart)
{
    uint32_t fields = parse_field_mask(value_for_key(tokens, count, "fields"));
    uint8_t payload[4];
    uint16_t position = 0u;
    write_u16(payload, &position, (uint16_t)(fields & 0xFFFFu));
    write_u16(payload, &position, (uint16_t)((fields >> 16u) & 0xFFFFu));
    transmit_command(PDS_BOARD_COMMAND_GET_VALUES, payload, position, usb_serial, board_uart);
}

static void send_stream_values_command(
    char **tokens,
    int count,
    Stream &usb_serial,
    Stream &board_uart)
{
    bool enabled = (count > 1) && (strcmp(tokens[1], "on") == 0);
    if ((count <= 1) || (!enabled && (strcmp(tokens[1], "off") != 0)))
    {
        usb_serial.println("Use: stream_values on period=200 fields=all OR stream_values off");
        return;
    }

    uint16_t period = 1000u;
    if (enabled && !read_required_u16(tokens, count, "period", &period, usb_serial))
    {
        return;
    }

    uint32_t fields = parse_field_mask(value_for_key(tokens, count, "fields"));
    uint8_t payload[7];
    uint16_t position = 0u;
    write_u8(payload, &position, enabled ? 1u : 0u);
    write_u16(payload, &position, period);
    write_u16(payload, &position, (uint16_t)(fields & 0xFFFFu));
    write_u16(payload, &position, (uint16_t)((fields >> 16u) & 0xFFFFu));
    transmit_command(PDS_BOARD_COMMAND_STREAM_VALUES, payload, position, usb_serial, board_uart);
}

static void transmit_command(
    uint8_t command_id,
    const uint8_t *payload,
    uint16_t payload_length,
    Stream &usb_serial,
    Stream &board_uart)
{
    chips_frame_type frame;
    frame.sequence_number = next_sequence_number++;
    frame.command_id = command_id;
    frame.response_flag = 0u;
    frame.payload_length_in_bytes = payload_length;

    for (uint16_t i = 0u; i < payload_length; i += 1u)
    {
        frame.payload_bytes[i] = payload[i];
    }

    send_chips_frame_to_board(board_uart, &frame);
    usb_serial.print("TX command=0x");
    usb_serial.print(command_id, HEX);
    usb_serial.print(" sequence=");
    usb_serial.print(frame.sequence_number);
    usb_serial.print(" payload_bytes=");
    usb_serial.println(payload_length);
}

static int split_tokens(char *line, char **tokens, int maximum_tokens)
{
    int count = 0;
    char *token = strtok(line, " \t\r\n");
    while ((token != NULL) && (count < maximum_tokens))
    {
        tokens[count++] = token;
        token = strtok(NULL, " \t\r\n");
    }
    return count;
}

static const char *value_for_key(char **tokens, int count, const char *key)
{
    size_t key_length = strlen(key);
    for (int i = 1; i < count; i += 1)
    {
        if ((strncmp(tokens[i], key, key_length) == 0) && (tokens[i][key_length] == '='))
        {
            return &tokens[i][key_length + 1u];
        }
    }
    return NULL;
}

static bool read_required_i32(
    char **tokens,
    int count,
    const char *key,
    int32_t *value,
    Stream &output)
{
    const char *text = value_for_key(tokens, count, key);
    if (text == NULL)
    {
        output.print("Missing ");
        output.println(key);
        return false;
    }
    *value = (int32_t)strtol(text, NULL, 0);
    return true;
}

static bool read_required_u16(
    char **tokens,
    int count,
    const char *key,
    uint16_t *value,
    Stream &output)
{
    int32_t signed_value = 0;
    if (!read_required_i32(tokens, count, key, &signed_value, output))
    {
        return false;
    }
    if ((signed_value < 0) || (signed_value > 65535))
    {
        output.print(key);
        output.println(" must be between 0 and 65535");
        return false;
    }
    *value = (uint16_t)signed_value;
    return true;
}

static bool read_required_i16(
    char **tokens,
    int count,
    const char *key,
    int16_t *value,
    Stream &output)
{
    int32_t signed_value = 0;
    if (!read_required_i32(tokens, count, key, &signed_value, output))
    {
        return false;
    }
    if ((signed_value < -32768) || (signed_value > 32767))
    {
        output.print(key);
        output.println(" must fit in a signed 16-bit value");
        return false;
    }
    *value = (int16_t)signed_value;
    return true;
}

static bool read_required_double(
    char **tokens,
    int count,
    const char *key,
    double *value,
    Stream &output)
{
    const char *text = value_for_key(tokens, count, key);
    if (text == NULL)
    {
        output.print("Missing ");
        output.println(key);
        return false;
    }
    *value = strtod(text, NULL);
    return true;
}

static bool write_state_payload(
    char **tokens,
    int count,
    uint8_t *payload,
    Stream &output)
{
    uint16_t battery_voltage = 0u;
    int16_t battery_current = 0;
    uint16_t panel_voltage = 0u;
    uint16_t panel_current = 0u;
    uint16_t rail_voltage = 0u;
    int16_t temperature = 0;
    uint16_t heartbeat_as_u16 = 0u;
    uint16_t faults = 0u;
    uint8_t obc_mode = 0u;
    uint8_t safe_substate = 0u;

    if (!read_required_u16(tokens, count, "battery_voltage", &battery_voltage, output)) return false;
    if (!read_required_i16(tokens, count, "battery_current", &battery_current, output)) return false;
    if (!read_required_u16(tokens, count, "panel_voltage", &panel_voltage, output)) return false;
    if (!read_required_u16(tokens, count, "panel_current", &panel_current, output)) return false;
    if (!read_required_u16(tokens, count, "rail_voltage", &rail_voltage, output)) return false;
    if (!read_required_i16(tokens, count, "temperature", &temperature, output)) return false;
    if (!read_required_u16(tokens, count, "heartbeat", &heartbeat_as_u16, output)) return false;
    if (!parse_obc_mode(value_for_key(tokens, count, "obc_mode"), &obc_mode)) return false;
    if (!parse_safe_substate(value_for_key(tokens, count, "safe_substate"), &safe_substate)) return false;
    if (!read_required_u16(tokens, count, "faults", &faults, output)) return false;

    uint16_t position = 0u;
    write_u16(payload, &position, battery_voltage);
    write_i16(payload, &position, battery_current);
    write_u16(payload, &position, panel_voltage);
    write_u16(payload, &position, panel_current);
    write_u16(payload, &position, rail_voltage);
    write_i16(payload, &position, temperature);
    write_u8(payload, &position, (uint8_t)heartbeat_as_u16);
    write_u8(payload, &position, obc_mode);
    write_u8(payload, &position, safe_substate);
    write_u16(payload, &position, faults);
    return true;
}

static bool write_mppt_payload(
    char **tokens,
    int count,
    uint8_t *payload,
    Stream &output)
{
    const char *curve = value_for_key(tokens, count, "curve");
    if ((curve == NULL) || (strcmp(curve, "quadratic") != 0))
    {
        output.println("Only curve=quadratic is supported");
        return false;
    }

    double a = 0.0;
    double b = 0.0;
    int16_t c = 0;
    uint16_t v_min = 0u;
    uint16_t v_max = 0u;
    uint16_t battery_voltage = 0u;
    if (!read_required_double(tokens, count, "a", &a, output)) return false;
    if (!read_required_double(tokens, count, "b", &b, output)) return false;
    if (!read_required_i16(tokens, count, "c", &c, output)) return false;
    if (!read_required_u16(tokens, count, "v_min", &v_min, output)) return false;
    if (!read_required_u16(tokens, count, "v_max", &v_max, output)) return false;
    if (!read_required_u16(tokens, count, "battery_voltage", &battery_voltage, output)) return false;

    uint16_t position = 0u;
    write_u8(payload, &position, PDS_CURVE_TYPE_QUADRATIC);
    write_i32(payload, &position, (int32_t)lround(a * 1000.0));
    write_i32(payload, &position, (int32_t)lround(b * 1000.0));
    write_i16(payload, &position, c);
    write_u16(payload, &position, v_min);
    write_u16(payload, &position, v_max);
    write_u16(payload, &position, battery_voltage);
    return true;
}

static bool parse_obc_mode(const char *text, uint8_t *mode)
{
    if (text == NULL) return false;
    if (strcmp(text, "measurement") == 0) *mode = EPS_SATELLITE_MODE_MEASUREMENT;
    else if (strcmp(text, "charging") == 0) *mode = EPS_SATELLITE_MODE_CHARGING;
    else if (strcmp(text, "uhf") == 0) *mode = EPS_SATELLITE_MODE_UHF_COMMUNICATION;
    else if (strcmp(text, "safe") == 0) *mode = EPS_SATELLITE_MODE_SAFE;
    else *mode = (uint8_t)strtoul(text, NULL, 0);
    return true;
}

static bool parse_safe_substate(const char *text, uint8_t *substate)
{
    if (text == NULL) return false;
    if (strcmp(text, "detumbling") == 0) *substate = EPS_SAFE_SUB_STATE_DETUMBLING;
    else if (strcmp(text, "charging") == 0) *substate = EPS_SAFE_SUB_STATE_CHARGING;
    else if (strcmp(text, "communication") == 0) *substate = EPS_SAFE_SUB_STATE_COMMUNICATION;
    else if (strcmp(text, "reboot") == 0) *substate = EPS_SAFE_SUB_STATE_REBOOT;
    else *substate = (uint8_t)strtoul(text, NULL, 0);
    return true;
}

static uint32_t parse_field_mask(const char *field_text)
{
    if ((field_text == NULL) || (strcmp(field_text, "all") == 0))
    {
        return PDS_VALUE_FIELD_ALL;
    }

    uint32_t mask = 0u;
    char local_copy[96];
    strncpy(local_copy, field_text, sizeof(local_copy) - 1u);
    local_copy[sizeof(local_copy) - 1u] = '\0';

    char *field = strtok(local_copy, ",");
    while (field != NULL)
    {
        if (strcmp(field, "mode") == 0) mask |= PDS_VALUE_FIELD_MODE;
        else if (strcmp(field, "pwm") == 0) mask |= PDS_VALUE_FIELD_PWM;
        else if (strcmp(field, "state") == 0) mask |= PDS_VALUE_FIELD_STATE;
        else if (strcmp(field, "loads") == 0) mask |= PDS_VALUE_FIELD_LOADS;
        else if (strcmp(field, "faults") == 0) mask |= PDS_VALUE_FIELD_FAULTS;
        else if (strcmp(field, "mppt") == 0) mask |= PDS_VALUE_FIELD_MPPT;
        else if (strcmp(field, "inputs") == 0) mask |= PDS_VALUE_FIELD_INPUTS;
        else if (strcmp(field, "commands") == 0) mask |= PDS_VALUE_FIELD_COMMANDS;
        field = strtok(NULL, ",");
    }

    return (mask == 0u) ? PDS_VALUE_FIELD_ALL : mask;
}

static void write_u8(uint8_t *payload, uint16_t *position, uint8_t value)
{
    payload[(*position)++] = value;
}

static void write_u16(uint8_t *payload, uint16_t *position, uint16_t value)
{
    payload[(*position)++] = (uint8_t)(value & 0xFFu);
    payload[(*position)++] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void write_i16(uint8_t *payload, uint16_t *position, int16_t value)
{
    write_u16(payload, position, (uint16_t)value);
}

static void write_i32(uint8_t *payload, uint16_t *position, int32_t value)
{
    uint32_t unsigned_value = (uint32_t)value;
    payload[(*position)++] = (uint8_t)(unsigned_value & 0xFFu);
    payload[(*position)++] = (uint8_t)((unsigned_value >> 8u) & 0xFFu);
    payload[(*position)++] = (uint8_t)((unsigned_value >> 16u) & 0xFFu);
    payload[(*position)++] = (uint8_t)((unsigned_value >> 24u) & 0xFFu);
}
