// =============================================================================
// buck_converter_model.c
// Ideal buck converter model for the MPPT simulation.
//
// The converter is modeled as an ideal transformer with ratio D:
//   V_out = D × V_in → V_in = V_out / D = V_battery / D
//
// The battery is modeled as a fixed voltage source (its voltage changes
// slowly over minutes/hours, while the MPPT loop runs at ~100 Hz).
//
// ADC conversion adds quantization and optional noise to simulate the
// real measurement chain: solar panel → voltage divider → ADC → uint16_t.
//
// Category: SIMULATION (PC only, float math)
// =============================================================================

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "buck_converter_model.h"

// ── Public: initialize ───────────────────────────────────────────────────────

void buck_converter_initialize_parameters(
    struct buck_converter_parameters *parameters,
    double battery_voltage_volts)
{
    parameters->battery_voltage_volts = battery_voltage_volts;

    // Match the algorithm's duty cycle limits (5% to 95%)
    // 0.05 * 65535 = 3277, 0.95 * 65535 = 62258
    parameters->minimum_duty_cycle_fraction = 0.05;
    parameters->maximum_duty_cycle_fraction = 0.95;
}

// ── Public: compute panel voltage from duty cycle ────────────────────────────

double buck_converter_compute_panel_voltage_from_duty_cycle(
    const struct buck_converter_parameters *parameters,
    uint16_t duty_cycle_as_fraction_of_65535)
{
    // Convert uint16_t duty cycle to fractional value (0.0 to 1.0)
    double duty_cycle_fraction =
        (double)duty_cycle_as_fraction_of_65535 / 65535.0;

    // Clamp to safe range to avoid division by very small numbers
    if (duty_cycle_fraction < parameters->minimum_duty_cycle_fraction) {
        duty_cycle_fraction = parameters->minimum_duty_cycle_fraction;
    }
    if (duty_cycle_fraction > parameters->maximum_duty_cycle_fraction) {
        duty_cycle_fraction = parameters->maximum_duty_cycle_fraction;
    }

    // V_panel = V_battery / D
    // Example: 7.4V battery, D=0.5 → V_panel = 14.8V
    // Example: 7.4V battery, D=0.4 → V_panel = 18.5V
    double panel_voltage_volts =
        parameters->battery_voltage_volts / duty_cycle_fraction;

    return panel_voltage_volts;
}

// ── Private: generate a pseudo-random number between -1.0 and +1.0 ──────────
//
// Uses the C standard library rand() function. This is adequate for
// simulation noise — we do not need cryptographic randomness here.

static double generate_random_noise_fraction(void)
{
    // rand() returns 0 to RAND_MAX. Scale to [-1.0, +1.0].
    double random_zero_to_one = (double)rand() / (double)RAND_MAX;
    return (2.0 * random_zero_to_one) - 1.0;
}

// ── Public: convert to ADC readings ──────────────────────────────────────────

void buck_converter_convert_to_adc_readings(
    double actual_voltage_volts,
    double actual_current_amps,
    double voltage_adc_reference_volts,
    double current_adc_reference_volts,
    int32_t adc_resolution_bits,
    double noise_fraction,
    uint16_t *out_voltage_adc_raw,
    uint16_t *out_current_adc_raw)
{
    // ADC max count: 2^resolution - 1 (e.g., 4095 for 12-bit)
    int32_t adc_max_count = (1 << adc_resolution_bits) - 1;

    // ── Voltage channel ──────────────────────────────────────────────────
    // Add noise: actual * (1 + noise_fraction * random)
    double noisy_voltage = actual_voltage_volts
        * (1.0 + noise_fraction * generate_random_noise_fraction());

    // Clamp to non-negative
    if (noisy_voltage < 0.0) {
        noisy_voltage = 0.0;
    }

    // Convert to ADC counts: counts = (voltage / reference) * max_count
    double voltage_counts =
        (noisy_voltage / voltage_adc_reference_volts) * (double)adc_max_count;

    // Clamp to ADC range
    if (voltage_counts < 0.0) {
        voltage_counts = 0.0;
    }
    if (voltage_counts > (double)adc_max_count) {
        voltage_counts = (double)adc_max_count;
    }

    *out_voltage_adc_raw = (uint16_t)(voltage_counts + 0.5);

    // ── Current channel ──────────────────────────────────────────────────
    double noisy_current = actual_current_amps
        * (1.0 + noise_fraction * generate_random_noise_fraction());

    if (noisy_current < 0.0) {
        noisy_current = 0.0;
    }

    double current_counts =
        (noisy_current / current_adc_reference_volts) * (double)adc_max_count;

    if (current_counts < 0.0) {
        current_counts = 0.0;
    }
    if (current_counts > (double)adc_max_count) {
        current_counts = (double)adc_max_count;
    }

    *out_current_adc_raw = (uint16_t)(current_counts + 0.5);
}
