// =============================================================================
// eps_state_machine.h
// Public interface for the EPS power management state machine.
//
// This module implements the complete EPS decision logic:
//   - PCU operating modes: MPPT_CHARGE, CV_FLOAT, SA_LOAD_FOLLOW,
//     BATTERY_DISCHARGE (mission doc p.100-103, Figures 3.4.6-3.4.10)
//   - Safe mode with 4 sub-states (mission doc p.24-25, Section 1.7.4.2)
//   - Thermal control: heater, charging prohibition below 0C (p.25)
//   - Load shedding: priority-based eFuse control (p.25, Figure 1.7.1)
//   - OBC heartbeat monitoring with 120s autonomy timeout (p.125)
//
// This module is pure logic (Category 1). It has zero hardware dependencies.
// It receives sensor readings as integers, and returns actuator commands.
// Given the same sequence of inputs, it always produces the same outputs.
//
// The state machine calls mppt_algorithm_run_one_iteration() from
// mppt_algorithm.h when it needs Incremental Conductance tracking.
// It does NOT reimplement or modify the MPPT algorithm.
//
// Category: PURE LOGIC (no hardware)
// =============================================================================

#ifndef EPS_STATE_MACHINE_H
#define EPS_STATE_MACHINE_H

#include <stdint.h>

#include "mppt_algorithm.h"
#include "eps_configuration_parameters.h"

// =============================================================================
// Enums — all values explicitly assigned per conventions.md Rule C15
// =============================================================================

// ── PCU operating modes ─────────────────────────────────────────────────────
//
// The EPS autonomously selects one of these four modes based on solar
// availability and battery state. See mission doc p.100, Section 3.4.2.2.4.

enum eps_pcu_operating_mode {
    EPS_PCU_MODE_MPPT_CHARGE       = 0,   // Sun available, battery not full
    EPS_PCU_MODE_CV_FLOAT          = 1,   // Sun available, battery full/nearly full
    EPS_PCU_MODE_SA_LOAD_FOLLOW    = 2,   // Sun available, battery full, follow load
    EPS_PCU_MODE_BATTERY_DISCHARGE = 3    // No sun, battery powers everything
};

// ── CV_FLOAT internal sub-state ─────────────────────────────────────────────
//
// CV_FLOAT has a temporary sub-state for handling transient load spikes.
// See mission doc p.102, Figure 3.4.8.

enum eps_cv_float_substate {
    EPS_CV_FLOAT_SUBSTATE_NORMAL    = 0,  // Normal constant-voltage regulation
    EPS_CV_FLOAT_SUBSTATE_TEMP_MPPT = 1   // Temporary MPPT for load spike
};

// ── Satellite operating modes (commanded by OBC) ────────────────────────────
//
// The OBC decides which satellite mode is active and communicates it to the
// EPS via UART/CHIPS polling. The EPS does NOT decide these modes.
// See mission doc p.20-25, Section 1.7.

enum eps_satellite_operating_mode {
    EPS_SATELLITE_MODE_MEASUREMENT       = 0,
    EPS_SATELLITE_MODE_CHARGING          = 1,
    EPS_SATELLITE_MODE_UHF_COMMUNICATION = 2,
    EPS_SATELLITE_MODE_SAFE              = 3
};

// ── Safe mode sub-states ────────────────────────────────────────────────────
//
// Safe mode is NOT a single static state. It has 4 dynamic sub-states, each
// requiring a different load configuration from the EPS.
// See mission doc p.24, Section 1.7.4.2.
//
// The OBC communicates the current sub-state. If the OBC is dead (120s
// timeout), the EPS determines the sub-state autonomously:
//   - Battery < BminCrit → CHARGING
//   - Otherwise → COMMUNICATION (try to beacon)

enum eps_safe_mode_sub_state {
    EPS_SAFE_SUB_STATE_DETUMBLING    = 0,  // Restore attitude stability
    EPS_SAFE_SUB_STATE_CHARGING      = 1,  // Battery critically low
    EPS_SAFE_SUB_STATE_COMMUNICATION = 2,  // Lost ground contact
    EPS_SAFE_SUB_STATE_REBOOT        = 3   // Periodic subsystem reboots
};

// ── Load identifiers ────────────────────────────────────────────────────────
//
// Ordered from lowest priority (shed first) to highest (shed last).
// Source: mission doc p.25, Figure 1.7.1 (p.24).

enum eps_load_identifier {
    EPS_LOAD_SPAD_CAMERA   = 0,   // Lowest priority — shed first
    EPS_LOAD_GNSS_RECEIVER = 1,
    EPS_LOAD_UHF_RADIO     = 2,
    EPS_LOAD_ADCS          = 3,
    EPS_LOAD_OBC           = 4,   // Highest priority — never shed
    EPS_LOAD_COUNT         = 5
};

// ── Safe mode alert reasons ─────────────────────────────────────────────────
//
// Stored in the EPS alert log (HK_EPS_ALERT_LOG, mission doc p.36) so the
// OBC can read the reason on its next poll.

enum eps_safe_mode_reason {
    EPS_SAFE_REASON_NONE                     = 0,
    EPS_SAFE_REASON_BATTERY_BELOW_MINIMUM    = 1,
    EPS_SAFE_REASON_TEMPERATURE_OUT_OF_RANGE = 2,
    EPS_SAFE_REASON_OBC_HEARTBEAT_TIMEOUT    = 3
};

// =============================================================================
// Structs
// =============================================================================

// ── Sensor readings (input to state machine each iteration) ─────────────────
//
// Filled by the main loop from ADC readings (on real MCU) or from physics
// models (in simulation). The state machine only reads this struct.

struct eps_sensor_readings_this_iteration {

    // Battery voltage measured by ADC, converted to millivolts.
    // Range: 5000-8400 mV nominal. Fits in uint16_t.
    uint16_t battery_voltage_in_millivolts;

    // Battery current measured by INA246 sensor via I2C.
    // Positive = charging (current INTO battery).
    // Negative = discharging (current OUT of battery).
    int16_t  battery_current_in_milliamps;

    // Solar array open-circuit or loaded voltage, in millivolts.
    // Used to determine if solar power is available (>= 8200 mV).
    uint16_t solar_array_voltage_in_millivolts;

    // Raw 12-bit ADC readings for the MPPT algorithm.
    // The MPPT algorithm was designed around the ADC scale and uses
    // cross-multiplication on these raw values. Converting to millivolts
    // and back would lose precision.
    uint16_t solar_array_voltage_raw_adc_reading;
    uint16_t solar_array_current_raw_adc_reading;

    // Charging rail voltage (buck converter output / battery bus).
    // Used in CV_FLOAT for bang-bang voltage regulation.
    uint16_t charging_rail_voltage_in_millivolts;

    // Battery temperature from thermistor via SPI, in deci-degrees C.
    // -10.0 C = -100. 60.0 C = 600.
    int16_t  battery_temperature_in_decidegrees_celsius;

    // Set to 1 if the OBC has communicated with the EPS this iteration
    // (any CHIPS transaction counts). Set to 0 otherwise.
    uint8_t  obc_heartbeat_received_this_iteration;

    // The satellite operating mode currently commanded by the OBC.
    // One of enum eps_satellite_operating_mode values.
    uint8_t  satellite_mode_commanded_by_obc;

    // The safe mode sub-state commanded by the OBC (only meaningful
    // when satellite_mode_commanded_by_obc == EPS_SATELLITE_MODE_SAFE).
    // One of enum eps_safe_mode_sub_state values.
    uint8_t  safe_mode_sub_state_commanded_by_obc;
};

// ── Actuator commands (output from state machine each iteration) ────────────
//
// Written by the state machine. The main loop reads this struct and applies
// the commands to hardware (on real MCU) or to simulation models (in sim).

struct eps_actuator_output_commands {

    // Duty cycle for the buck converter PWM, as a fraction of 65535.
    // 0 = 0%, 65535 = 100%. Clamped to [5%, 95%] by the state machine.
    uint16_t buck_converter_duty_cycle_as_fraction_of_65535;

    // 1 = solar panel eFuse should be closed (panel connected to converter).
    // 0 = solar panel eFuse should be open (panel disconnected).
    uint8_t  panel_efuse_should_be_enabled;

    // 1 = battery heater should be turned on.
    // 0 = battery heater should be off.
    uint8_t  heater_should_be_enabled;

    // Per-load enable flags. 1 = load powered on, 0 = load shed.
    // Indexed by enum eps_load_identifier.
    uint8_t  load_enable_flags[EPS_LOAD_COUNT];

    // 1 = the EPS has detected a condition that warrants safe mode.
    // This flag is stored in HK_EPS_ALERT_LOG for the OBC to read.
    // The EPS has ALREADY taken local protective action.
    uint8_t  safe_mode_alert_flag_for_obc;

    // The reason for the safe mode alert.
    // One of enum eps_safe_mode_reason values.
    uint8_t  safe_mode_alert_reason;

    // Current PCU mode for telemetry reporting to OBC.
    uint8_t  current_pcu_mode_for_telemetry;
};

// ── State machine persistent state ──────────────────────────────────────────
//
// All internal state that persists between iterations. Passed by pointer
// per conventions.md Rule B2. No globals.

struct eps_state_machine_persistent_state {

    // ── Current modes ───────────────────────────────────────────────────
    uint8_t  current_pcu_operating_mode;          // enum eps_pcu_operating_mode
    uint8_t  current_satellite_mode_from_obc;     // enum eps_satellite_operating_mode
    uint8_t  safe_mode_is_active;                 // 1 = in safe mode, 0 = normal
    uint8_t  current_safe_mode_sub_state;         // enum eps_safe_mode_sub_state
    uint8_t  state_machine_has_been_initialized;  // 1 after initialize() called

    // ── CV_FLOAT internal sub-state ─────────────────────────────────────
    uint8_t  cv_float_current_substate;           // enum eps_cv_float_substate

    // ── MPPT algorithm state (embedded, not separate) ───────────────────
    struct mppt_algorithm_state mppt_algorithm_persistent_state;

    // ── Timeout counters (iteration counts) ─────────────────────────────
    uint32_t iterations_in_current_pcu_mode;
    uint32_t iterations_with_battery_voltage_below_charge_resume;
    uint32_t iterations_since_last_obc_heartbeat;

    // ── Current duty cycle ──────────────────────────────────────────────
    uint16_t current_duty_cycle_as_fraction_of_65535;

    // ── Load shedding state ─────────────────────────────────────────────
    uint8_t  load_is_currently_enabled[EPS_LOAD_COUNT];
};

// =============================================================================
// Public functions
// =============================================================================

// Sets all state to known initial values. Calls mppt_algorithm_initialize()
// on the embedded MPPT state. Sets the PCU mode to initial_pcu_operating_mode.
// For LEOP, pass EPS_PCU_MODE_BATTERY_DISCHARGE (solar panels stowed at boot,
// mission doc p.27). For simulation, pass whatever the scenario requires.
// Enables all loads. Sets state_machine_has_been_initialized to 1.
void eps_state_machine_initialize(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    uint8_t initial_pcu_operating_mode);

// Runs one iteration of the EPS state machine. Called once per superloop
// iteration on the real MCU, or once per simulation step.
//
// Reads sensor data, runs PCU mode logic, checks safe mode conditions,
// applies thermal control, and writes actuator commands.
//
// This function has no side effects beyond modifying *eps_persistent_state
// and writing to *actuator_commands_output.
void eps_state_machine_run_one_iteration(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output);

#endif // EPS_STATE_MACHINE_H
