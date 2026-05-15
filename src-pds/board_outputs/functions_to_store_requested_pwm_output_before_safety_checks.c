/*
 * functions_to_store_requested_pwm_output_before_safety_checks.c
 *
 * Two tiny helpers used by every mode runner that wants to drive the
 * buck converter PWM. Despite the long name, neither one talks to the
 * TCC0 hardware directly — they just write into the runtime-state
 * snapshot's "requested PWM" fields.
 *
 * The actual hardware register write happens later, after the mode
 * runner returns, in this sequence inside the main loop:
 *
 *     mode runner               (fills requested_pwm_duty_cycle)
 *         ↓
 *     block_dangerous_outputs() (clamps PWM to 0 if a fault is active)
 *         ↓
 *     apply_outputs_to_board()  (writes TCC0 registers from snapshot)
 *
 * This split is deliberate: it means every mode runner shares one
 * safety gate, so a future addition to block_dangerous_outputs (e.g.
 * "if battery temperature is too high, refuse PWM") protects every
 * mode automatically without having to be added to each runner.
 */

#include <stdint.h>

#include "assertion_handler.h"
#include "functions_to_store_requested_pwm_output_before_safety_checks.h"

/*
 * Records that the current mode wants to drive PWM at the given duty
 * cycle. The "requested" and "applied" snapshot fields both get the
 * same value here; the safety gate that runs after this function may
 * later overwrite "applied" with 0 if it decides the request is
 * dangerous, leaving "requested" intact for telemetry.
 */
void request_pwm_output_in_runtime_state(
    pds_runtime_state_type *runtime_state,
    uint16_t duty_cycle_as_fraction_of_65535)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    /* Both fields start equal. The safety pass that runs after the
     * mode dispatcher gets to clobber "applied" if needed. */
    runtime_state->snapshot.requested_pwm_duty_cycle_as_fraction_of_65535 =
        duty_cycle_as_fraction_of_65535;
    runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535 =
        duty_cycle_as_fraction_of_65535;

    /* The "enabled" flag is just a derived convenience for the page —
     * any non-zero duty means PWM is actively driving the converter. */
    runtime_state->snapshot.pwm_output_is_enabled =
        (duty_cycle_as_fraction_of_65535 > 0u) ? 1u : 0u;
}

/*
 * Force-zero variant. Used when the mode runner has decided the
 * converter must be off this iteration — for example, the MPPT runner
 * couldn't get a valid input sample, or the OFF mode runner just
 * wants everything dark.
 *
 * Also forces the panel-eFuse-enabled telemetry bit to 0 so the page
 * shows a consistent "everything off" picture, not "PWM off but eFuse
 * still on" which would be confusing to read.
 */
void request_no_pwm_output_in_runtime_state(
    pds_runtime_state_type *runtime_state)
{
    SATELLITE_ASSERT(runtime_state != (void *)0);

    runtime_state->snapshot.requested_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535 = 0u;
    runtime_state->snapshot.pwm_output_is_enabled = 0u;
    runtime_state->snapshot.panel_efuse_is_enabled = 0u;
}
