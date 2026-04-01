// =============================================================================
// mppt_algorithm.c
// Incremental Conductance MPPT algorithm for a buck converter topology.
//
// The algorithm adjusts the PWM duty cycle to track the Maximum Power Point
// of the solar array. It uses the Incremental Conductance method as specified
// in the CHESS Pathfinder 0 mission document (section 3.4.2.2.2, page 97).
//
// All arithmetic is integer-only. Division is avoided by using
// cross-multiplication: instead of comparing ΔI/ΔV with -I/V, we compare
// ΔI × V with -I × ΔV. This avoids the need for a hardware divider, which
// the Cortex-M0+ does not have.
//
// Buck converter sign convention:
//   Higher duty cycle → lower panel voltage → more panel current
//   Lower duty cycle  → higher panel voltage → less panel current
//   (Because V_panel = V_battery / D for an ideal buck in CCM)
//
// Category: PURE LOGIC (no hardware)
// =============================================================================

#include <stdint.h>

#include "assertion_handler.h"
#include "mppt_algorithm.h"

// ── Private helper: clamp duty cycle to safe operating range ─────────────────

static uint16_t clamp_duty_cycle_to_safe_operating_range(
    uint16_t unclamped_duty_cycle)
{
    if (unclamped_duty_cycle < MPPT_MINIMUM_DUTY_CYCLE) {
        return MPPT_MINIMUM_DUTY_CYCLE;
    }
    if (unclamped_duty_cycle > MPPT_MAXIMUM_DUTY_CYCLE) {
        return MPPT_MAXIMUM_DUTY_CYCLE;
    }
    return unclamped_duty_cycle;
}

// ── Private helper: check if a change is smaller than the noise floor ────────

static uint8_t change_is_within_noise_threshold(
    int32_t measured_change)
{
    // The absolute value of the change is compared against the threshold.
    // We avoid abs() to keep dependencies minimal.
    int32_t absolute_change = measured_change;
    if (absolute_change < 0) {
        absolute_change = -absolute_change;
    }

    return (absolute_change <= (int32_t)MPPT_ZERO_CHANGE_THRESHOLD) ? 1u : 0u;
}

// ── Private helper: increase duty cycle by one step ──────────────────────────
//
// For the buck converter, increasing the duty cycle lowers the panel voltage
// and draws more current from the panel. This moves the operating point
// to the LEFT on the I-V curve (toward higher current, lower voltage).

static uint16_t increase_duty_cycle_by_one_step(
    uint16_t current_duty_cycle)
{
    uint32_t new_duty_cycle =
        (uint32_t)current_duty_cycle + MPPT_DUTY_CYCLE_STEP_SIZE;

    if (new_duty_cycle > MPPT_MAXIMUM_DUTY_CYCLE) {
        return MPPT_MAXIMUM_DUTY_CYCLE;
    }

    return (uint16_t)new_duty_cycle;
}

// ── Private helper: decrease duty cycle by one step ──────────────────────────
//
// For the buck converter, decreasing the duty cycle raises the panel voltage
// and draws less current from the panel. This moves the operating point
// to the RIGHT on the I-V curve (toward lower current, higher voltage).

static uint16_t decrease_duty_cycle_by_one_step(
    uint16_t current_duty_cycle)
{
    if (current_duty_cycle <= (MPPT_MINIMUM_DUTY_CYCLE + MPPT_DUTY_CYCLE_STEP_SIZE)) {
        return MPPT_MINIMUM_DUTY_CYCLE;
    }

    return (uint16_t)(current_duty_cycle - MPPT_DUTY_CYCLE_STEP_SIZE);
}

// ── Public: initialize ───────────────────────────────────────────────────────

void mppt_algorithm_initialize(
    struct mppt_algorithm_state *state)
{
    SATELLITE_ASSERT(state != (void *)0);

    // Start at 50% duty cycle = 32768 out of 65535
    // 0.50 * 65535 = 32768 (rounded)
    state->current_duty_cycle_as_fraction_of_65535 = 32768u;
    state->previous_averaged_voltage = 0u;
    state->previous_averaged_current = 0u;
    state->voltage_accumulator_for_averaging = 0u;
    state->current_accumulator_for_averaging = 0u;
    state->samples_collected_in_current_window = 0u;
    state->algorithm_has_been_initialized = 1u;
}

// ── Public: run one iteration ────────────────────────────────────────────────

uint16_t mppt_algorithm_run_one_iteration(
    struct mppt_algorithm_state *state,
    uint16_t voltage_raw_adc_reading,
    uint16_t current_raw_adc_reading)
{
    // ── Preconditions ────────────────────────────────────────────────────
    SATELLITE_ASSERT(state != (void *)0);
    SATELLITE_ASSERT(state->algorithm_has_been_initialized == 1u);
    SATELLITE_ASSERT(voltage_raw_adc_reading <= 4095u);
    SATELLITE_ASSERT(current_raw_adc_reading <= 4095u);

    // ── Accumulate samples for the moving average ────────────────────────
    // The algorithm collects N samples, averages them, then makes one
    // decision. This reduces ADC noise by a factor of sqrt(N).
    state->voltage_accumulator_for_averaging += voltage_raw_adc_reading;
    state->current_accumulator_for_averaging += current_raw_adc_reading;
    state->samples_collected_in_current_window += 1u;

    // If we haven't collected enough samples yet, return current D unchanged
    if (state->samples_collected_in_current_window
        < MPPT_MOVING_AVERAGE_WINDOW_SIZE)
    {
        return state->current_duty_cycle_as_fraction_of_65535;
    }

    // ── Compute averaged readings ────────────────────────────────────────
    uint16_t averaged_voltage = (uint16_t)(
        state->voltage_accumulator_for_averaging
        / MPPT_MOVING_AVERAGE_WINDOW_SIZE);

    uint16_t averaged_current = (uint16_t)(
        state->current_accumulator_for_averaging
        / MPPT_MOVING_AVERAGE_WINDOW_SIZE);

    // Reset accumulators for next window
    state->voltage_accumulator_for_averaging = 0u;
    state->current_accumulator_for_averaging = 0u;
    state->samples_collected_in_current_window = 0u;

    // ── Compute changes since previous averaged reading ──────────────────
    int32_t delta_voltage =
        (int32_t)averaged_voltage
        - (int32_t)state->previous_averaged_voltage;

    int32_t delta_current =
        (int32_t)averaged_current
        - (int32_t)state->previous_averaged_current;

    // Store for next window
    state->previous_averaged_voltage = averaged_voltage;
    state->previous_averaged_current = averaged_current;

    uint16_t duty_cycle = state->current_duty_cycle_as_fraction_of_65535;

    // ── Incremental Conductance decision logic ───────────────────────────
    //
    // At MPP: dP/dV = 0 → dI/dV = -I/V
    // Cross-multiply to avoid division: ΔI × V vs -I × ΔV

    if (change_is_within_noise_threshold(delta_voltage)) {
        if (change_is_within_noise_threshold(delta_current)) {
            // ΔI ≈ 0 and ΔV ≈ 0: at or near MPP. Hold.
        } else if (delta_current > 0) {
            // Current increased at same voltage → irradiance increased.
            // MPP shifted to higher voltage.
            // For buck: decrease D to raise panel voltage (move right).
            duty_cycle = decrease_duty_cycle_by_one_step(duty_cycle);
        } else {
            // Current decreased at same voltage → irradiance decreased.
            // For buck: increase D to lower panel voltage (move left).
            duty_cycle = increase_duty_cycle_by_one_step(duty_cycle);
        }
    } else {
        // Cross-multiply: ΔI × V vs -I × ΔV
        // Max product: 4095 × 4095 = 16,769,025 — fits in int32_t.
        int32_t left_side =
            delta_current * (int32_t)averaged_voltage;

        int32_t right_side =
            -((int32_t)averaged_current) * delta_voltage;

        if (left_side == right_side) {
            // At MPP exactly. Hold.
        } else if (
            (delta_voltage > 0 && left_side > right_side)
            ||
            (delta_voltage < 0 && left_side < right_side)) {
            // LEFT of MPP (dP/dV > 0). Voltage should increase.
            // For buck: decrease D to raise panel voltage.
            duty_cycle = decrease_duty_cycle_by_one_step(duty_cycle);
        } else {
            // RIGHT of MPP (dP/dV < 0). Voltage should decrease.
            // For buck: increase D to lower panel voltage.
            duty_cycle = increase_duty_cycle_by_one_step(duty_cycle);
        }
    }

    duty_cycle = clamp_duty_cycle_to_safe_operating_range(duty_cycle);
    state->current_duty_cycle_as_fraction_of_65535 = duty_cycle;

    // ── Postcondition ────────────────────────────────────────────────────
    SATELLITE_ASSERT(duty_cycle >= MPPT_MINIMUM_DUTY_CYCLE);
    SATELLITE_ASSERT(duty_cycle <= MPPT_MAXIMUM_DUTY_CYCLE);

    return duty_cycle;
}
