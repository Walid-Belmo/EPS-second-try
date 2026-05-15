/*
 * functions_to_block_outputs_when_faults_are_injected.c
 *
 * The safety pass that runs between the mode dispatcher and the
 * hardware-write step in app/main.c's main loop. Sequence per
 * iteration:
 *
 *     mode runner               (writes "requested PWM" into snapshot)
 *         ↓
 *     block_dangerous_outputs() (THIS function — clamps PWM to 0 if a
 *                                fault is active, regardless of mode)
 *         ↓
 *     apply_outputs_to_board()  (writes TCC0 from snapshot)
 *
 * The split exists so every mode shares one safety gate. Whatever a
 * mode runner asked for is just a request; this function decides if
 * the request actually reaches the hardware.
 *
 * "Faults" today means the operator-injected fault flags from the
 * State page (the inject_state CHIPS command writes them into
 * runtime_state.injected_state_inputs.fault_flags). Real hardware
 * fault detection — battery overcurrent, panel overvoltage etc. —
 * lives in the LM139 comparator block on the PCB and is independent
 * of firmware; this function only handles the software-injected
 * faults the State page uses to test the safe-mode response.
 */

#include "functions_to_block_outputs_when_faults_are_injected.h"

#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "functions_to_store_requested_pwm_output_before_safety_checks.h"

/*
 * Called once per main-loop iteration, after the mode dispatcher and
 * before the hardware write. If injected fault flags are non-zero,
 * forces PWM to zero and marks the snapshot's safe-mode telemetry
 * fields so the OBC (and the web pages) can see that something
 * tripped.
 *
 * Note: this only blocks PWM. The PV/BAT eFuse switches and the
 * status LED stay under whatever the active mode requested. Manual
 * mode therefore still lets the operator wiggle those even with a
 * fault injected — the design choice is "manual mode is for testing,
 * the hardware comparator is the real protection."
 */
void block_dangerous_outputs(void)
{
    /* Pointer to the firmware-wide runtime state singleton. */
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Common case: no fault injected. Leave the snapshot untouched
     * and let the mode runner's request flow through to the hardware
     * write step. */
    if (runtime_state->injected_state_inputs.fault_flags == 0u)
    {
        return;
    }

    /* Fault path. Override the mode runner's PWM request with zero so
     * apply_outputs_to_board() drives TCC0 to 0% duty regardless of
     * what the mode wanted. Also clears the panel-eFuse-enabled
     * telemetry bit (see request_no_pwm_output_in_runtime_state for
     * why). */
    request_no_pwm_output_in_runtime_state(runtime_state);

    /* Surface the safe-mode condition into the snapshot fields the
     * status reply carries. The web pages read these to render the
     * red "safe mode active" indicators; the OBC reads
     * safe_mode_alert_for_obc to know it should investigate. */
    runtime_state->snapshot.safe_mode_is_active = 1u;
    runtime_state->snapshot.safe_mode_alert_for_obc = 1u;

    /* Tag the reason with the "injected fault" code so a log reader
     * can tell this safe-mode entry came from operator-injected
     * fault flags rather than from a real sensor reading. The other
     * safe-mode reasons (battery low, temperature out of range,
     * heartbeat timeout) are set by the EPS state-machine path. */
    runtime_state->snapshot.safe_mode_reason = PDS_SAFE_REASON_INJECTED_FAULT;
}
