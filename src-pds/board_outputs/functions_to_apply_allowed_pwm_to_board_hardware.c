/*
 * functions_to_apply_allowed_pwm_to_board_hardware.c
 *
 * The hardware-write step that closes the per-iteration sequence in
 * app/main.c's main loop:
 *
 *     mode runner               (writes "requested PWM" into snapshot)
 *         ↓
 *     block_dangerous_outputs() (clamps "applied PWM" to 0 if a fault
 *                                is active)
 *         ↓
 *     apply_outputs_to_board()  (THIS function — writes TCC0 register
 *                                from snapshot.applied PWM)
 *
 * This is the ONLY place in the firmware that drives the buck-
 * converter PWM hardware. Every mode runner, every safety pass, every
 * web command ultimately just writes into the snapshot; this one call
 * is what makes the silicon move. Centralising it here means there is
 * exactly one register-write to inspect on a scope and exactly one
 * place to add new safety checks if a future revision needs them.
 */

#include "functions_to_apply_allowed_pwm_to_board_hardware.h"

#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "pwm_buck_converter.h"

/*
 * Push the snapshot's APPLIED PWM duty value (the post-safety-pass
 * value, NOT the raw mode-runner request) into the TCC0 hardware.
 * The split between snapshot.requested_pwm and snapshot.applied_pwm
 * exists so the operator's web page can show both: "you asked for X,
 * the firmware actually drove Y" — which makes a clamp by the safety
 * pass visible to the operator instead of silent.
 *
 * Called every main-loop iteration even when nothing has changed.
 * The PWM driver's set_duty_cycle is cheap (a few register writes;
 * the TCC0 buffered compare register accepts a new value every
 * iteration without glitching), so the cost of re-asserting on every
 * pass is trivial and the benefit is large: a transient register
 * clobber by some other code path gets corrected on the very next
 * iteration.
 */
void apply_outputs_to_board(void)
{
    /* Pointer to the firmware-wide runtime state. The "applied" PWM
     * field lives in snapshot, where the safety pass and any mode
     * runner can have written to it earlier this iteration. */
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Hand the value to the PWM driver. On mainboard this writes
     * TCC0's buffered compare register, which makes both PA12 and
     * PA13 update simultaneously on the next PWM period boundary
     * (no glitch). On devboard the PWM driver is a no-op stub
     * (pwm_buck_converter_disabled_stub.c) so this call still
     * compiles and runs but doesn't drive anything physical. */
    pwm_buck_converter_set_duty_cycle(
        runtime_state->snapshot.applied_pwm_duty_cycle_as_fraction_of_65535);
}
