// =============================================================================
// buck_converter_model.h
// Models the EPC2152 GaN half-bridge buck converter for simulation.
//
// In a buck converter operating in Continuous Conduction Mode (CCM):
//   V_out = D × V_in
//
// Since the output is connected to the battery (approximately a voltage
// source at its current state of charge), V_out is fixed. Therefore:
//   V_panel = V_battery / D
//
// This means:
//   Higher D → lower V_panel → panel operates at lower voltage, higher current
//   Lower D  → higher V_panel → panel operates at higher voltage, lower current
//
// Hardware: EPC2152 GaN half-bridge, 300 kHz switching frequency.
// Source: CHESS Pathfinder 0 mission document, section 3.4.2.2.1, page 96.
//
// Category: SIMULATION (PC only, float math)
// =============================================================================

#ifndef BUCK_CONVERTER_MODEL_H
#define BUCK_CONVERTER_MODEL_H

#include <stdint.h>

// ── Converter parameters ─────────────────────────────────────────────────────

struct buck_converter_parameters {
    double battery_voltage_volts;
    double minimum_duty_cycle_fraction;
    double maximum_duty_cycle_fraction;
};

// ── Public functions ─────────────────────────────────────────────────────────

// Initializes the converter parameters with the given battery voltage.
//   battery_voltage: typically 6.0V (empty) to 8.4V (full) for 2S Li-ion.
void buck_converter_initialize_parameters(
    struct buck_converter_parameters *parameters,
    double battery_voltage_volts);

// Given a duty cycle (as uint16_t 0-65535), computes the panel voltage
// that the converter imposes on the solar array input.
//
// Returns: V_panel = V_battery / D, clamped to reasonable physical limits.
double buck_converter_compute_panel_voltage_from_duty_cycle(
    const struct buck_converter_parameters *parameters,
    uint16_t duty_cycle_as_fraction_of_65535);

// Converts a panel voltage and current to the values that the ADC would
// read, given the ADC reference voltage and resolution.
//
//   actual_voltage_volts  — the real panel voltage
//   actual_current_amps   — the real panel current
//   voltage_adc_reference — the ADC reference voltage for the voltage channel
//   current_adc_reference — the ADC reference voltage for the current channel
//   adc_resolution_bits   — typically 12 for the SAMD21
//   noise_fraction        — random noise amplitude as fraction (0.02 = ±2%)
//   out_voltage_adc_raw   — output: 12-bit ADC reading for voltage
//   out_current_adc_raw   — output: 12-bit ADC reading for current
void buck_converter_convert_to_adc_readings(
    double actual_voltage_volts,
    double actual_current_amps,
    double voltage_adc_reference_volts,
    double current_adc_reference_volts,
    int32_t adc_resolution_bits,
    double noise_fraction,
    uint16_t *out_voltage_adc_raw,
    uint16_t *out_current_adc_raw);

#endif // BUCK_CONVERTER_MODEL_H
