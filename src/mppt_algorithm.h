// =============================================================================
// mppt_algorithm.h
// Public interface for the Incremental Conductance MPPT algorithm.
//
// This module is pure logic (Category 1). It has zero hardware dependencies.
// It receives raw ADC readings for voltage and current, and returns a duty
// cycle value for the buck converter PWM output.
//
// All arithmetic is integer-only so the code is directly portable to the
// SAMD21RT Cortex-M0+ which has no hardware floating-point unit and no
// hardware integer divider.
//
// Category: PURE LOGIC (no hardware)
// =============================================================================

#ifndef MPPT_ALGORITHM_H
#define MPPT_ALGORITHM_H

#include <stdint.h>

// ── Duty cycle representation ────────────────────────────────────────────────
//
// The duty cycle is represented as a uint16_t in the range [0, 65535].
// 0 corresponds to 0% duty cycle, 65535 corresponds to 100% duty cycle.
// This gives a resolution of 100% / 65535 = 0.0015% per step.
//
// The algorithm clamps the output between a minimum and maximum to protect
// the buck converter hardware:
//   - 0% duty cycle would disconnect the converter (no output)
//   - 100% duty cycle would short the high-side switch (damage risk)

#define MPPT_DUTY_CYCLE_FULL_SCALE          65535u

// 5% minimum = 0.05 * 65535 = 3277
#define MPPT_MINIMUM_DUTY_CYCLE             3277u

// 95% maximum = 0.95 * 65535 = 62258
#define MPPT_MAXIMUM_DUTY_CYCLE             62258u

// ── Step size ────────────────────────────────────────────────────────────────
//
// Each iteration, the algorithm adjusts the duty cycle by this many counts.
// 328 counts = 0.5% of full scale = 0.005 * 65535 ≈ 328.
// For the buck converter V=Vbat/D, each step changes panel voltage by
// ~0.2V near the MPP (D≈0.42). This gives a voltage change of ~36 ADC
// counts, which must be larger than the noise floor after averaging
// (~20 counts with 8-sample window and ±2% raw noise).
// SNR ≈ 36/20 = 1.8, which is sufficient for reliable decisions.

#define MPPT_DUTY_CYCLE_STEP_SIZE           328u

// ── Noise threshold ──────────────────────────────────────────────────────────
//
// When the change in voltage or current between two consecutive readings is
// smaller than this threshold, the algorithm treats it as zero (noise).
// This prevents the algorithm from chasing phantom changes caused by ADC
// measurement noise.
//
// Value: 5 ADC counts out of 4095 (12-bit ADC) = 0.12% of full scale.
// This filters out quantization noise while preserving sensitivity to
// real changes in panel operating conditions.

#define MPPT_ZERO_CHANGE_THRESHOLD          5u

// ── Moving average filter ────────────────────────────────────────────────────
//
// The ADC readings contain noise (±2% on the real hardware). The IncCond
// algorithm compares small changes ΔV and ΔI between consecutive readings.
// If the noise amplitude exceeds the step-induced change, the algorithm
// makes random decisions. A moving average of the last N samples reduces
// noise by a factor of sqrt(N).
//
// 8 samples: noise reduced by factor of ~2.8 (from ±2% to ±0.7%).
// The algorithm runs its decision logic every N samples, not every sample.
// At 100 Hz sampling, this means one decision every 80 ms.

#define MPPT_MOVING_AVERAGE_WINDOW_SIZE     8u

// ── Algorithm state ──────────────────────────────────────────────────────────
//
// All persistent state for the algorithm lives in this struct.
// It is passed by pointer to every function (conventions.md Rule B2).
// The struct is opaque — callers allocate it but do not access its fields.

struct mppt_algorithm_state {
    uint16_t previous_averaged_voltage;
    uint16_t previous_averaged_current;
    uint16_t current_duty_cycle_as_fraction_of_65535;
    uint32_t voltage_accumulator_for_averaging;
    uint32_t current_accumulator_for_averaging;
    uint8_t  samples_collected_in_current_window;
    uint8_t  algorithm_has_been_initialized;
};

// ── Public functions ─────────────────────────────────────────────────────────

// Sets the algorithm to its initial state with a starting duty cycle of 50%.
// Must be called exactly once before the first call to run_one_iteration.
void mppt_algorithm_initialize(
    struct mppt_algorithm_state *state);

// Runs one iteration of the Incremental Conductance algorithm.
//
// Inputs:
//   state                  — pointer to the algorithm's persistent state
//   voltage_raw_adc_reading — current solar panel voltage as a 12-bit ADC value
//   current_raw_adc_reading — current solar panel current as a 12-bit ADC value
//
// Returns:
//   The updated duty cycle as a uint16_t in the range
//   [MPPT_MINIMUM_DUTY_CYCLE, MPPT_MAXIMUM_DUTY_CYCLE].
//
// This function has no side effects beyond modifying *state.
// Given the same sequence of inputs, it always produces the same outputs.
uint16_t mppt_algorithm_run_one_iteration(
    struct mppt_algorithm_state *state,
    uint16_t voltage_raw_adc_reading,
    uint16_t current_raw_adc_reading);

#endif // MPPT_ALGORITHM_H
