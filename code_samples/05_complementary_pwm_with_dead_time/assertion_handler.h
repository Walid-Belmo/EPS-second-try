/* =============================================================================
 * assertion_handler.h
 * Defines the SATELLITE_ASSERT macro used throughout all firmware modules.
 *
 * An assertion is a sanity check on something that must always be true by
 * design. It fires only when something impossible has happened — a caller
 * bug, memory corruption, or hardware behaving outside specification.
 *
 * Assertions remain active in flight builds. They are the early warning
 * system for unexpected states that cannot be anticipated at design time.
 *
 * Category: PURE LOGIC (no hardware access in this header)
 * =============================================================================
 */

#ifndef ASSERTION_HANDLER_H
#define ASSERTION_HANDLER_H

/* satellite_handle_assertion_failure() is always defined.
 * Its behaviour differs between debug and flight builds:
 *   Debug: logs file/line to UART, then freezes (watchdog resets).
 *   Flight: resets the chip immediately via NVIC_SystemReset(). */
void satellite_handle_assertion_failure(const char *file, int line);

/* SATELLITE_ASSERT(condition)
 * If 'condition' is false, calls the failure handler with the source
 * file name and line number where the macro was written.
 * __FILE__ and __LINE__ are replaced by the compiler at compile time. */
#define SATELLITE_ASSERT(condition)                                      \
    do {                                                                  \
        if (!(condition)) {                                               \
            satellite_handle_assertion_failure(__FILE__, __LINE__);        \
        }                                                                 \
    } while (0)

#endif /* ASSERTION_HANDLER_H */
