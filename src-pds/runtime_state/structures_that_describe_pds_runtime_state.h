#ifndef PDS_RUNTIME_STATE_H
#define PDS_RUNTIME_STATE_H

#include <stdint.h>

#include "eps_configuration_parameters.h"
#include "eps_state_machine.h"
#include "mppt_algorithm.h"
#include "board_command_contract/board_command_ids_and_payload_layouts.h"

#define PDS_CONTROL_LOOP_PERIOD_MS 100u
#define PDS_MINIMUM_STREAM_PERIOD_MS 100u
#define PDS_MAXIMUM_STREAM_PERIOD_MS 10000u
#define PDS_STATUS_PAYLOAD_VERSION 4u

#define PDS_PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE 5u
#define PDS_PANEL_RAW_ADC_TO_MILLIAMPS_SCALE 2u
#define PDS_LOAD_ENABLE_MASK_ALL_LOADS 0x1Fu
#define PDS_SAFE_REASON_INJECTED_FAULT 4u

typedef struct {
    uint32_t loop_count;
    uint16_t panel_voltage_in_millivolts;
    uint16_t panel_current_in_milliamps;
    uint32_t panel_power_in_milliwatts;
    uint8_t mppt_input_sample_is_valid;
    uint16_t mppt_duty_cycle_as_fraction_of_65535;
    uint16_t state_machine_duty_cycle_as_fraction_of_65535;
    uint16_t requested_pwm_duty_cycle_as_fraction_of_65535;
    uint16_t applied_pwm_duty_cycle_as_fraction_of_65535;
    uint8_t pwm_output_is_enabled;
    uint8_t pcu_mode;
    uint8_t safe_mode_is_active;
    uint8_t safe_mode_reason;
    uint8_t panel_efuse_is_enabled;
    uint8_t heater_is_enabled;
    uint8_t safe_mode_alert_for_obc;
    uint8_t load_enable_mask;

    /* Manual mode readbacks. The status LED level reflects whatever the
     * manual runner last wrote to PB22; the four eFuse status bits are
     * sampled live from PA18-PA21 every loop iteration so the UI can
     * confirm whether the TPS25940 actually accepted each enable request. */
    uint8_t status_led_is_on;
    uint8_t pv_efuse_power_good;
    uint8_t pv_efuse_fault_active;
    uint8_t bat_efuse_power_good;
    uint8_t bat_efuse_fault_active;
} pds_runtime_snapshot_type;

typedef struct {
    uint8_t requested_mode;
    uint8_t mppt_input_source;
    uint16_t fixed_pwm_duty_cycle_as_fraction_of_65535;

    /* Operator-requested values written by the set_manual_* commands and
     * applied to hardware every loop iteration while the firmware is in
     * PDS_REQUESTED_MODE_MANUAL. They have no effect outside that mode. */
    uint16_t manual_pwm_duty_cycle_as_fraction_of_65535;
    uint8_t manual_pv_switch_requested;
    uint8_t manual_bat_switch_requested;
    uint8_t manual_status_led_requested;

    pds_state_demo_inputs_type injected_state_inputs;

    uint8_t telemetry_stream_is_enabled;
    uint16_t telemetry_stream_period_ms;
    uint32_t telemetry_field_mask;
    uint32_t last_stream_timestamp_ms;
    uint8_t stream_sequence_number;

    uint32_t valid_chips_frame_count;
    uint32_t crc_error_count;
    uint32_t too_long_frame_count;
    uint32_t executed_command_count;
    uint8_t last_command_id;
    uint8_t last_command_status;

    struct mppt_algorithm_state mppt_algorithm_state;
    struct eps_state_machine_persistent_state state_machine_state;
    struct eps_configuration_thresholds thresholds;

    uint32_t last_control_loop_timestamp_ms;
    pds_runtime_snapshot_type snapshot;
} pds_runtime_state_type;

#endif /* PDS_RUNTIME_STATE_H */
