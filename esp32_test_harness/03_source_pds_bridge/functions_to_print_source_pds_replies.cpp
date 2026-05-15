#include "functions_to_print_source_pds_replies.h"
#include "source_pds_command_contract.h"

static uint8_t read_u8(const uint8_t *payload, uint16_t *position);
static uint16_t read_u16(const uint8_t *payload, uint16_t *position);
static int16_t read_i16(const uint8_t *payload, uint16_t *position);
static uint32_t read_u32(const uint8_t *payload, uint16_t *position);

typedef struct {
    uint8_t status;
    uint8_t version;
    uint32_t requested_fields;
    uint32_t timestamp_ms;

    uint32_t valid_frames;
    uint32_t crc_errors;
    uint32_t long_errors;
    uint32_t executed_commands;
    uint8_t last_command;
    uint8_t last_status;

    uint8_t requested_mode;
    uint8_t stream_enabled;
    uint16_t stream_period_ms;
    uint32_t stream_fields;

    uint16_t fixed_pwm;
    uint16_t requested_pwm;
    uint16_t applied_pwm;
    uint8_t pwm_enabled;

    uint16_t input_battery_mv;
    int16_t input_battery_ma;
    uint16_t input_panel_mv;
    uint16_t input_panel_ma;
    uint16_t input_rail_mv;
    int16_t input_temperature;
    uint8_t heartbeat;
    uint8_t obc_mode;
    uint8_t safe_substate;
    uint16_t faults;

    uint8_t mppt_input_sample_is_valid;
    uint32_t loop_count;
    uint16_t panel_mv;
    uint16_t panel_ma;
    uint32_t panel_mw;
    uint16_t mppt_duty;
    uint16_t state_duty;
    uint8_t pcu_mode;
    uint8_t safe_active;
    uint8_t safe_reason;
    uint8_t panel_efuse;
    uint8_t heater;
    uint8_t safe_alert;
    uint8_t load_mask;

    /* Manual mode block, appended unconditionally by the firmware in
     * payload version 3. Older firmware (version 2) omits this block; in
     * that case the reader leaves these fields at zero. */
    bool manual_block_present;
    uint16_t manual_pwm;
    uint8_t manual_pv_requested;
    uint8_t manual_bat_requested;
    uint8_t manual_led_requested;
    uint8_t status_led_is_on;
    uint8_t pv_pgood;
    uint8_t pv_flt;
    uint8_t bat_pgood;
    uint8_t bat_flt;

    /* Sensor reads block, payload version 4. Always-on observability:
     * every status reply carries each sensor's reading and what Layer 1
     * chose, regardless of which firmware mode is active. Older firmware
     * (version 3 or below) omits this block; reader writes zeros then. */
    bool sensor_reads_block_present;
    uint8_t sensor_source;
    uint16_t layer1_battery_v_mv;
    int16_t  layer1_battery_i_ma;
    uint16_t layer1_panel_v_mv;
    uint16_t layer1_panel_i_ma;
    uint16_t layer1_rail_v_mv;
    uint16_t ina226_battery_v_mv;
    int16_t  ina226_battery_i_ma;
    uint16_t ina226_panel_v_mv;
    int16_t  ina226_panel_i_ma;
    int16_t  lt6108_battery_i_ma;
    int16_t  lt6108_panel_i_ma;
    uint16_t tps25940_battery_imon_ma;
    uint16_t tps25940_panel_imon_ma;
    uint16_t divider_rail_v_mv;
    uint16_t divider_panel_bus_v_mv;
} source_pds_status_values_type;

static void print_ack_reply(const chips_frame_type *frame, Stream &output);
static void print_values_reply(const chips_frame_type *frame, Stream &output);
static void read_status_values(
    const uint8_t *payload,
    source_pds_status_values_type *values);
static bool field_is_requested(uint32_t requested_fields, uint32_t field_mask);
static void print_status_header(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_command_counters(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_mode_values(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_pwm_values(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_injected_input_values(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_mppt_live_values(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_state_values(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_fault_values(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_load_values(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_manual_outputs_and_efuse_status_values(
    const source_pds_status_values_type *values,
    Stream &output);
static void print_sensor_reads_block(
    const source_pds_status_values_type *values,
    Stream &output);
static const char *requested_mode_name(uint8_t mode);
static const char *pcu_mode_name(uint8_t mode);

void print_reply_received_from_source_pds_board(
    const chips_frame_type *frame,
    Stream &output)
{
    output.print("[BOARD] reply command=0x");
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

    /* The Source PDS status packet grew from 84 to 94 bytes when manual
     * control mode added 10 trailing bytes (manual_pwm + 4 manual requested
     * flags + 4 eFuse status bits + the LED actual-level mirror). The check
     * here matches the shorter, older size on purpose: anything that was a
     * valid status packet before remains parseable. The new fields are read
     * past the 84-byte mark and silently ignored if the packet is older. */
    if (frame->payload_length_in_bytes >= 84u)
    {
        print_values_reply(frame, output);
        return;
    }

    output.println("[BOARD] reply payload is shorter than the Source PDS status packet");
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
    output.print("[BOARD] status=");
    output.print(source_pds_status_name(status));
    output.print(" (");
    output.print(status);
    output.println(")");
}

static void print_values_reply(const chips_frame_type *frame, Stream &output)
{
    source_pds_status_values_type values;
    read_status_values(frame->payload_bytes, &values);

    print_status_header(&values, output);

    if (field_is_requested(values.requested_fields, PDS_VALUE_FIELD_COMMANDS))
    {
        print_command_counters(&values, output);
    }
    if (field_is_requested(values.requested_fields, PDS_VALUE_FIELD_MODE))
    {
        print_mode_values(&values, output);
    }
    if (field_is_requested(values.requested_fields, PDS_VALUE_FIELD_PWM))
    {
        print_pwm_values(&values, output);
    }
    if (field_is_requested(values.requested_fields, PDS_VALUE_FIELD_INPUTS))
    {
        print_injected_input_values(&values, output);
    }
    if (field_is_requested(values.requested_fields, PDS_VALUE_FIELD_MPPT))
    {
        print_mppt_live_values(&values, output);
    }
    if (field_is_requested(values.requested_fields, PDS_VALUE_FIELD_STATE))
    {
        print_state_values(&values, output);
    }
    if (field_is_requested(values.requested_fields, PDS_VALUE_FIELD_FAULTS))
    {
        print_fault_values(&values, output);
    }
    if (field_is_requested(values.requested_fields, PDS_VALUE_FIELD_LOADS))
    {
        print_load_values(&values, output);
    }
    if (values.manual_block_present)
    {
        print_manual_outputs_and_efuse_status_values(&values, output);
    }
    if (values.sensor_reads_block_present)
    {
        print_sensor_reads_block(&values, output);
    }
}

static void read_status_values(
    const uint8_t *payload,
    source_pds_status_values_type *values)
{
    uint16_t p = 0u;

    values->status = read_u8(payload, &p);
    values->version = read_u8(payload, &p);
    values->requested_fields = read_u32(payload, &p);
    values->timestamp_ms = read_u32(payload, &p);

    values->valid_frames = read_u32(payload, &p);
    values->crc_errors = read_u32(payload, &p);
    values->long_errors = read_u32(payload, &p);
    values->executed_commands = read_u32(payload, &p);
    values->last_command = read_u8(payload, &p);
    values->last_status = read_u8(payload, &p);

    values->requested_mode = read_u8(payload, &p);
    values->stream_enabled = read_u8(payload, &p);
    values->stream_period_ms = read_u16(payload, &p);
    values->stream_fields = read_u32(payload, &p);

    values->fixed_pwm = read_u16(payload, &p);
    values->requested_pwm = read_u16(payload, &p);
    values->applied_pwm = read_u16(payload, &p);
    values->pwm_enabled = read_u8(payload, &p);

    values->input_battery_mv = read_u16(payload, &p);
    values->input_battery_ma = read_i16(payload, &p);
    values->input_panel_mv = read_u16(payload, &p);
    values->input_panel_ma = read_u16(payload, &p);
    values->input_rail_mv = read_u16(payload, &p);
    values->input_temperature = read_i16(payload, &p);
    values->heartbeat = read_u8(payload, &p);
    values->obc_mode = read_u8(payload, &p);
    values->safe_substate = read_u8(payload, &p);
    values->faults = read_u16(payload, &p);

    values->mppt_input_sample_is_valid = read_u8(payload, &p);
    values->loop_count = read_u32(payload, &p);
    values->panel_mv = read_u16(payload, &p);
    values->panel_ma = read_u16(payload, &p);
    values->panel_mw = read_u32(payload, &p);
    values->mppt_duty = read_u16(payload, &p);
    values->state_duty = read_u16(payload, &p);
    values->pcu_mode = read_u8(payload, &p);
    values->safe_active = read_u8(payload, &p);
    values->safe_reason = read_u8(payload, &p);
    values->panel_efuse = read_u8(payload, &p);
    values->heater = read_u8(payload, &p);
    values->safe_alert = read_u8(payload, &p);
    values->load_mask = read_u8(payload, &p);

    /* Older firmware stops here at byte 84. Newer firmware appends the
     * manual mode block. Mark presence via version field (bumped to 3). */
    values->manual_block_present = (values->version >= 3u);
    if (values->manual_block_present)
    {
        values->manual_pwm = read_u16(payload, &p);
        values->manual_pv_requested = read_u8(payload, &p);
        values->manual_bat_requested = read_u8(payload, &p);
        values->manual_led_requested = read_u8(payload, &p);
        values->status_led_is_on = read_u8(payload, &p);
        values->pv_pgood = read_u8(payload, &p);
        values->pv_flt = read_u8(payload, &p);
        values->bat_pgood = read_u8(payload, &p);
        values->bat_flt = read_u8(payload, &p);
    }
    else
    {
        values->manual_pwm = 0u;
        values->manual_pv_requested = 0u;
        values->manual_bat_requested = 0u;
        values->manual_led_requested = 0u;
        values->status_led_is_on = 0u;
        values->pv_pgood = 0u;
        values->pv_flt = 0u;
        values->bat_pgood = 0u;
        values->bat_flt = 0u;
    }

    /* Sensor reads block, version 4 onward. Same backward-compat
     * pattern as the manual block: present from version 4, zeroed
     * if the firmware is older. */
    values->sensor_reads_block_present = (values->version >= 4u);
    if (values->sensor_reads_block_present)
    {
        values->sensor_source             = read_u8(payload, &p);
        values->layer1_battery_v_mv       = read_u16(payload, &p);
        values->layer1_battery_i_ma       = read_i16(payload, &p);
        values->layer1_panel_v_mv         = read_u16(payload, &p);
        values->layer1_panel_i_ma         = read_u16(payload, &p);
        values->layer1_rail_v_mv          = read_u16(payload, &p);
        values->ina226_battery_v_mv       = read_u16(payload, &p);
        values->ina226_battery_i_ma       = read_i16(payload, &p);
        values->ina226_panel_v_mv         = read_u16(payload, &p);
        values->ina226_panel_i_ma         = read_i16(payload, &p);
        values->lt6108_battery_i_ma       = read_i16(payload, &p);
        values->lt6108_panel_i_ma         = read_i16(payload, &p);
        values->tps25940_battery_imon_ma  = read_u16(payload, &p);
        values->tps25940_panel_imon_ma    = read_u16(payload, &p);
        values->divider_rail_v_mv         = read_u16(payload, &p);
        values->divider_panel_bus_v_mv    = read_u16(payload, &p);
    }
    else
    {
        values->sensor_source             = 0u;
        values->layer1_battery_v_mv       = 0u;
        values->layer1_battery_i_ma       = 0;
        values->layer1_panel_v_mv         = 0u;
        values->layer1_panel_i_ma         = 0u;
        values->layer1_rail_v_mv          = 0u;
        values->ina226_battery_v_mv       = 0u;
        values->ina226_battery_i_ma       = 0;
        values->ina226_panel_v_mv         = 0u;
        values->ina226_panel_i_ma         = 0;
        values->lt6108_battery_i_ma       = 0;
        values->lt6108_panel_i_ma         = 0;
        values->tps25940_battery_imon_ma  = 0u;
        values->tps25940_panel_imon_ma    = 0u;
        values->divider_rail_v_mv         = 0u;
        values->divider_panel_bus_v_mv    = 0u;
    }
}

static bool field_is_requested(uint32_t requested_fields, uint32_t field_mask)
{
    return ((requested_fields == PDS_VALUE_FIELD_ALL)
        || ((requested_fields & field_mask) != 0u));
}

static void print_status_header(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] status=");
    output.print(source_pds_status_name(values->status));
    output.print(" version=");
    output.print(values->version);
    output.print(" timestamp_ms=");
    output.println(values->timestamp_ms);
    output.print("[BOARD] requested_fields=0x");
    output.println(values->requested_fields, HEX);
}

static void print_command_counters(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] counters valid_frames=");
    output.print(values->valid_frames);
    output.print(" crc_errors=");
    output.print(values->crc_errors);
    output.print(" long_errors=");
    output.print(values->long_errors);
    output.print(" executed=");
    output.print(values->executed_commands);
    output.print(" last_command=0x");
    output.print(values->last_command, HEX);
    output.print(" last_status=");
    output.println(source_pds_status_name(values->last_status));
}

static void print_mode_values(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] mode=");
    output.print(requested_mode_name(values->requested_mode));
    output.print(" stream=");
    output.print(values->stream_enabled);
    output.print(" period=");
    output.print(values->stream_period_ms);
    output.print(" stream_fields=0x");
    output.println(values->stream_fields, HEX);
}

static void print_pwm_values(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] pwm fixed=");
    output.print(values->fixed_pwm);
    output.print(" requested=");
    output.print(values->requested_pwm);
    output.print(" applied=");
    output.print(values->applied_pwm);
    output.print(" enabled=");
    output.println(values->pwm_enabled);
}

static void print_injected_input_values(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] inputs battery=");
    output.print(values->input_battery_mv);
    output.print("mV ");
    output.print(values->input_battery_ma);
    output.print("mA panel=");
    output.print(values->input_panel_mv);
    output.print("mV ");
    output.print(values->input_panel_ma);
    output.print("mA rail=");
    output.print(values->input_rail_mv);
    output.print("mV temp=");
    output.print(values->input_temperature);
    output.print(" heartbeat=");
    output.print(values->heartbeat);
    output.print(" obc_mode=");
    output.print(values->obc_mode);
    output.print(" safe_substate=");
    output.print(values->safe_substate);
    output.print(" faults=");
    output.println(values->faults);
}

static void print_mppt_live_values(
    const source_pds_status_values_type *values,
    Stream &output)
{
    if (values->mppt_input_sample_is_valid == 0u)
    {
        output.println("[BOARD] mppt_live valid=0");
        return;
    }

    output.print("[BOARD] mppt_live loops=");
    output.print(values->loop_count);
    output.print(" panel=");
    output.print(values->panel_mv);
    output.print("mV ");
    output.print(values->panel_ma);
    output.print("mA power=");
    output.print(values->panel_mw);
    output.print("mW duty=");
    output.println(values->mppt_duty);
}

static void print_state_values(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] state loops=");
    output.print(values->loop_count);
    output.print(" pcu=");
    output.print(pcu_mode_name(values->pcu_mode));
    output.print(" state_duty=");
    output.println(values->state_duty);
}

static void print_fault_values(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] faults safe=");
    output.print(values->safe_active);
    output.print(" reason=");
    output.print(values->safe_reason);
    output.print(" safe_alert=");
    output.println(values->safe_alert);
}

static void print_load_values(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] outputs panel_efuse=");
    output.print(values->panel_efuse);
    output.print(" heater=");
    output.print(values->heater);
    output.print(" loads=0x");
    output.println(values->load_mask, HEX);
}

static void print_manual_outputs_and_efuse_status_values(
    const source_pds_status_values_type *values,
    Stream &output)
{
    output.print("[BOARD] manual pwm=");
    output.print(values->manual_pwm);
    output.print(" pv_req=");
    output.print(values->manual_pv_requested);
    output.print(" bat_req=");
    output.print(values->manual_bat_requested);
    output.print(" led_req=");
    output.print(values->manual_led_requested);
    output.print(" led_is_on=");
    output.println(values->status_led_is_on);

    output.print("[BOARD] efuse pv_pgood=");
    output.print(values->pv_pgood);
    output.print(" pv_flt=");
    output.print(values->pv_flt);
    output.print(" bat_pgood=");
    output.print(values->bat_pgood);
    output.print(" bat_flt=");
    output.println(values->bat_flt);
}

static void print_sensor_reads_block(
    const source_pds_status_values_type *values,
    Stream &output)
{
    /* One-line-per-measurement format chosen so the Python regex parsers
     * stay simple: every line starts with "sensor" and a measurement tag,
     * then key=value pairs. */
    const char *source_label =
        (values->sensor_source == PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE)
        ? "real" : "injected";
    output.print("[BOARD] sensor source=");
    output.println(source_label);

    output.print("[BOARD] sensor_battery_v ina226=");
    output.print(values->ina226_battery_v_mv);
    output.print(" divider=");
    output.print(values->divider_rail_v_mv);
    output.print(" layer1=");
    output.println(values->layer1_battery_v_mv);

    output.print("[BOARD] sensor_battery_i ina226=");
    output.print(values->ina226_battery_i_ma);
    output.print(" lt6108=");
    output.print(values->lt6108_battery_i_ma);
    output.print(" tps25940=");
    output.print(values->tps25940_battery_imon_ma);
    output.print(" layer1=");
    output.println(values->layer1_battery_i_ma);

    output.print("[BOARD] sensor_panel_v ina226=");
    output.print(values->ina226_panel_v_mv);
    output.print(" divider=");
    output.print(values->divider_panel_bus_v_mv);
    output.print(" layer1=");
    output.println(values->layer1_panel_v_mv);

    output.print("[BOARD] sensor_panel_i ina226=");
    output.print(values->ina226_panel_i_ma);
    output.print(" lt6108=");
    output.print(values->lt6108_panel_i_ma);
    output.print(" tps25940=");
    output.print(values->tps25940_panel_imon_ma);
    output.print(" layer1=");
    output.println(values->layer1_panel_i_ma);

    output.print("[BOARD] sensor_rail_v divider=");
    output.print(values->divider_rail_v_mv);
    output.print(" layer1=");
    output.println(values->layer1_rail_v_mv);
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

static const char *requested_mode_name(uint8_t mode)
{
    if (mode == PDS_REQUESTED_MODE_OFF) return "off";
    if (mode == PDS_REQUESTED_MODE_FLIGHT) return "flight";
    if (mode == PDS_REQUESTED_MODE_MPPT_TEST) return "mppt_test";
    if (mode == PDS_REQUESTED_MODE_STATE_TEST) return "state_test";
    if (mode == PDS_REQUESTED_MODE_FIXED_PWM_TEST) return "fixed_pwm_test";
    if (mode == PDS_REQUESTED_MODE_MANUAL) return "manual";
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
