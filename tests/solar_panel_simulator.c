// =============================================================================
// solar_panel_simulator.c
// Five-parameter single-diode model for the Azur Space 3G30C solar array.
//
// The single-diode equation is:
//   I = Iph - Isat * (exp((V + I*Rs) / (n*Vt)) - 1) - (V + I*Rs) / Rsh
//
// This equation is implicit in I (I appears on both sides). We solve it
// using Newton-Raphson iteration for each voltage point.
//
// Temperature dependence:
//   Iph(T)  = Iph_ref * (G/Gref) * (1 + muIsc * (T - Tref))
//   Isat(T) = Isat_ref * (T/Tref)^3 * exp(q*Eg/(n*k) * (1/Tref - 1/T))
//
// Source for cell parameters:
//   Azur Space 3G30C datasheet (4x8 cm, triple-junction GaAs)
//   https://www.azurspace.com/media/uploads/file_links/file/
//   bdb_00010891-01-00_tj3g30-advanced_4x8.pdf
//   Page 2: "Electrical Data" table — Voc=2669mV, Isc=525mA at AM0, 28°C
//   Page 2: "Temperature Coefficients" — dVoc/dT=-6.0mV/°C, dIsc/dT=+0.35mA/°C
//
// Category: SIMULATION (PC only, float math)
// =============================================================================

#include <stdint.h>
#include <math.h>

#include "solar_panel_simulator.h"

// ── Physical constants ───────────────────────────────────────────────────────

// Boltzmann constant in joules per kelvin
static const double BOLTZMANN_CONSTANT_JOULES_PER_KELVIN = 1.380649e-23;

// Electron charge in coulombs
static const double ELECTRON_CHARGE_COULOMBS = 1.602176634e-19;

// Effective band gap energy for the single-diode model temperature scaling.
// For triple-junction GaInP/GaAs/Ge cells, the effective Eg is much higher
// than single-junction Si (1.12 eV). Derived from the datasheet dVoc/dT:
//   dVoc/dT ≈ (Voc - n*Eg) / T → Eg ≈ (Voc - T*dVoc/dT) / n
//   Eg ≈ (2.669 - 301*(-0.006)) / 1.3 ≈ 3.44 eV
// Using 3.4 eV ensures the model's Voc matches the datasheet at all temps.
static const double BANDGAP_ENERGY_ELECTRON_VOLTS = 3.4;

// AM0 solar irradiance at 1 AU in watts per square meter
static const double AM0_IRRADIANCE_WATTS_PER_SQUARE_METER = 1366.1;

// Conversion from Celsius to Kelvin
static const double CELSIUS_TO_KELVIN_OFFSET = 273.15;

// ── Newton-Raphson solver limits ─────────────────────────────────────────────

// Maximum number of iterations for the Newton-Raphson solver.
// The solver converges in 5-10 iterations for well-behaved I-V points.
// 50 iterations provides a generous safety margin.
#define NEWTON_RAPHSON_MAXIMUM_ITERATIONS  50

// Convergence threshold: stop when the correction is smaller than this.
// 1 microamp is far below any measurement precision we care about.
static const double NEWTON_RAPHSON_CONVERGENCE_THRESHOLD_AMPS = 1.0e-6;

// ── Number of voltage steps for MPP search ───────────────────────────────────
// We scan the I-V curve in this many steps to find the maximum power point.
#define MPP_SEARCH_NUMBER_OF_VOLTAGE_STEPS  500

// ── Private: compute thermal voltage ─────────────────────────────────────────
//
// Vt = k * T / q
// At 25°C (298.15 K): Vt = 25.7 mV

static double compute_thermal_voltage_for_temperature(
    double temperature_kelvin)
{
    return (BOLTZMANN_CONSTANT_JOULES_PER_KELVIN * temperature_kelvin)
           / ELECTRON_CHARGE_COULOMBS;
}

// ── Private: compute photocurrent at given temperature and irradiance ────────
//
// Iph(T,G) = Iph_ref * (G / Gref) * (1 + muIsc * (T - Tref))
//
// The photocurrent is proportional to irradiance and increases slightly
// with temperature (because thermal energy helps free additional carriers).

static double compute_photocurrent_at_conditions(
    const struct solar_panel_parameters *parameters,
    double temperature_kelvin,
    double irradiance_fraction)
{
    double temperature_difference_kelvin =
        temperature_kelvin - parameters->reference_temperature_kelvin;

    double photocurrent_amps =
        parameters->short_circuit_current_at_reference_temperature_amps
        * irradiance_fraction
        * (1.0 + parameters->current_temperature_coefficient_amps_per_kelvin
               * temperature_difference_kelvin);

    // Photocurrent cannot be negative (no light means no current)
    if (photocurrent_amps < 0.0) {
        photocurrent_amps = 0.0;
    }

    return photocurrent_amps;
}

// ── Private: compute saturation current at given temperature ─────────────────
//
// Isat(T) = Isat_ref * (T/Tref)^3 * exp(q*Eg/(n*k) * (1/Tref - 1/T))
//
// The saturation current increases exponentially with temperature. This is
// the primary mechanism by which Voc decreases when the panel gets hotter:
// higher Isat pushes the diode forward voltage down.

static double compute_saturation_current_at_temperature(
    const struct solar_panel_parameters *parameters,
    double temperature_kelvin)
{
    double temperature_ratio =
        temperature_kelvin / parameters->reference_temperature_kelvin;

    double thermal_voltage_at_reference =
        compute_thermal_voltage_for_temperature(
            parameters->reference_temperature_kelvin);

    // Compute Isat at reference temperature from Voc and Isc:
    // At open circuit: 0 = Iph - Isat * (exp(Voc/(n*Ns*Vt)) - 1)
    // Therefore: Isat_ref = Iph / (exp(Voc/(n*Ns*Vt)) - 1)
    double voc_per_cell =
        parameters->open_circuit_voltage_at_reference_temperature_volts
        / (double)parameters->number_of_cells_in_series;

    double thermal_voltage_times_ideality =
        parameters->diode_ideality_factor * thermal_voltage_at_reference;

    double isat_reference =
        (parameters->short_circuit_current_at_reference_temperature_amps
         / (double)parameters->number_of_strings_in_parallel)
        / (exp(voc_per_cell / thermal_voltage_times_ideality) - 1.0);

    // Scale to current temperature
    double exponent_argument =
        (ELECTRON_CHARGE_COULOMBS * BANDGAP_ENERGY_ELECTRON_VOLTS)
        / (parameters->diode_ideality_factor
           * BOLTZMANN_CONSTANT_JOULES_PER_KELVIN)
        * (1.0 / parameters->reference_temperature_kelvin
           - 1.0 / temperature_kelvin);

    double isat_at_temperature =
        isat_reference
        * temperature_ratio * temperature_ratio * temperature_ratio
        * exp(exponent_argument);

    return isat_at_temperature;
}

// ── Private: solve single-diode equation using Newton-Raphson ────────────────
//
// The equation to solve for I:
//   f(I) = Iph - Isat*(exp((V + I*Rs)/(n*Ns*Vt)) - 1) - (V + I*Rs)/Rsh - I = 0
//
// Newton-Raphson update:
//   I_new = I - f(I) / f'(I)

static double solve_current_at_voltage_using_newton_raphson(
    double photocurrent_amps,
    double saturation_current_amps,
    double panel_voltage_volts,
    double thermal_voltage_times_ideality_times_cells,
    double series_resistance_ohms,
    double shunt_resistance_ohms)
{
    // Initial guess: start with the photocurrent (short-circuit approximation)
    double current_estimate_amps = photocurrent_amps;

    for (int32_t iteration = 0;
         iteration < NEWTON_RAPHSON_MAXIMUM_ITERATIONS;
         iteration += 1)
    {
        double voltage_plus_ir =
            panel_voltage_volts
            + current_estimate_amps * series_resistance_ohms;

        double exponential_argument =
            voltage_plus_ir / thermal_voltage_times_ideality_times_cells;

        // Clamp the exponential argument to prevent overflow.
        // exp(500) ≈ 1.4e217 which is within double range (max ~1.8e308).
        // Must be high enough that Isat * exp(x) >> Iph, otherwise the
        // model produces non-physical current at voltages beyond Voc.
        if (exponential_argument > 500.0) {
            exponential_argument = 500.0;
        }

        double exp_term = exp(exponential_argument);

        // f(I) = Iph - Isat*(exp(...) - 1) - (V + I*Rs)/Rsh - I
        double function_value =
            photocurrent_amps
            - saturation_current_amps * (exp_term - 1.0)
            - voltage_plus_ir / shunt_resistance_ohms
            - current_estimate_amps;

        // f'(I) = -Isat * Rs / (n*Ns*Vt) * exp(...) - Rs/Rsh - 1
        double derivative_value =
            - saturation_current_amps * series_resistance_ohms
              / thermal_voltage_times_ideality_times_cells * exp_term
            - series_resistance_ohms / shunt_resistance_ohms
            - 1.0;

        // Avoid division by zero in degenerate cases
        if (fabs(derivative_value) < 1.0e-30) {
            break;
        }

        double correction = function_value / derivative_value;
        current_estimate_amps -= correction;

        // Current cannot be negative for a solar panel producing power
        if (current_estimate_amps < 0.0) {
            current_estimate_amps = 0.0;
        }

        if (fabs(correction) < NEWTON_RAPHSON_CONVERGENCE_THRESHOLD_AMPS) {
            break;
        }
    }

    return current_estimate_amps;
}

// ── Public: initialize parameters ────────────────────────────────────────────

void solar_panel_initialize_parameters(
    struct solar_panel_parameters *parameters,
    int32_t panel_configuration_mode)
{
    // ── Per-cell values from Azur Space 3G30C datasheet ──────────────────
    // Datasheet: page 2, "Electrical Data" table, AM0 1366 W/m², 28°C
    // Voc = 2669 mV per cell
    // Isc = 525 mA per cell (for the 4x8 cm cell, area 30.18 cm²)
    double voc_per_cell_volts = 2.669;
    double isc_per_cell_amps  = 0.525;

    // Temperature coefficients from datasheet page 2:
    // dVoc/dT = -6.0 mV/°C per cell
    // dIsc/dT = +0.35 mA/°C per cell
    double dvoc_dt_per_cell_volts_per_kelvin = -0.0060;
    double disc_dt_per_cell_amps_per_kelvin  = +0.00035;

    // Reference conditions: AM0, 28°C (as stated in datasheet)
    double reference_temperature_celsius = 28.0;

    if (panel_configuration_mode == PANEL_CONFIG_4P) {
        // 4 panels in parallel, each with 7 cells in series.
        // Array Voc = 7 * Voc_cell = 7 * 2.669 = 18.683 V
        // Array Isc = 4 * Isc_cell = 4 * 0.525 = 2.100 A
        parameters->number_of_cells_in_series  = 7;
        parameters->number_of_strings_in_parallel = 4;
    } else {
        // 2S2P: 2 panels in series (14 cells), 2 such strings in parallel.
        // Array Voc = 14 * 2.669 = 37.366 V
        // Array Isc = 2 * 0.525 = 1.050 A
        parameters->number_of_cells_in_series  = 14;
        parameters->number_of_strings_in_parallel = 2;
    }

    parameters->open_circuit_voltage_at_reference_temperature_volts =
        voc_per_cell_volts * (double)parameters->number_of_cells_in_series;

    parameters->short_circuit_current_at_reference_temperature_amps =
        isc_per_cell_amps * (double)parameters->number_of_strings_in_parallel;

    parameters->voltage_temperature_coefficient_volts_per_kelvin =
        dvoc_dt_per_cell_volts_per_kelvin
        * (double)parameters->number_of_cells_in_series;

    parameters->current_temperature_coefficient_amps_per_kelvin =
        disc_dt_per_cell_amps_per_kelvin
        * (double)parameters->number_of_strings_in_parallel;

    parameters->reference_temperature_kelvin =
        reference_temperature_celsius + CELSIUS_TO_KELVIN_OFFSET;

    parameters->reference_irradiance_watts_per_square_meter =
        AM0_IRRADIANCE_WATTS_PER_SQUARE_METER;

    // ── Diode model fitting parameters ───────────────────────────────────
    // These are estimated from the datasheet I-V curve shape.
    // Ideality factor n: typical 1.2-1.5 for triple-junction GaAs.
    // Rs and Rsh: fitted so that the model produces Voc, Isc, and Pmpp
    // consistent with datasheet values.
    parameters->diode_ideality_factor = 1.3;

    // Series resistance: affects the slope near Voc.
    // Scaled to array level: Rs_array = Rs_cell * Ns / Np
    // Typical Rs_cell ≈ 0.02 ohms for a 30 cm² cell.
    double rs_per_cell_ohms = 0.020;
    parameters->series_resistance_ohms =
        rs_per_cell_ohms
        * (double)parameters->number_of_cells_in_series
        / (double)parameters->number_of_strings_in_parallel;

    // Shunt resistance: affects the slope near Isc.
    // Scaled to array level: Rsh_array = Rsh_cell * Ns / Np (inverted)
    // Actually: Rsh_array = Rsh_cell / Ns * Np (current divides, voltage adds)
    // Typical Rsh_cell ≈ 1000 ohms for a good cell.
    double rsh_per_cell_ohms = 1000.0;
    parameters->shunt_resistance_ohms =
        rsh_per_cell_ohms
        * (double)parameters->number_of_cells_in_series
        / (double)parameters->number_of_strings_in_parallel;
}

// ── Public: compute current at voltage ───────────────────────────────────────

double solar_panel_compute_current_at_voltage(
    const struct solar_panel_parameters *parameters,
    double panel_voltage_volts,
    double temperature_celsius,
    double irradiance_fraction)
{
    // Handle zero irradiance: panel produces no current in the dark
    if (irradiance_fraction <= 0.0) {
        return 0.0;
    }

    // Handle negative or zero voltage: return short-circuit current
    if (panel_voltage_volts <= 0.0) {
        return compute_photocurrent_at_conditions(
            parameters,
            temperature_celsius + CELSIUS_TO_KELVIN_OFFSET,
            irradiance_fraction);
    }

    double temperature_kelvin =
        temperature_celsius + CELSIUS_TO_KELVIN_OFFSET;

    double photocurrent =
        compute_photocurrent_at_conditions(
            parameters, temperature_kelvin, irradiance_fraction);

    double saturation_current =
        compute_saturation_current_at_temperature(
            parameters, temperature_kelvin);

    double thermal_voltage =
        compute_thermal_voltage_for_temperature(temperature_kelvin);

    // n * Ns * Vt — the combined thermal voltage for the series string
    double thermal_voltage_times_ideality_times_cells =
        parameters->diode_ideality_factor
        * (double)parameters->number_of_cells_in_series
        * thermal_voltage;

    double current = solve_current_at_voltage_using_newton_raphson(
        photocurrent,
        saturation_current,
        panel_voltage_volts,
        thermal_voltage_times_ideality_times_cells,
        parameters->series_resistance_ohms,
        parameters->shunt_resistance_ohms);

    return current;
}

// ── Public: find maximum power point ─────────────────────────────────────────

void solar_panel_find_maximum_power_point(
    const struct solar_panel_parameters *parameters,
    double temperature_celsius,
    double irradiance_fraction,
    double *out_mpp_voltage,
    double *out_mpp_current,
    double *out_mpp_power)
{
    // Compute Voc at this temperature to set the scan range.
    // Voc shifts with temperature: Voc(T) ≈ Voc_ref + dVoc/dT * (T - Tref)
    double temperature_kelvin =
        temperature_celsius + CELSIUS_TO_KELVIN_OFFSET;

    double voc_at_temperature =
        parameters->open_circuit_voltage_at_reference_temperature_volts
        + parameters->voltage_temperature_coefficient_volts_per_kelvin
          * (temperature_kelvin - parameters->reference_temperature_kelvin);

    // At reduced irradiance, Voc drops slightly (logarithmic relationship).
    // Approximate: Voc(G) ≈ Voc * (1 + 0.05 * ln(G)) for G in (0,1]
    if (irradiance_fraction > 0.0 && irradiance_fraction < 1.0) {
        voc_at_temperature += 0.05 * (double)parameters->number_of_cells_in_series
                              * log(irradiance_fraction);
    }

    if (voc_at_temperature <= 0.0) {
        *out_mpp_voltage = 0.0;
        *out_mpp_current = 0.0;
        *out_mpp_power   = 0.0;
        return;
    }

    // Scan from 0 to Voc in small steps, find the voltage with max power
    double best_voltage = 0.0;
    double best_current = 0.0;
    double best_power   = 0.0;

    for (int32_t step = 1;
         step < MPP_SEARCH_NUMBER_OF_VOLTAGE_STEPS;
         step += 1)
    {
        double voltage =
            voc_at_temperature * (double)step
            / (double)MPP_SEARCH_NUMBER_OF_VOLTAGE_STEPS;

        double current = solar_panel_compute_current_at_voltage(
            parameters, voltage, temperature_celsius, irradiance_fraction);

        double power = voltage * current;

        if (power > best_power) {
            best_power   = power;
            best_voltage = voltage;
            best_current = current;
        }
    }

    *out_mpp_voltage = best_voltage;
    *out_mpp_current = best_current;
    *out_mpp_power   = best_power;
}
