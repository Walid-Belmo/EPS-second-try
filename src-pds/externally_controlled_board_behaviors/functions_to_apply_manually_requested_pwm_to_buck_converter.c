/*
 * functions_to_apply_manually_requested_pwm_to_buck_converter.c
 *
 * Fixed-PWM test mode runner. Used when the operator wants to drive
 * the buck converter at one specific duty cycle and watch what happens
 * — typically for bench bring-up of the power stage, scope-checking
 * the dead-time on PA12/PA13, or characterising a converter component
 * before letting MPPT decide the duty.
 *
 * The mode is entered when the operator types `run_pwm duty=NNNNN` on
 * the ESP32 USB serial. That command's handler stores NNNNN in
 * runtime_state.fixed_pwm_duty_cycle_as_fraction_of_65535 and sets
 * runtime_state.requested_mode to PDS_REQUESTED_MODE_FIXED_PWM_TEST,
 * which makes the dispatcher in app/main.c call this runner every
 * iteration until the mode changes.
 *
 * No control loop, no MPPT, no state machine. Just "drive PWM at the
 * value the operator typed".
 */

#include <stdint.h>

#include "eps_state_machine.h"
#include "functions_to_apply_manually_requested_pwm_to_buck_converter.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"

/*
 * One iteration of fixed-PWM test mode. Called every pass through the
 * main loop while requested_mode is FIXED_PWM_TEST. The body
 * deliberately re-asserts the requested duty every iteration rather
 * than only when the operator changes it — this means a transient
 * register glitch or some other code path clobbering the snapshot
 * gets corrected on the very next iteration, and there is exactly one
 * place where this mode's PWM intent lives.
 */
void run_fixed_pwm_test_only(void)
{
    /* Pointer to the firmware-wide runtime state. The fixed PWM duty
     * cycle the operator typed lives in runtime_state's
     * fixed_pwm_duty_cycle_as_fraction_of_65535 field. */
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* Push that operator-typed duty into the snapshot's
     * "requested PWM" field. The actual TCC0 hardware register write
     * happens later in apply_outputs_to_board(); this function only
     * tells the snapshot what we want. The safety pass
     * (block_dangerous_outputs) runs between this and the hardware
     * write and is allowed to override the request to 0 if a fault
     * flag is active — we want that, not against it. */
    request_pwm_output_in_runtime_state(
        runtime_state,
        runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535);

    /* The status reply includes a "PCU mode" telemetry field that
     * other pages (especially the State page) display as the chip's
     * current operating regime. Fixed-PWM test isn't a real PCU mode
     * from the EPS state-machine perspective, so we pin the
     * displayed value to MPPT_CHARGE — the closest analogue, since
     * we're driving the panel-side converter. Without this pin, the
     * snapshot would carry whatever value the previous mode last
     * wrote, which would confuse the operator. */
    runtime_state->snapshot.pcu_mode = (uint8_t)EPS_PCU_MODE_MPPT_CHARGE;

    /* Mirror "is the PV eFuse open?" into the snapshot for the
     * telemetry page. Convention: any non-zero requested duty implies
     * we expect the panel eFuse to be passing current; zero duty
     * implies the converter is idle and the eFuse can be reported as
     * disabled. This is purely a display convenience — fixed-PWM mode
     * does not actually drive PA16 (the real eFuse enable pin) since
     * that's the manual-mode runner's job. */
    runtime_state->snapshot.panel_efuse_is_enabled =
        (runtime_state->fixed_pwm_duty_cycle_as_fraction_of_65535 > 0u)
        ? 1u
        : 0u;
}
