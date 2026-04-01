// =============================================================================
// solar_panel_simulator.h
// Simulates a solar panel array using the five-parameter single-diode model.
//
// This file runs on the PC only. It uses floating-point math to model the
// physics of the solar panel accurately. It never runs on the chip.
//
// The model is parameterized from the Azur Space 3G30C triple-junction GaAs
// solar cell datasheet:
//   https://www.azurspace.com/media/uploads/file_links/file/
//   bdb_00010891-01-00_tj3g30-advanced_4x8.pdf
//
// Category: SIMULATION (PC only, float math)
// =============================================================================

#ifndef SOLAR_PANEL_SIMULATOR_H
#define SOLAR_PANEL_SIMULATOR_H

#include <stdint.h>

// ── Array configuration modes ────────────────────────────────────────────────
//
// The CHESS document says "2x2 (two parallel, two in series)" but the
// system-level voltage (18.34V max) only matches a 4P configuration.
// Both modes are provided so the user can compare and verify.

#define PANEL_CONFIG_4P     0  // 4 panels in parallel, 7 cells in series each
#define PANEL_CONFIG_2S2P   1  // 2 panels in series (14 cells), 2 strings parallel

// ── Solar panel parameters ───────────────────────────────────────────────────
//
// All values are at the array level for the selected configuration.
// The struct is filled by the initialization function based on the
// chosen configuration mode.

struct solar_panel_parameters {
    double open_circuit_voltage_at_reference_temperature_volts;
    double short_circuit_current_at_reference_temperature_amps;
    double voltage_temperature_coefficient_volts_per_kelvin;
    double current_temperature_coefficient_amps_per_kelvin;
    double reference_temperature_kelvin;
    double reference_irradiance_watts_per_square_meter;
    int32_t number_of_cells_in_series;
    int32_t number_of_strings_in_parallel;
    double diode_ideality_factor;
    double series_resistance_ohms;
    double shunt_resistance_ohms;
};

// ── Public functions ─────────────────────────────────────────────────────────

// Fills the parameters struct with values for the selected configuration.
//   panel_configuration_mode: PANEL_CONFIG_4P or PANEL_CONFIG_2S2P
void solar_panel_initialize_parameters(
    struct solar_panel_parameters *parameters,
    int32_t panel_configuration_mode);

// Computes the panel current for a given panel voltage, temperature, and
// irradiance level, using the five-parameter single-diode model.
//
// Uses Newton-Raphson iteration internally to solve the implicit equation.
//
//   parameters           — the panel parameters struct
//   panel_voltage_volts  — the voltage imposed on the panel by the converter
//   temperature_celsius  — the panel temperature in degrees Celsius
//   irradiance_fraction  — fraction of full sun (0.0 = dark, 1.0 = full AM0)
//
// Returns: the panel current in amps at the given operating point.
double solar_panel_compute_current_at_voltage(
    const struct solar_panel_parameters *parameters,
    double panel_voltage_volts,
    double temperature_celsius,
    double irradiance_fraction);

// Computes the theoretical maximum power point (MPP) by scanning the I-V
// curve and finding the voltage where P = V × I is maximized.
//
// This is used as the reference value to check whether the MPPT algorithm
// converges correctly.
//
//   parameters           — the panel parameters struct
//   temperature_celsius  — the panel temperature in degrees Celsius
//   irradiance_fraction  — fraction of full sun (0.0 = dark, 1.0 = full AM0)
//   out_mpp_voltage      — output: the voltage at the MPP (volts)
//   out_mpp_current      — output: the current at the MPP (amps)
//   out_mpp_power        — output: the power at the MPP (watts)
void solar_panel_find_maximum_power_point(
    const struct solar_panel_parameters *parameters,
    double temperature_celsius,
    double irradiance_fraction,
    double *out_mpp_voltage,
    double *out_mpp_current,
    double *out_mpp_power);

#endif // SOLAR_PANEL_SIMULATOR_H
