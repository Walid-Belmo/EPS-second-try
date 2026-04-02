// =============================================================================
// eps_configuration_parameters.h
// All configurable thresholds for the EPS state machine in one struct.
//
// Every threshold that influences a state machine decision lives here.
// No magic numbers anywhere else. Values are placeholders until finalized
// by the battery team, thermal team, and mission operations.
//
// Category: PURE LOGIC (no hardware)
// =============================================================================

#ifndef EPS_CONFIGURATION_PARAMETERS_H
#define EPS_CONFIGURATION_PARAMETERS_H

#include <stdint.h>

// =============================================================================
// Configuration thresholds struct
//
// All voltage thresholds are in millivolts (mV), uint16_t, range 0-65535.
// All current thresholds are in milliamps (mA), int16_t, signed because
//   battery current is positive when charging and negative when discharging.
// All temperature thresholds are in deci-degrees Celsius (0.1 C), int16_t,
//   so -10.0 C is represented as -100.
// All timeout thresholds are in iteration counts, uint32_t.
//
// Sources for placeholder values are listed per field. "TBD" means the value
// will be set by another team (battery, thermal, ops) before flight.
// =============================================================================

struct eps_configuration_thresholds {

    // ── Battery voltage thresholds (millivolts) ─────────────────────────
    //
    // These define the boundaries between PCU operating modes and safe mode
    // entry conditions. See mission doc p.99 for hardware limits and
    // Figures 3.4.6-3.4.10 for how they drive mode transitions.

    // Maximum allowed battery voltage. Above this, hardware comparator
    // (LM139) cuts the buck converter independently of software.
    // Source: mission doc p.99 — "Battery: 8.4 V maximum"
    uint16_t battery_voltage_maximum_in_millivolts;                     // 8400

    // Voltage at which we consider the battery "full" and transition
    // from MPPT_CHARGE to SA_LOAD_FOLLOW (Figure 3.4.6, p.101).
    // Placeholder: 100 mV below max. TBD by battery team.
    uint16_t battery_voltage_full_threshold_in_millivolts;              // 8300

    // Voltage below which CV_FLOAT should resume active MPPT charging.
    // If battery voltage stays below this for t2 iterations while in
    // CV_FLOAT, transition to MPPT_CHARGE (Figure 3.4.8, p.102).
    // Placeholder: 300 mV below max. TBD by battery team.
    uint16_t battery_voltage_charge_resume_threshold_in_millivolts;     // 8100

    // Minimum operational battery voltage. Below this, the EPS flags a
    // safe mode alert and takes protective action.
    // Source: mission doc p.99 — "Battery: 5 V minimum"
    uint16_t battery_voltage_minimum_in_millivolts;                     // 5000

    // Critical battery level for safe mode CHARGING sub-state.
    // Below this, maximum load shedding and priority solar charging.
    // Placeholder: 500 mV above minimum. TBD by battery team.
    uint16_t battery_voltage_critical_in_millivolts;                    // 5500

    // Hysteresis margin to prevent rapid mode switching when battery
    // voltage is near a threshold. Used in the top-level decision tree
    // (Figure 3.4.6: "Vbat < Vbat_max - D") and in BATTERY_DISCHARGE
    // safe mode check (Figure 3.4.10: "Vbat < Vbat_min + D").
    // Placeholder: 200 mV. TBD.
    uint16_t battery_voltage_hysteresis_margin_in_millivolts;           // 200

    // ── Battery current thresholds (milliamps, signed) ──────────────────
    //
    // Positive current = charging (current flowing INTO battery).
    // Negative current = discharging (current flowing OUT of battery).

    // Maximum allowed charging current. If exceeded in MPPT_CHARGE,
    // duty cycle is reduced to protect the battery (Figure 3.4.7, p.101).
    // Placeholder: TBD by battery team based on cell datasheet.
    int16_t battery_current_maximum_charge_in_milliamps;                // 2000

    // Maximum allowed discharge current (negative value). If exceeded in
    // BATTERY_DISCHARGE, load shedding is triggered (Figure 3.4.10, p.103).
    // Also used in CV_FLOAT to detect large transient loads that require
    // entering TEMP_MPPT sub-state (Figure 3.4.8, p.102).
    // Placeholder: TBD by battery team.
    int16_t battery_current_maximum_discharge_in_milliamps;             // -2000

    // Minimum charging current threshold for SA_LOAD_FOLLOW. If battery
    // current drops below this, duty cycle is clamped to prevent net
    // charging (Figure 3.4.9, p.102).
    // Placeholder: TBD.
    int16_t battery_current_minimum_charge_threshold_in_milliamps;      // 100

    // ── Solar array threshold (millivolts) ──────────────────────────────

    // Minimum solar array voltage to consider solar power available.
    // Below this, the buck converter cannot operate (Table 3.4.1, p.94:
    // "Input Voltage (PV): 8.2V - 18.34V"). This is our engineering
    // inference — the mission doc does not explicitly define "solar
    // available."
    uint16_t solar_array_minimum_voltage_for_availability_in_millivolts; // 8200

    // ── Temperature thresholds (deci-degrees Celsius) ───────────────────
    //
    // Represented as int16_t in units of 0.1 C. For example:
    //   -10.0 C = -100 deci-degrees
    //    60.0 C =  600 deci-degrees
    //     0.0 C =    0 deci-degrees

    // Below this temperature, the battery heater is turned ON.
    // Source: mission doc p.25 — "If temperature falls below TempMin,
    // heaters are activated."
    // Placeholder: -10.0 C. TBD by thermal team.
    int16_t temperature_minimum_for_heater_activation_in_decidegrees;   // -100

    // Above this temperature, non-essential loads are shed to reduce heat.
    // Source: mission doc p.25 — "If temperature exceeds TempMax,
    // non-essential systems remain OFF to prevent overheating."
    // Placeholder: 60.0 C. TBD by thermal team.
    int16_t temperature_maximum_for_load_shedding_in_decidegrees;       // 600

    // Below 0 C, lithium-ion batteries must NOT be charged. Charging
    // below freezing causes lithium plating on the anode, permanently
    // damaging the battery. This is a hard safety override that forces
    // the duty cycle to minimum regardless of PCU mode.
    // Value: 0.0 C = 0 deci-degrees. This is a physics constraint,
    // not a configurable parameter.
    int16_t temperature_minimum_for_charging_allowed_in_decidegrees;    // 0

    // ── Timeout thresholds (iteration counts) ───────────────────────────
    //
    // All timeouts are in units of superloop iterations. The real-time
    // duration depends on the firmware loop period (approximately 200 us
    // at 48 MHz with ADC reads). To convert seconds to iterations:
    //   iterations = seconds / loop_period
    // For example: 120 seconds / 0.0002 s = 600000 iterations.

    // MPPT_CHARGE timeout. If the battery has not shown sufficient charge
    // buffer after this many iterations, transition to CV_FLOAT.
    // Source: mission doc p.101 — "If after a timeout period the battery
    // still shows insufficient charge buffer, transitions to CV_FLOAT."
    // Placeholder: TBD.
    uint32_t mppt_charge_timeout_for_insufficient_buffer_in_iterations;

    // CV_FLOAT low-voltage wait. If battery voltage stays below Vbat_CHG
    // for this many iterations while in CV_FLOAT, transition to
    // MPPT_CHARGE. Source: Figure 3.4.8 (p.102), labeled "t2".
    // Placeholder: TBD.
    uint32_t cv_float_low_voltage_wait_timeout_in_iterations;

    // OBC heartbeat timeout. If the EPS has not received any communication
    // from the OBC for this many iterations, the EPS assumes the OBC is
    // dead and enters autonomous safe mode.
    // Source: mission doc p.125 — "After 120 seconds without any message
    // from OBC, the subsystem may assume autonomy."
    // Value: 120 seconds / loop_period.
    uint32_t obc_heartbeat_timeout_in_iterations;

    // ── CV_FLOAT duty cycle regulation ──────────────────────────────────

    // Step size for the bang-bang controller in CV_FLOAT mode. Each
    // iteration, the duty cycle is increased or decreased by this amount
    // to regulate the charging rail voltage at Vbat_max.
    // Placeholder: ~0.25% of 65535 = 164. TBD.
    uint16_t cv_float_duty_cycle_adjustment_step_size;                  // 164
};

#endif // EPS_CONFIGURATION_PARAMETERS_H
