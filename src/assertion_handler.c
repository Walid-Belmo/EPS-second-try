/* =============================================================================
 * assertion_handler.c
 * Handles assertion failures by logging the location and halting (debug)
 * or resetting the chip (flight).
 *
 * Category: UTILITY (no hardware registers accessed directly)
 *
 * In debug builds (-DDEBUG_LOGGING_ENABLED), the failure handler sends the
 * file name and line number to the debug UART (SERCOM5/PA22) so the developer
 * can read it on PuTTY, then freezes the CPU. The watchdog timer (once
 * configured in Phase 8) will reset the chip after its timeout period.
 * Freezing rather than resetting immediately gives the developer time to
 * read the message before the chip restarts.
 *
 * In flight builds (no DEBUG_LOGGING_ENABLED), there is no one watching
 * a terminal. The best action is to reset immediately and resume operation.
 * The chip comes back up, reads the reset cause register (PM_REGS->PM_RCAUSE),
 * and can report "software reset" in the next telemetry packet to ground.
 * =============================================================================
 */

#include <stdint.h>

#include "samd21g17d.h"
#include "debug_functions.h"
#include "assertion_handler.h"

void satellite_handle_assertion_failure(const char *file_where_assertion_failed,
                                        int32_t line_number_of_failed_assertion)
{
    /* ── Debug build (USB connected, PuTTY open) ─────────────────────── */
#ifdef DEBUG_LOGGING_ENABLED
    DEBUG_LOG_TEXT("!!! ASSERTION FAILED — SYSTEM HALTED !!!");
    DEBUG_LOG_TEXT(file_where_assertion_failed);
    DEBUG_LOG_INT("line", line_number_of_failed_assertion);

    /* Freeze the CPU. The developer reads the message on PuTTY before
     * the watchdog resets the chip. Without this freeze, the chip would
     * reset instantly and the message would scroll off screen. */
    while (1) /* @non-terminating@ */
    {
        /* Intentionally empty — waiting for watchdog or manual reset. */
    }
#endif

    /* ── Flight build (in orbit, OBC listening on mission UART) ─────── */
#ifndef DEBUG_LOGGING_ENABLED
    /* Suppress unused parameter warnings in flight builds where
     * there is no logging to use the file/line information. */
    (void)file_where_assertion_failed;
    (void)line_number_of_failed_assertion;

    /* ARM CMSIS function: triggers an immediate software reset.
     * The chip reboots in milliseconds and resumes normal operation. */
    NVIC_SystemReset();
#endif
}
