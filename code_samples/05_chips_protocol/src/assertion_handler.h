/* =============================================================================
 * assertion_handler.h
 * Provides the SATELLITE_ASSERT macro for runtime sanity checks on invariants.
 *
 * An assertion is a check on something that must always be true by design.
 * It fires only when something impossible has happened — memory corruption,
 * a caller bug, or hardware behaving outside specification.
 *
 * Assertions remain active in flight builds. They are the early warning
 * system for unexpected states that cannot be anticipated at design time.
 *
 * Usage:
 *   SATELLITE_ASSERT(pointer != (void *)0);
 *   SATELLITE_ASSERT(payload_length <= MAXIMUM_PAYLOAD_SIZE);
 *
 * If the condition is true (normal case), nothing happens — zero cost.
 * If the condition is false, satellite_handle_assertion_failure() is called,
 * which logs the file and line number (debug build) or resets the chip
 * (flight build).
 * =============================================================================
 */

#ifndef ASSERTION_HANDLER_H
#define ASSERTION_HANDLER_H

#include <stdint.h>

/* Called when an assertion fails. Behaviour differs between builds:
 *   Debug build:  logs file/line to debug UART, then freezes (watchdog resets)
 *   Flight build: triggers an immediate software reset via NVIC_SystemReset() */
void satellite_handle_assertion_failure(const char *file_where_assertion_failed,
                                        int32_t line_number_of_failed_assertion);

/* The assertion macro. Evaluates the condition. If false, calls the failure
 * handler with the current file name and line number.
 *
 * The do { ... } while (0) wrapper ensures the macro behaves like a single
 * statement in all contexts (if/else, loops, etc.) without syntax surprises.
 *
 * Assertions must be side-effect free — never put anything inside
 * SATELLITE_ASSERT() that changes state. */
#define SATELLITE_ASSERT(condition)                                          \
    do {                                                                      \
        if (!(condition)) {                                                   \
            satellite_handle_assertion_failure(__FILE__, __LINE__);           \
        }                                                                     \
    } while (0)

#endif /* ASSERTION_HANDLER_H */
