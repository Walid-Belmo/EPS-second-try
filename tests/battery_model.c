// =============================================================================
// battery_model.c
// Simple battery simulation model for EPS state machine testing.
//
// Models a 2S2P 18650 lithium-ion pack. The open-circuit voltage is
// approximated as a piecewise-linear function of state of charge, based on
// typical 18650 discharge curves from datasheets.
//
// This is intentionally simple — accuracy within ~5% is sufficient for
// testing the state machine logic. The goal is exercising mode transitions,
// not predicting exact battery behavior.
//
// Category: SIMULATION (PC only)
// =============================================================================

#include <stdint.h>

#include "../src/assertion_handler.h"
#include "battery_model.h"

// ── Public: initialize with defaults for CHESS 2S2P pack ────────────────────

void battery_model_initialize_parameters(
    struct battery_model_parameters *battery_parameters,
    double initial_state_of_charge_fraction)
{
    SATELLITE_ASSERT(battery_parameters != (void *)0);
    SATELLITE_ASSERT(initial_state_of_charge_fraction >= 0.0);
    SATELLITE_ASSERT(initial_state_of_charge_fraction <= 1.0);

    // 2S2P 18650 pack: 2 cells in series, 2 strings in parallel
    // Total capacity: 2 × 3.0 Ah = 6.0 Ah (two parallel strings)
    // Voltage range: 2 × 3.0V = 6.0V (empty) to 2 × 4.2V = 8.4V (full)
    // Source: mission doc Table 3.4.1 (p.94)
    battery_parameters->capacity_in_amp_hours = 6.0;
    battery_parameters->internal_resistance_in_ohms = 0.1;
    battery_parameters->current_state_of_charge_fraction =
        initial_state_of_charge_fraction;
    battery_parameters->minimum_cell_voltage_volts = 3.0;
    battery_parameters->maximum_cell_voltage_volts = 4.2;
    battery_parameters->number_of_cells_in_series = 2;
    battery_parameters->number_of_strings_in_parallel = 2;
}

// ── Public: open-circuit voltage from SOC ───────────────────────────────────
//
// Piecewise-linear approximation of a typical 18650 cell Voc(SOC):
//   SOC 0.00 → 3.00V per cell
//   SOC 0.10 → 3.30V per cell
//   SOC 0.20 → 3.50V per cell
//   SOC 0.50 → 3.70V per cell
//   SOC 0.80 → 3.90V per cell
//   SOC 0.90 → 4.05V per cell
//   SOC 1.00 → 4.20V per cell
//
// Pack voltage = cell voltage × number_of_cells_in_series

static double interpolate_cell_voltage_from_state_of_charge(
    double state_of_charge_fraction)
{
    // Breakpoints: (SOC, cell_voltage_volts)
    // 7 breakpoints defining the piecewise-linear curve
    static const double soc_breakpoints[7] =
        { 0.00, 0.10, 0.20, 0.50, 0.80, 0.90, 1.00 };
    static const double voltage_breakpoints_volts[7] =
        { 3.00, 3.30, 3.50, 3.70, 3.90, 4.05, 4.20 };

    double soc = state_of_charge_fraction;

    if (soc <= 0.0) {
        return voltage_breakpoints_volts[0];
    }
    if (soc >= 1.0) {
        return voltage_breakpoints_volts[6];
    }

    // Find the segment
    for (int32_t segment_index = 0; segment_index < 6; segment_index += 1) {
        double soc_low = soc_breakpoints[segment_index];
        double soc_high = soc_breakpoints[segment_index + 1];

        if (soc >= soc_low && soc <= soc_high) {
            double fraction_within_segment =
                (soc - soc_low) / (soc_high - soc_low);
            double voltage_low = voltage_breakpoints_volts[segment_index];
            double voltage_high = voltage_breakpoints_volts[segment_index + 1];

            return voltage_low
                   + (fraction_within_segment * (voltage_high - voltage_low));
        }
    }

    // Should not reach here
    return voltage_breakpoints_volts[6];
}

double battery_model_compute_open_circuit_voltage_from_state_of_charge(
    const struct battery_model_parameters *battery_parameters)
{
    SATELLITE_ASSERT(battery_parameters != (void *)0);
    SATELLITE_ASSERT(battery_parameters->current_state_of_charge_fraction >= 0.0);
    SATELLITE_ASSERT(battery_parameters->current_state_of_charge_fraction <= 1.0);

    double cell_voltage = interpolate_cell_voltage_from_state_of_charge(
        battery_parameters->current_state_of_charge_fraction);

    double pack_voltage =
        cell_voltage * (double)battery_parameters->number_of_cells_in_series;

    return pack_voltage;
}

// ── Public: terminal voltage under load ─────────────────────────────────────

double battery_model_compute_terminal_voltage_under_load(
    const struct battery_model_parameters *battery_parameters,
    double current_in_amps)
{
    SATELLITE_ASSERT(battery_parameters != (void *)0);

    double open_circuit_voltage =
        battery_model_compute_open_circuit_voltage_from_state_of_charge(
            battery_parameters);

    // V_terminal = V_oc - I × R_internal
    // When charging (I > 0): terminal voltage is HIGHER than Voc
    // When discharging (I < 0): terminal voltage is LOWER than Voc
    // Note: the sign convention here means I × R is subtracted,
    // so charging raises voltage and discharging lowers it.
    double terminal_voltage =
        open_circuit_voltage
        - (current_in_amps * battery_parameters->internal_resistance_in_ohms);

    // Clamp to physical limits
    double minimum_pack_voltage =
        battery_parameters->minimum_cell_voltage_volts
        * (double)battery_parameters->number_of_cells_in_series;
    double maximum_pack_voltage =
        battery_parameters->maximum_cell_voltage_volts
        * (double)battery_parameters->number_of_cells_in_series;

    if (terminal_voltage < minimum_pack_voltage) {
        terminal_voltage = minimum_pack_voltage;
    }
    if (terminal_voltage > maximum_pack_voltage) {
        terminal_voltage = maximum_pack_voltage;
    }

    return terminal_voltage;
}

// ── Public: update state of charge ──────────────────────────────────────────

void battery_model_update_state_of_charge(
    struct battery_model_parameters *battery_parameters,
    double current_in_amps,
    double time_step_in_seconds)
{
    SATELLITE_ASSERT(battery_parameters != (void *)0);
    SATELLITE_ASSERT(time_step_in_seconds > 0.0);

    // charge_delta_amp_hours = current × time / 3600
    // SOC change = charge_delta / capacity
    double charge_delta_amp_hours =
        current_in_amps * time_step_in_seconds / 3600.0;

    double soc_change =
        charge_delta_amp_hours / battery_parameters->capacity_in_amp_hours;

    battery_parameters->current_state_of_charge_fraction += soc_change;

    // Clamp SOC to [0, 1]
    if (battery_parameters->current_state_of_charge_fraction < 0.0) {
        battery_parameters->current_state_of_charge_fraction = 0.0;
    }
    if (battery_parameters->current_state_of_charge_fraction > 1.0) {
        battery_parameters->current_state_of_charge_fraction = 1.0;
    }
}
