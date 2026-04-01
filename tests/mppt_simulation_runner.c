// =============================================================================
// mppt_simulation_runner.c
// Closed-loop MPPT simulation that outputs CSV data for visualization.
//
// This program simulates the complete power system loop:
//   1. The algorithm outputs a duty cycle
//   2. The buck converter model computes the resulting panel voltage
//   3. The solar panel model computes the current at that voltage
//   4. ADC noise is added and values are quantized to 12-bit
//   5. The algorithm receives the noisy ADC readings
//   6. Repeat
//
// The simulation runs multiple scenarios (steady-state, step changes,
// temperature ramps) and outputs CSV data for each one.
//
// Usage:
//   ./run_mppt_simulation [scenario_number] [panel_config]
//   scenario_number: 1-6 (default: 1)
//   panel_config: 4p or 2s2p (default: 4p)
//
// Category: SIMULATION (PC only)
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/assertion_handler.h"
#include "../src/mppt_algorithm.h"
#include "solar_panel_simulator.h"
#include "buck_converter_model.h"

// ── Simulation parameters ────────────────────────────────────────────────────

// Number of iterations per scenario
#define SIMULATION_ITERATIONS_PER_SCENARIO   1000

// ADC configuration matching the SAMD21 hardware
// The ADC reference voltages represent the full-scale voltage that
// corresponds to the maximum ADC reading (4095 for 12-bit).
// These must match the voltage divider ratios on the real PCB.
#define ADC_RESOLUTION_BITS                  12

// Voltage channel: assume a divider that maps 0-25V to 0-3.3V
// Reference = 25V means ADC reading 4095 = 25V panel voltage
#define VOLTAGE_ADC_REFERENCE_VOLTS          25.0

// Current channel: assume a sense amplifier that maps 0-3A to 0-3.3V
// Reference = 3A means ADC reading 4095 = 3A panel current
#define CURRENT_ADC_REFERENCE_AMPS           3.0

// ADC noise: ±2% random variation on each reading
// Set to 0.0 for ideal ADC, 0.02 for realistic ±2% noise.
// The algorithm uses a 4-sample moving average to filter noise.
#define ADC_NOISE_FRACTION                   0.02

// ── Private: print CSV header ────────────────────────────────────────────────

static void print_csv_header(void)
{
    (void)printf(
        "iteration,"
        "duty_cycle_raw,"
        "duty_cycle_percent,"
        "panel_voltage_volts,"
        "panel_current_amps,"
        "panel_power_watts,"
        "voltage_adc_raw,"
        "current_adc_raw,"
        "temperature_celsius,"
        "irradiance_percent,"
        "theoretical_mpp_voltage,"
        "theoretical_mpp_current,"
        "theoretical_mpp_power,"
        "power_error_percent,"
        "algorithm_decision\n");
}

// ── Private: determine algorithm decision label ──────────────────────────────

static const char *determine_algorithm_decision_label(
    uint16_t previous_duty_cycle,
    uint16_t current_duty_cycle)
{
    if (current_duty_cycle > previous_duty_cycle) {
        return "INCREASE_D";
    }
    if (current_duty_cycle < previous_duty_cycle) {
        return "DECREASE_D";
    }
    return "HOLD";
}

// ── Private: run one simulation scenario ─────────────────────────────────────

static int32_t run_one_scenario(
    struct solar_panel_parameters *panel_parameters,
    struct buck_converter_parameters *converter_parameters,
    int32_t scenario_number)
{
    struct mppt_algorithm_state algorithm_state;
    mppt_algorithm_initialize(&algorithm_state);

    // Scenario-specific parameters
    double temperature_celsius = 25.0;
    double irradiance_fraction = 1.0;

    // Print scenario header as a CSV comment
    (void)printf("# Scenario %d: ", (int)scenario_number);

    switch (scenario_number) {
        case 1:
            (void)printf("Steady state, 100%% irradiance, 25C\n");
            temperature_celsius = 25.0;
            irradiance_fraction = 1.0;
            break;
        case 2:
            (void)printf("Steady state, 50%% irradiance, 25C\n");
            temperature_celsius = 25.0;
            irradiance_fraction = 0.5;
            break;
        case 3:
            (void)printf("Steady state, 100%% irradiance, 80C (hot panel)\n");
            temperature_celsius = 80.0;
            irradiance_fraction = 1.0;
            break;
        case 4:
            (void)printf("Step change: 100%% -> 50%% irradiance at iteration 250\n");
            temperature_celsius = 25.0;
            irradiance_fraction = 1.0;
            break;
        case 5:
            (void)printf("Temperature ramp: 25C -> 80C over 500 iterations\n");
            temperature_celsius = 25.0;
            irradiance_fraction = 1.0;
            break;
        case 6:
            (void)printf("Eclipse entry: irradiance drops to 0 at iteration 250\n");
            temperature_celsius = 25.0;
            irradiance_fraction = 1.0;
            break;
        default:
            (void)printf("Unknown scenario\n");
            return -1;
    }

    print_csv_header();

    int32_t converged_count = 0;
    int32_t total_post_transient_iterations = 0;

    for (int32_t iteration = 0;
         iteration < SIMULATION_ITERATIONS_PER_SCENARIO;
         iteration += 1)
    {
        // ── Update scenario conditions per iteration ─────────────────────
        if (scenario_number == 4 && iteration == 250) {
            // Step change: irradiance drops to 50%
            irradiance_fraction = 0.5;
        }
        if (scenario_number == 5) {
            // Temperature ramp: 25°C to 80°C linearly
            temperature_celsius =
                25.0 + (80.0 - 25.0)
                * (double)iteration
                / (double)(SIMULATION_ITERATIONS_PER_SCENARIO - 1);
        }
        if (scenario_number == 6 && iteration >= 250) {
            // Eclipse: irradiance drops to 0
            irradiance_fraction = 0.0;
        }

        // ── Step 1: Get current duty cycle from algorithm state ───────────
        uint16_t duty_cycle_raw =
            algorithm_state.current_duty_cycle_as_fraction_of_65535;

        // ── Step 2: Buck converter computes panel voltage ────────────────
        double panel_voltage =
            buck_converter_compute_panel_voltage_from_duty_cycle(
                converter_parameters, duty_cycle_raw);

        // ── Step 3: Solar panel model computes current ───────────────────
        double panel_current =
            solar_panel_compute_current_at_voltage(
                panel_parameters,
                panel_voltage,
                temperature_celsius,
                irradiance_fraction);

        double panel_power = panel_voltage * panel_current;

        // ── Step 4: Convert to ADC readings with noise ───────────────────
        uint16_t voltage_adc_raw = 0;
        uint16_t current_adc_raw = 0;

        buck_converter_convert_to_adc_readings(
            panel_voltage,
            panel_current,
            VOLTAGE_ADC_REFERENCE_VOLTS,
            CURRENT_ADC_REFERENCE_AMPS,
            ADC_RESOLUTION_BITS,
            ADC_NOISE_FRACTION,
            &voltage_adc_raw,
            &current_adc_raw);

        // ── Step 5: Feed to MPPT algorithm ───────────────────────────────
        uint16_t previous_duty_cycle = duty_cycle_raw;

        uint16_t new_duty_cycle =
            mppt_algorithm_run_one_iteration(
                &algorithm_state,
                voltage_adc_raw,
                current_adc_raw);

        // ── Compute theoretical MPP for comparison ───────────────────────
        double mpp_voltage = 0.0;
        double mpp_current = 0.0;
        double mpp_power   = 0.0;

        solar_panel_find_maximum_power_point(
            panel_parameters,
            temperature_celsius,
            irradiance_fraction,
            &mpp_voltage,
            &mpp_current,
            &mpp_power);

        // ── Compute power error ──────────────────────────────────────────
        double power_error_percent = 0.0;
        if (mpp_power > 0.001) {
            power_error_percent =
                ((mpp_power - panel_power) / mpp_power) * 100.0;
        }

        // ── Track convergence (after initial 200 iterations transient) ───
        if (iteration >= 200 && mpp_power > 0.001) {
            total_post_transient_iterations += 1;
            if (fabs(power_error_percent) < 2.0) {
                converged_count += 1;
            }
        }

        // ── Output CSV row ───────────────────────────────────────────────
        const char *decision = determine_algorithm_decision_label(
            previous_duty_cycle, new_duty_cycle);

        double duty_cycle_percent =
            (double)new_duty_cycle / 65535.0 * 100.0;

        (void)printf(
            "%d,%u,%.2f,%.4f,%.4f,%.4f,%u,%u,%.1f,%.1f,"
            "%.4f,%.4f,%.4f,%.2f,%s\n",
            (int)iteration,
            (unsigned)new_duty_cycle,
            duty_cycle_percent,
            panel_voltage,
            panel_current,
            panel_power,
            (unsigned)voltage_adc_raw,
            (unsigned)current_adc_raw,
            temperature_celsius,
            irradiance_fraction * 100.0,
            mpp_voltage,
            mpp_current,
            mpp_power,
            power_error_percent,
            decision);
    }

    // ── Print pass/fail verdict ──────────────────────────────────────────
    double convergence_ratio = 0.0;
    if (total_post_transient_iterations > 0) {
        convergence_ratio =
            (double)converged_count / (double)total_post_transient_iterations;
    }

    (void)fprintf(stderr,
        "Scenario %d: %d/%d iterations within 2%% of MPP (%.1f%%) — %s\n",
        (int)scenario_number,
        (int)converged_count,
        (int)total_post_transient_iterations,
        convergence_ratio * 100.0,
        (convergence_ratio >= 0.90) ? "PASS" : "FAIL");

    return (convergence_ratio >= 0.90) ? 0 : 1;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    // ── Parse command-line arguments ─────────────────────────────────────
    int32_t scenario_number = 1;
    int32_t panel_config_mode = PANEL_CONFIG_4P;

    if (argc >= 2) {
        scenario_number = atoi(argv[1]);
        if (scenario_number < 1 || scenario_number > 6) {
            (void)fprintf(stderr,
                "Usage: %s [scenario 1-6] [4p|2s2p]\n",
                argv[0]);
            return 1;
        }
    }

    if (argc >= 3) {
        if (strcmp(argv[2], "2s2p") == 0) {
            panel_config_mode = PANEL_CONFIG_2S2P;
        }
    }

    // ── Seed the random number generator for ADC noise ────────────────────
    // Using a fixed seed for reproducibility. Change to time(NULL) for
    // different noise patterns each run.
    srand(42u);

    // ── Initialize models ────────────────────────────────────────────────
    struct solar_panel_parameters panel_params;
    solar_panel_initialize_parameters(&panel_params, panel_config_mode);

    // Default battery voltage: 7.4V (nominal for 2S Li-ion)
    double battery_voltage = 7.4;
    if (argc >= 4) {
        battery_voltage = atof(argv[3]);
    }

    struct buck_converter_parameters converter_params;
    buck_converter_initialize_parameters(&converter_params, battery_voltage);

    (void)fprintf(stderr,
        "MPPT Simulation — Panel config: %s, Battery: %.1fV\n",
        (panel_config_mode == PANEL_CONFIG_4P) ? "4P" : "2S2P",
        battery_voltage);

    // ── Run the selected scenario ────────────────────────────────────────
    int32_t result = run_one_scenario(
        &panel_params, &converter_params, scenario_number);

    return (int)result;
}
