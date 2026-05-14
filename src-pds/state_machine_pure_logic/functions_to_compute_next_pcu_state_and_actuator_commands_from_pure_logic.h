// =============================================================================
// functions_to_compute_next_pcu_state_and_actuator_commands_from_pure_logic.h
//
// Pure dispatcher implementation of the EPS power state machine for the bench
// simulator. One function, no hardware dependencies, deterministic from inputs.
//
// This is NOT the flight-grade state machine. The flight target (per-mode
// bodies with timeouts, hysteresis, entry actions) is described in
// src-pds/state-transitions.md. On a bench with clean injected inputs the
// dispatcher produces the correct steady-state behavior without the per-mode
// machinery, which exists only to handle real-hardware noise and dynamics.
//
// What it does each call:
//   1. Update OBC heartbeat counter (increment or reset).
//   2. Check safety overrides (battery below min, temperature out of range,
//      heartbeat timeout). If any tripped, latch safe mode.
//   3. If currently latched in safe mode, check whether OBC has commanded a
//      non-SAFE satellite mode AND no safety condition is currently active —
//      if so, release the latch.
//   4. If not in safe mode, pick PCU mode from solar availability and battery
//      voltage (Fig 3.4.6 dispatcher in mission doc p.101).
//   5. Compute actuator commands (PWM duty, panel eFuse, heater, load mask,
//      safe alert flag) for the chosen mode and (if in safe) sub-state.
//
// Category: PURE LOGIC (no hardware)
// =============================================================================

#ifndef FUNCTIONS_TO_COMPUTE_NEXT_PCU_STATE_AND_ACTUATOR_COMMANDS_FROM_PURE_LOGIC_H
#define FUNCTIONS_TO_COMPUTE_NEXT_PCU_STATE_AND_ACTUATOR_COMMANDS_FROM_PURE_LOGIC_H

#include "eps_state_machine.h"
#include "eps_configuration_parameters.h"

// Runs one iteration of the pure-dispatcher EPS state machine.
//
// Signature matches the legacy eps_state_machine_run_one_iteration so the
// wrapper can call this as a drop-in replacement.
//
// Reads:  *sensor_readings, *thresholds, *persistent_state (in)
// Writes: *persistent_state (updated counters, mode, sub-state, latch),
//         *actuator_commands_output (PWM, eFuse, heater, loads, safe flags)
//
// No globals. No I/O. Deterministic.
void compute_next_pcu_state_and_actuator_commands_from_inputs_and_current_state(
    struct eps_state_machine_persistent_state *persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds,
    struct eps_actuator_output_commands *actuator_commands_output);

#endif // FUNCTIONS_TO_COMPUTE_NEXT_PCU_STATE_AND_ACTUATOR_COMMANDS_FROM_PURE_LOGIC_H
