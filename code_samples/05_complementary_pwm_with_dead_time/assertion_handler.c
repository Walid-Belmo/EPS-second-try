/* =============================================================================
 * assertion_handler.c
 * Implements the assertion failure handler called by SATELLITE_ASSERT.
 *
 * Category: HARDWARE DRIVER (accesses NVIC for system reset in flight builds)
 * =============================================================================
 */

#include <stdint.h>

#include "samd21g17d.h"

#include "assertion_handler.h"
#include "debug_functions.h"

void satellite_handle_assertion_failure(const char *file, int line)
{
    /* ── Debug build (USB connected, serial terminal open) ──────────────
     * Print the failure location so the developer can read it, then
     * freeze. The watchdog will reset the chip after its timeout.
     * Freezing rather than resetting immediately gives the developer
     * time to read the message before the chip restarts. */
    #ifdef DEBUG_LOGGING_ENABLED
        DEBUG_LOG_TEXT("!!! ASSERTION FAILED — SYSTEM HALTED !!!");
        DEBUG_LOG_TEXT(file);
        DEBUG_LOG_INT("line", (int32_t)line);

        while (1) /* @non-terminating@ */
        {
            /* Waiting for watchdog to reset the chip */
        }
    #endif

    /* ── Flight build (in orbit, no USB) ───────────────────────────────
     * Reset cleanly. The chip comes back up, reads PM->RCAUSE, logs a
     * software reset, and resumes operation. A clean reboot is safer
     * than continuing with potentially corrupted state. */
    #ifndef DEBUG_LOGGING_ENABLED
        (void)file;
        (void)line;
        NVIC_SystemReset();
    #endif
}
