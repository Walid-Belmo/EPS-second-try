/* =============================================================================
 * pwm_buck_converter_disabled_stub.c
 *
 * No-op implementation of the PWM driver API. Compiled into the
 * `devboard-pds` build target only; the mainboard build links the real
 * driver in pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c instead.
 *
 * Why this exists: the rest of the firmware (mode runners, the
 * apply_outputs_to_board step) calls
 *   pwm_buck_converter_initialize_for_demo()
 *   pwm_buck_converter_set_duty_cycle(N)
 * unconditionally. On the dev board there is no buck converter, no
 * EPC2152 gate driver, and the PA12/PA13 pins aren't wired to
 * anything that interprets PWM. If we let the real TCC0 driver run
 * there, the dev board's pins would still oscillate at 300 kHz —
 * harmless, but noisy on a scope and confusing during bring-up.
 *
 * The Makefile selects this stub vs. the real driver via the BOARD
 * variable; both files implement the same API declared in
 * pwm_buck_converter.h, so the rest of the firmware compiles
 * unchanged for either build target.
 *
 * BUILD TARGET: devboard-pds (and also `devboard`).
 * =============================================================================
 */

#include <stdint.h>

#include "pwm_buck_converter.h"

/*
 * Init: nothing to do. The dev-board build still calls this from the
 * boot sequence, but there are no hardware registers to configure.
 */
void pwm_buck_converter_initialize_for_demo(void)
{
}

/*
 * Set duty cycle: nothing to do. The cast-to-void on the argument
 * silences the -Wunused-parameter warning that -Werror would otherwise
 * promote to a build failure.
 */
void pwm_buck_converter_set_duty_cycle(
    uint16_t duty_cycle_as_fraction_of_65535)
{
    (void)duty_cycle_as_fraction_of_65535;
}
