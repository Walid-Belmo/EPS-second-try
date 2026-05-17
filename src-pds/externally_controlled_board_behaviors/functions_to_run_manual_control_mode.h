#ifndef FUNCTIONS_TO_RUN_MANUAL_CONTROL_MODE_H
#define FUNCTIONS_TO_RUN_MANUAL_CONTROL_MODE_H

/*
 * Runs one iteration of manual-control mode.
 *
 * Manual-control mode lets the operator drive each of the four user-facing
 * board outputs independently from the web UI:
 *
 *   - PWM duty cycle (always available; the only one that exists on the
 *     dev-board build target as well as the mainboard)
 *   - PV-side eFuse enable      (mainboard only)
 *   - Battery-side eFuse enable (mainboard only)
 *   - Status LED (PB22)         (mainboard only)
 *
 * On every loop iteration this function copies the four "manual_*" requested
 * values from runtime state into the corresponding hardware drivers and
 * samples the four eFuse status inputs (PV/BAT power-good and fault flags)
 * back into the runtime snapshot so the UI can show what the chip actually
 * did. The set_manual_* command handlers only update the runtime fields;
 * the actual hardware writes happen here.
 */
void run_manual_control_mode(void);

#endif /* FUNCTIONS_TO_RUN_MANUAL_CONTROL_MODE_H */
