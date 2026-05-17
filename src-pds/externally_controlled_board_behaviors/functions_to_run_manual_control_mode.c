/*
 * functions_to_run_manual_control_mode.c
 *
 * Manual-control mode runner. Used when the operator wants to drive
 * each of the four user-facing board outputs independently from a web
 * UI (the /manual page), bypassing every algorithm and state machine.
 *
 * The four outputs:
 *   1. PWM duty cycle      (PA12/PA13 via TCC0; the only one that
 *                            also exists on the dev board)
 *   2. PV-side eFuse switch (PA16; gates the solar-panel TPS25940)
 *   3. Battery-side eFuse switch (PA17; gates the battery TPS25940)
 *   4. Status LED PB22     (the green LED2 on the mainboard)
 *
 * Mode entry: operator types `enter_manual` on the web app's Manual
 * page, the command handler sets requested_mode = MANUAL and zeroes
 * every "manual_*" field so the mode starts in a known safe state
 * (PWM=0, both switches off, LED off). From then on the dispatcher in
 * app/main.c calls run_manual_control_mode() every iteration until
 * the mode changes.
 *
 * Each iteration we re-assert every output to whatever the operator's
 * latest set_manual_* command stored in runtime state. Re-asserting
 * every iteration (rather than only on change) means a transient
 * register clobber by another code path gets corrected on the very
 * next loop, and there is exactly one place in the firmware that owns
 * what each pin should be doing in this mode.
 *
 * Build-target split:
 *   - Layer 1 PWM call (request_pwm_output_in_runtime_state) compiles
 *     and runs on every build — even the dev board has the buck PWM
 *     pins on PA12/PA13.
 *   - The three GPIO-driver calls (PV switch, BAT switch, status LED)
 *     and the eFuse status reader only exist on the mainboard, so
 *     they're guarded by `#ifdef __SAMD21J17D__`. On the dev board
 *     the snapshot mirror earlier in this function still reflects what
 *     the operator requested, so the page shows a sensible value even
 *     though no real pin moves.
 */

#include <stdint.h>

#include "eps_state_machine.h"
#include "functions_to_run_manual_control_mode.h"
#include "board_outputs/functions_to_store_requested_pwm_output_before_safety_checks.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"

#ifdef __SAMD21J17D__
/* These four headers only exist for mainboard builds. The dev board
 * doesn't wire PA16/PA17/PA18-21/PB22 to anything useful, so it does
 * not link these drivers. The whole #ifdef block below is what makes
 * the same source compile cleanly on both targets. */
#include "led_status_pb22_active_high_on_mainboard.h"
#include "gpio_pv_efuse_enable_pa16_on_mainboard.h"
#include "gpio_bat_efuse_enable_pa17_on_mainboard.h"
#include "gpio_efuse_status_inputs_pa18_to_pa21_on_mainboard.h"
#endif

/*
 * One iteration of manual-control mode. Called every pass through the
 * main loop while requested_mode is MANUAL. Three responsibilities,
 * in order:
 *
 *   1. Re-assert PWM (works on every build target).
 *   2. Pin telemetry fields the page reads to a consistent picture.
 *   3. On mainboard only: drive the three GPIO outputs and read the
 *      four status inputs back so the page can show "I asked for ON,
 *      the eFuse confirms it's ON, no fault active".
 */
void run_manual_control_mode(void)
{
    /* Pointer to the firmware-wide runtime state. The four
     * operator-typed values live in
     * runtime_state.manual_pwm_duty_cycle_as_fraction_of_65535,
     * runtime_state.manual_pv_switch_requested,
     * runtime_state.manual_bat_switch_requested,
     * runtime_state.manual_status_led_requested. The set_manual_*
     * command handlers write them; this runner reads them. */
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();

    /* PWM is the one output that exists on every build target. The
     * board_outputs layer takes care of the safety-block step that
     * runs after the mode dispatcher (block_dangerous_outputs), so we
     * just register the request here and let it flow through. The
     * actual TCC0 register write happens later in
     * apply_outputs_to_board(). */
    request_pwm_output_in_runtime_state(
        runtime_state,
        runtime_state->manual_pwm_duty_cycle_as_fraction_of_65535);

    /* The State page (and any other consumer that displays "current
     * PCU mode") reads runtime_state->snapshot.pcu_mode. Manual
     * control isn't a "real" PCU mode from the EPS state-machine's
     * perspective, so we pin the field to MPPT_CHARGE — the closest
     * analogue, since manual mode is most often used to characterise
     * the panel-side converter. Without this pin, the field would
     * carry whatever value the previous mode last wrote, which would
     * confuse the operator. */
    runtime_state->snapshot.pcu_mode = (uint8_t)EPS_PCU_MODE_MPPT_CHARGE;

    /* Mirror every requested manual value into the snapshot now,
     * UNCONDITIONALLY (i.e. even on dev-board builds that don't have
     * the real GPIO drivers). This means the operator sees the full
     * round-trip on every build:
     *
     *   click button -> page POST -> firmware updates manual_X field
     *   -> next iteration of this runner mirrors X into snapshot
     *   -> next status reply -> page renders the new value
     *
     * On mainboard builds, the real eFuse status inputs sampled below
     * (PA18-PA21) overwrite these simulated readbacks with the
     * physical chip's actual reply. On dev board there's nothing to
     * sample, so these mirrored values stay as the readback the page
     * sees — which is exactly what we want for a structural demo of
     * the command path even without real chips. */
    runtime_state->snapshot.status_led_is_on =
        runtime_state->manual_status_led_requested;
    runtime_state->snapshot.panel_efuse_is_enabled =
        runtime_state->manual_pv_switch_requested;
    runtime_state->snapshot.pv_efuse_power_good =
        runtime_state->manual_pv_switch_requested;
    runtime_state->snapshot.pv_efuse_fault_active = 0u;
    runtime_state->snapshot.bat_efuse_power_good =
        runtime_state->manual_bat_switch_requested;
    runtime_state->snapshot.bat_efuse_fault_active = 0u;

    /* === Mainboard-only section ===
     *
     * Everything below this line writes (or reads) real silicon. On
     * the dev board these pins either don't exist or aren't wired to
     * anything useful, so the whole block is compiled out. */
#ifdef __SAMD21J17D__
    /* PV-side eFuse enable (PA16). Drive PA16 high to ASK the
     * TPS25940 to turn on; the chip may refuse if its internal
     * over-current protection is tripped, which is exactly why we
     * re-read PV_PGOOD below to find out whether it actually obeyed. */
    if (runtime_state->manual_pv_switch_requested != 0u)
    {
        request_pv_efuse_enable_on_mainboard();
    }
    else
    {
        request_pv_efuse_disable_on_mainboard();
    }

    /* Battery-side eFuse enable (PA17). Same shape as above for the
     * battery rail. The two eFuses are independent — the operator
     * can have one on and one off. */
    if (runtime_state->manual_bat_switch_requested != 0u)
    {
        request_bat_efuse_enable_on_mainboard();
    }
    else
    {
        request_bat_efuse_disable_on_mainboard();
    }

    /* Status LED (PB22, active-HIGH on the mainboard). Manual control
     * also takes ownership of the heartbeat blink that normally runs
     * in main.c — see app/main.c, where the heartbeat code skips the
     * toggle whenever requested_mode is MANUAL. So while we're in
     * this mode the LED state is exactly whatever the operator last
     * commanded; once we leave, the heartbeat resumes. */
    if (runtime_state->manual_status_led_requested != 0u)
    {
        turn_pb22_status_led_on_mainboard();
    }
    else
    {
        turn_pb22_status_led_off_on_mainboard();
    }

    /* Read the four eFuse status inputs (PA18=PV_PGOOD, PA19=BAT_PGOOD,
     * PA20=~PV_FLT inverted, PA21=~BAT_FLT inverted) and overwrite
     * the simulated readbacks we wrote above with the real chip
     * samples. On mainboard the pins are pulled up by the eFuse
     * chips' open-drain outputs; on this code path we sample them
     * here once per iteration so the next status reply carries fresh
     * values. */
    read_efuse_status_inputs_from_pa18_to_pa21(
        &runtime_state->snapshot.pv_efuse_power_good,
        &runtime_state->snapshot.pv_efuse_fault_active,
        &runtime_state->snapshot.bat_efuse_power_good,
        &runtime_state->snapshot.bat_efuse_fault_active);
#endif
}
