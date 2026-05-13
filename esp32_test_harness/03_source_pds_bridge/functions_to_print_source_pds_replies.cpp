#include "functions_to_print_source_pds_replies.h"
#include "source_pds_command_contract.h"

static uint8_t read_u8(const uint8_t *payload, uint16_t *position);
static uint16_t read_u16(const uint8_t *payload, uint16_t *position);
static int16_t read_i16(const uint8_t *payload, uint16_t *position);
static uint32_t read_u32(const uint8_t *payload, uint16_t *position);
static int32_t read_i32(const uint8_t *payload, uint16_t *position);
static void print_ack_reply(const chips_frame_type *frame, Stream &output);
static void print_values_reply(const chips_frame_type *frame, Stream &output);
static void print_status_header(const uint8_t *payload, uint16_t *p, Stream &output);
static void print_command_counters(const uint8_t *payload, uint16_t *p, Stream &output);
static void print_mode_and_pwm_values(const uint8_t *payload, uint16_t *p, Stream &output);
static void print_injected_input_values(const uint8_t *payload, uint16_t *p, Stream &output);
static void print_mppt_curve_values(const uint8_t *payload, uint16_t *p, Stream &output);
static void print_runtime_snapshot_values(const uint8_t *payload, uint16_t *p, Stream &output);
static const char *requested_mode_name(uint8_t mode);
static const char *pcu_mode_name(uint8_t mode);

void print_reply_received_from_source_pds_board(
    const chips_frame_type *frame,
    Stream &output)
{
    output.print("RX reply command=0x");
    output.print(frame->command_id, HEX);
    output.print(" sequence=");
    output.print(frame->sequence_number);
    output.print(" bytes=");
    output.println(frame->payload_length_in_bytes);

    if (frame->payload_length_in_bytes == 1u)
    {
        print_ack_reply(frame, output);
        return;
    }

    if (frame->payload_length_in_bytes >= 100u)
    {
        print_values_reply(frame, output);
        return;
    }

    output.println("  reply payload is shorter than the Source PDS status packet");
}

const char *source_pds_status_name(uint8_t status)
{
    if (status == CHIPS_STATUS_SUCCESS) return "success";
    if (status == CHIPS_STATUS_UNKNOWN_COMMAND) return "unknown_command";
    if (status == CHIPS_STATUS_INVALID_PAYLOAD_LENGTH) return "bad_payload_length";
    if (status == CHIPS_STATUS_PARAMETER_OUT_OF_RANGE) return "value_out_of_range";
    if (status == CHIPS_STATUS_COMMAND_NOT_AVAILABLE) return "command_not_available";
    return "unknown_status";
}

static void print_ack_reply(const chips_frame_type *frame, Stream &output)
{
    uint8_t status = frame->payload_bytes[0];
    output.print("  status=");
    output.print(source_pds_status_name(status));
    output.print(" (");
    output.print(status);
    output.println(")");
}

static void print_values_reply(const chips_frame_type *frame, Stream &output)
{
    uint16_t p = 0u;
    print_status_header(frame->payload_bytes, &p, output);
    print_command_counters(frame->payload_bytes, &p, output);
    print_mode_and_pwm_values(frame->payload_bytes, &p, output);
    print_injected_input_values(frame->payload_bytes, &p, output);
    print_mppt_curve_values(frame->payload_bytes, &p, output);
    print_runtime_snapshot_values(frame->payload_bytes, &p, output);
}

static void print_status_header(const uint8_t *payload, uint16_t *p, Stream &output)
{
    uint8_t status = read_u8(payload, p);
    uint8_t version = read_u8(payload, p);
    uint32_t fields = read_u32(payload, p);
    uint32_t timestamp = read_u32(payload, p);

    output.print("  status=");
    output.print(source_pds_status_name(status));
    output.print(" version=");
    output.print(version);
    output.print(" timestamp_ms=");
    output.println(timestamp);
    output.print("  requested_fields=0x");
    output.println(fields, HEX);
}

static void print_command_counters(const uint8_t *payload, uint16_t *p, Stream &output)
{
    uint32_t valid_frames = read_u32(payload, p);
    uint32_t crc_errors = read_u32(payload, p);
    uint32_t long_errors = read_u32(payload, p);
    uint32_t executed = read_u32(payload, p);
    uint8_t last_command = read_u8(payload, p);
    uint8_t last_status = read_u8(payload, p);

    output.print("  counters: valid_frames=");
    output.print(valid_frames);
    output.print(" crc_errors=");
    output.print(crc_errors);
    output.print(" long_errors=");
    output.print(long_errors);
    output.print(" executed=");
    output.print(executed);
    output.print(" last_command=0x");
    output.print(last_command, HEX);
    output.print(" last_status=");
    output.println(source_pds_status_name(last_status));
}

static void print_mode_and_pwm_values(const uint8_t *payload, uint16_t *p, Stream &output)
{
    uint8_t requested_mode = read_u8(payload, p);
    uint8_t stream_enabled = read_u8(payload, p);
    uint16_t stream_period = read_u16(payload, p);
    uint32_t stream_fields = read_u32(payload, p);
    uint16_t fixed_pwm = read_u16(payload, p);
    uint16_t requested_pwm = read_u16(payload, p);
    uint16_t applied_pwm = read_u16(payload, p);
    uint8_t pwm_enabled = read_u8(payload, p);

    output.print("  mode=");
    output.print(requested_mode_name(requested_mode));
    output.print(" stream=");
    output.print(stream_enabled);
    output.print(" period=");
    output.print(stream_period);
    output.print(" stream_fields=0x");
    output.println(stream_fields, HEX);
    output.print("  pwm: fixed=");
    output.print(fixed_pwm);
    output.print(" requested=");
    output.print(requested_pwm);
    output.print(" applied=");
    output.print(applied_pwm);
    output.print(" enabled=");
    output.println(pwm_enabled);
}

static void print_injected_input_values(const uint8_t *payload, uint16_t *p, Stream &output)
{
    uint16_t input_battery_mv = read_u16(payload, p);
    int16_t input_battery_ma = read_i16(payload, p);
    uint16_t input_panel_mv = read_u16(payload, p);
    uint16_t input_panel_ma = read_u16(payload, p);
    uint16_t input_rail_mv = read_u16(payload, p);
    int16_t input_temp = read_i16(payload, p);
    uint8_t heartbeat = read_u8(payload, p);
    uint8_t obc_mode = read_u8(payload, p);
    uint8_t safe_substate = read_u8(payload, p);
    uint16_t faults = read_u16(payload, p);

    output.print("  inputs: battery=");
    output.print(input_battery_mv);
    output.print("mV ");
    output.print(input_battery_ma);
    output.print("mA panel=");
    output.print(input_panel_mv);
    output.print("mV ");
    output.print(input_panel_ma);
    output.print("mA rail=");
    output.print(input_rail_mv);
    output.print("mV temp=");
    output.print(input_temp);
    output.print(" heartbeat=");
    output.print(heartbeat);
    output.print(" obc_mode=");
    output.print(obc_mode);
    output.print(" safe_substate=");
    output.print(safe_substate);
    output.print(" faults=");
    output.println(faults);
}

static void print_mppt_curve_values(const uint8_t *payload, uint16_t *p, Stream &output)
{
    uint8_t curve_type = read_u8(payload, p);
    int32_t curve_a = read_i32(payload, p);
    int32_t curve_b = read_i32(payload, p);
    int16_t curve_c = read_i16(payload, p);
    uint16_t curve_v_min = read_u16(payload, p);
    uint16_t curve_v_max = read_u16(payload, p);
    uint16_t curve_battery = read_u16(payload, p);

    output.print("  mppt: curve=");
    output.print(curve_type);
    output.print(" a_scaled=");
    output.print(curve_a);
    output.print(" b_scaled=");
    output.print(curve_b);
    output.print(" c=");
    output.print(curve_c);
    output.print(" v_range=");
    output.print(curve_v_min);
    output.print("..");
    output.print(curve_v_max);
    output.print(" battery=");
    output.println(curve_battery);
}

static void print_runtime_snapshot_values(const uint8_t *payload, uint16_t *p, Stream &output)
{
    uint32_t loop_count = read_u32(payload, p);
    uint16_t panel_mv = read_u16(payload, p);
    uint16_t panel_ma = read_u16(payload, p);
    uint32_t panel_mw = read_u32(payload, p);
    uint16_t mppt_duty = read_u16(payload, p);
    uint16_t state_duty = read_u16(payload, p);
    uint8_t pcu_mode = read_u8(payload, p);
    uint8_t safe_active = read_u8(payload, p);
    uint8_t safe_reason = read_u8(payload, p);
    uint8_t panel_efuse = read_u8(payload, p);
    uint8_t heater = read_u8(payload, p);
    uint8_t safe_alert = read_u8(payload, p);
    uint8_t load_mask = read_u8(payload, p);

    output.print("  snapshot: loops=");
    output.print(loop_count);
    output.print(" panel=");
    output.print(panel_mv);
    output.print("mV ");
    output.print(panel_ma);
    output.print("mA power=");
    output.print(panel_mw);
    output.print("mW mppt_duty=");
    output.print(mppt_duty);
    output.print(" state_duty=");
    output.print(state_duty);
    output.print(" pcu=");
    output.println(pcu_mode_name(pcu_mode));
    output.print("  safety: safe=");
    output.print(safe_active);
    output.print(" reason=");
    output.print(safe_reason);
    output.print(" panel_efuse=");
    output.print(panel_efuse);
    output.print(" heater=");
    output.print(heater);
    output.print(" safe_alert=");
    output.print(safe_alert);
    output.print(" loads=0x");
    output.println(load_mask, HEX);
}

static uint8_t read_u8(const uint8_t *payload, uint16_t *position)
{
    return payload[(*position)++];
}

static uint16_t read_u16(const uint8_t *payload, uint16_t *position)
{
    uint16_t value = payload[*position] | ((uint16_t)payload[*position + 1u] << 8u);
    *position += 2u;
    return value;
}

static int16_t read_i16(const uint8_t *payload, uint16_t *position)
{
    return (int16_t)read_u16(payload, position);
}

static uint32_t read_u32(const uint8_t *payload, uint16_t *position)
{
    uint32_t value = (uint32_t)payload[*position]
        | ((uint32_t)payload[*position + 1u] << 8u)
        | ((uint32_t)payload[*position + 2u] << 16u)
        | ((uint32_t)payload[*position + 3u] << 24u);
    *position += 4u;
    return value;
}

static int32_t read_i32(const uint8_t *payload, uint16_t *position)
{
    return (int32_t)read_u32(payload, position);
}

static const char *requested_mode_name(uint8_t mode)
{
    if (mode == PDS_REQUESTED_MODE_OFF) return "off";
    if (mode == PDS_REQUESTED_MODE_FLIGHT) return "flight";
    if (mode == PDS_REQUESTED_MODE_MPPT_TEST) return "mppt_test";
    if (mode == PDS_REQUESTED_MODE_STATE_TEST) return "state_test";
    if (mode == PDS_REQUESTED_MODE_FIXED_PWM_TEST) return "fixed_pwm_test";
    return "unknown";
}

static const char *pcu_mode_name(uint8_t mode)
{
    if (mode == EPS_PCU_MODE_MPPT_CHARGE) return "MPPT_CHARGE";
    if (mode == EPS_PCU_MODE_CV_FLOAT) return "CV_FLOAT";
    if (mode == EPS_PCU_MODE_SA_LOAD_FOLLOW) return "SA_LOAD_FOLLOW";
    if (mode == EPS_PCU_MODE_BATTERY_DISCHARGE) return "BATTERY_DISCHARGE";
    return "unknown";
}
