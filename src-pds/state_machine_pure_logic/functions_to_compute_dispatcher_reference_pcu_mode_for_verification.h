// =============================================================================
// functions_to_compute_dispatcher_reference_pcu_mode_for_verification.h
//
// Pure function that returns the steady-state PCU mode the dispatcher would
// pick for a given set of inputs. Used as the EXPECTED VALUE oracle by the
// State Transition Page (and re-implemented Python-side in the same page so
// firmware and page can be cross-checked).
//
// Does NOT mutate state. Does NOT consider latches, counters, sub-states, or
// timeouts. Strictly: "given these inputs, which of the 5 modes should the
// machine settle into at steady state?"
//
// Category: PURE LOGIC (no hardware)
// =============================================================================

#ifndef FUNCTIONS_TO_COMPUTE_DISPATCHER_REFERENCE_PCU_MODE_FOR_VERIFICATION_H
#define FUNCTIONS_TO_COMPUTE_DISPATCHER_REFERENCE_PCU_MODE_FOR_VERIFICATION_H

#include <stdint.h>

#include "eps_state_machine.h"
#include "eps_configuration_parameters.h"

// Returns one of enum eps_pcu_operating_mode as uint8_t. May return a synthetic
// value (255) when safety conditions imply SAFE_MODE — callers should check
// the safe_mode_is_active output to disambiguate.
//
// The third argument is the persistent-state-resident heartbeat counter; this
// keeps the function pure (no globals) while still expressing the
// heartbeat-timeout safety condition.
uint8_t compute_dispatcher_reference_pcu_mode_for_verification_only(
    const struct eps_sensor_readings_this_iteration *sensor_readings,
    const struct eps_configuration_thresholds *thresholds,
    uint32_t iterations_since_last_obc_heartbeat,
    uint8_t *safe_mode_is_active_output,
    uint8_t *safe_mode_reason_output);

#endif // FUNCTIONS_TO_COMPUTE_DISPATCHER_REFERENCE_PCU_MODE_FOR_VERIFICATION_H
