// =============================================================================
// battery_model.h
// Simple battery simulation model for the EPS state machine testing.
//
// Models a 2S2P 18650 lithium-ion battery pack as used in the CHESS CubeSat.
// Tracks state of charge (SOC) and computes terminal voltage as a function
// of SOC and load current.
//
// This is a PC-only simulation model using floating-point math.
// It is NOT compiled for the SAMD21 target.
//
// Battery specs from mission doc Table 3.4.1 (p.94):
//   Configuration: 2S2P (2 series, 2 parallel)
//   Capacity: ~6 Ah (~43 Wh)
//   Voltage range: 6V - 8.4V (2 cells in series × 3.0-4.2V per cell)
//
// Category: SIMULATION (PC only)
// =============================================================================

#ifndef BATTERY_MODEL_H
#define BATTERY_MODEL_H

#include <stdint.h>

struct battery_model_parameters {
    double capacity_in_amp_hours;
    double internal_resistance_in_ohms;
    double current_state_of_charge_fraction;  // 0.0 = empty, 1.0 = full
    double minimum_cell_voltage_volts;        // per cell (e.g., 3.0V)
    double maximum_cell_voltage_volts;        // per cell (e.g., 4.2V)
    int32_t number_of_cells_in_series;        // 2 for 2S2P
    int32_t number_of_strings_in_parallel;    // 2 for 2S2P
};

// Sets up the battery model with default parameters for the CHESS 2S2P pack.
// initial_state_of_charge_fraction: 0.0 = empty, 1.0 = full.
void battery_model_initialize_parameters(
    struct battery_model_parameters *battery_parameters,
    double initial_state_of_charge_fraction);

// Computes the open-circuit voltage (no load) based on current SOC.
// Uses a piecewise-linear approximation of a typical 18650 discharge curve.
// Returns the pack voltage (2 cells in series).
double battery_model_compute_open_circuit_voltage_from_state_of_charge(
    const struct battery_model_parameters *battery_parameters);

// Computes the terminal voltage under load.
// V_terminal = V_oc(SOC) - I_load × R_internal
// current_in_amps: positive = charging, negative = discharging
double battery_model_compute_terminal_voltage_under_load(
    const struct battery_model_parameters *battery_parameters,
    double current_in_amps);

// Updates the state of charge based on current flow over one time step.
// current_in_amps: positive = charging (SOC increases),
//                  negative = discharging (SOC decreases)
// time_step_in_seconds: duration of this simulation step
void battery_model_update_state_of_charge(
    struct battery_model_parameters *battery_parameters,
    double current_in_amps,
    double time_step_in_seconds);

#endif // BATTERY_MODEL_H
