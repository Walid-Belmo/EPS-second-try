/*
 * functions_to_keep_board_outputs_off_when_requested.c
 *
 * Two trivial mode runners:
 *
 *   run_off_mode()              - what the firmware does when the
 *                                 operator pressed "Off" on the web app
 *                                 (or has not selected any test mode
 *                                 yet). Goal: keep every board output
 *                                 dark, every iteration, no surprises.
 *
 *   run_full_satellite_logic() - what the firmware does in "flight"
 *                                 mode. In this PDS skeleton it is a
 *                                 placeholder that just keeps PWM at
 *                                 zero; the real flight path (running
 *                                 the EPS state machine on real sensor
 *                                 readings) is not wired into this
 *                                 file yet — it lives in the State Test
 *                                 path for now.
 *
 * Both runners are dispatched from the if/else cascade in app/main.c
 * once per main-loop iteration. Neither one writes any hardware
 * register directly. They only update the "requested PWM = 0" field in
 * the runtime-state snapshot; the actual TCC0 register write happens
 * later in apply_outputs_to_board() after the safety pass runs.
 */

#include "functions_to_keep_board_outputs_off_when_requested.h"

#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"

/*
 * "Off" mode runner. Called every iteration of the main loop while
 * runtime_state.requested_mode is PDS_REQUESTED_MODE_OFF (the default
 * after boot, and the state the operator returns to by clicking the Off
 * button on any web page).
 *
 * The single job is to keep PWM at zero. We don't trust that some
 * earlier iteration left things off — we re-assert it every time, so
 * even if a previous mode left a non-zero duty in the snapshot the
 * very next loop iteration overwrites it with 0.
 */
void run_off_mode(void)
{
    /* Get the firmware-wide singleton runtime state. Same accessor
     * every other module uses; see
     * runtime_state/functions_to_access_pds_runtime_state.c for why
     * the project deliberately uses one shared struct rather than
     * scattered per-module globals. */
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Force the snapshot's requested-PWM and applied-PWM fields to 0,
     * the pwm-enabled flag to 0, and the panel-eFuse-enabled telemetry
     * bit to 0. The actual TCC0 hardware write happens later in
     * apply_outputs_to_board(); this just sets the value that write
     * will read. See
     * board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.c
     * for the full picture of why "request" and "apply" are split. */
    request_no_pwm_output_in_runtime_state(runtime_state);
}

/*
 * "Flight" mode runner. The real flight behaviour — read real sensors,
 * run the EPS state machine, command the buck converter — has not been
 * ported into this PDS layer yet. The State Test mode runs that logic
 * today against operator-injected sensor values; once the real sensor
 * read paths (Layer 1 sensor abstraction with REAL_BOARD_HARDWARE
 * source) are validated on the mainboard, this function will be
 * replaced with a thin wrapper around the State Test logic but with
 * the sensor source pinned to REAL_BOARD_HARDWARE.
 *
 * Until then: behave like Off mode so a stray click into Flight mode
 * doesn't put the converter into an unknown state.
 */
void run_full_satellite_logic(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Same call as run_off_mode() — keep everything quiet until the
     * real flight path is wired up. */
    request_no_pwm_output_in_runtime_state(runtime_state);
}
