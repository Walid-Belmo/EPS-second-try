#ifndef PDS_BOARD_COMMANDS_H
#define PDS_BOARD_COMMANDS_H

#include <stdint.h>

/*
 * These are the CHIPS command numbers understood by the board firmware.
 *
 * The computer never sends these bytes directly. The computer sends readable
 * text to the ESP32, and the ESP32 converts that text into these CHIPS
 * commands before talking to the SAMD21 board.
 */

#define PDS_BOARD_COMMAND_OFF                 0x30u
#define PDS_BOARD_COMMAND_RUN_PWM             0x31u
#define PDS_BOARD_COMMAND_START_MPPT_DEMO     0x32u
#define PDS_BOARD_COMMAND_START_STATE_DEMO    0x33u
#define PDS_BOARD_COMMAND_INJECT_STATE_INPUTS 0x34u
#define PDS_BOARD_COMMAND_GET_VALUES          0x35u
#define PDS_BOARD_COMMAND_STREAM_VALUES       0x36u

/*
 * Fixed byte lengths let the board reject broken commands before any RAM value
 * is changed. The state input payload is 17 bytes, matching the proven demo
 * payload used in src/eps_demo_chips_command_dispatch.c.
 */

#define PDS_RUN_PWM_PAYLOAD_LENGTH_IN_BYTES          2u
#define PDS_MPPT_DEMO_PAYLOAD_LENGTH_IN_BYTES       17u
#define PDS_STATE_INPUT_PAYLOAD_LENGTH_IN_BYTES      17u
#define PDS_GET_VALUES_PAYLOAD_LENGTH_IN_BYTES        4u
#define PDS_STREAM_VALUES_PAYLOAD_LENGTH_IN_BYTES     7u

#define PDS_CURVE_TYPE_QUADRATIC 0u

/*
 * Field masks are used by get_values and stream_values.
 * Version 1 still sends one fixed status packet, but storing the mask now lets
 * the ESP32 and later UI ask for smaller packets without changing commands.
 */

#define PDS_VALUE_FIELD_MODE          (1u << 0)
#define PDS_VALUE_FIELD_PWM           (1u << 1)
#define PDS_VALUE_FIELD_STATE         (1u << 2)
#define PDS_VALUE_FIELD_LOADS         (1u << 3)
#define PDS_VALUE_FIELD_FAULTS        (1u << 4)
#define PDS_VALUE_FIELD_MPPT          (1u << 5)
#define PDS_VALUE_FIELD_INPUTS        (1u << 6)
#define PDS_VALUE_FIELD_COMMANDS      (1u << 7)
#define PDS_VALUE_FIELD_ALL           0xFFFFFFFFu

/*
 * Coefficients for the quadratic MPPT demo use this scale:
 *
 *   current_mA = A * voltage_V^2 + B * voltage_V + C
 *
 * The ESP32 sends A and B multiplied by 1000, so the SAMD21 can compute the
 * curve with integer arithmetic and no floating point dependency.
 */

#define PDS_QUADRATIC_COEFFICIENT_SCALE 1000l

typedef enum {
    PDS_REQUESTED_MODE_OFF = 0,
    PDS_REQUESTED_MODE_FLIGHT = 1,
    PDS_REQUESTED_MODE_MPPT_TEST = 2,
    PDS_REQUESTED_MODE_STATE_TEST = 3,
    PDS_REQUESTED_MODE_FIXED_PWM_TEST = 4
} pds_requested_mode_type;

typedef struct {
    uint16_t battery_voltage_in_millivolts;
    int16_t battery_current_in_milliamps;
    uint16_t panel_voltage_in_millivolts;
    uint16_t panel_current_in_milliamps;
    uint16_t charging_rail_voltage_in_millivolts;
    int16_t battery_temperature_in_decidegrees_celsius;
    uint8_t heartbeat_received;
    uint8_t satellite_mode_from_obc;
    uint8_t safe_mode_substate_from_obc;
    uint16_t fault_flags;
} pds_state_demo_inputs_type;

typedef struct {
    uint8_t curve_type;
    int32_t coefficient_a_scaled;
    int32_t coefficient_b_scaled;
    int16_t coefficient_c_in_milliamps;
    uint16_t minimum_panel_voltage_in_millivolts;
    uint16_t maximum_panel_voltage_in_millivolts;
    uint16_t battery_voltage_in_millivolts;
} pds_mppt_demo_curve_type;

#endif /* PDS_BOARD_COMMANDS_H */
