// =============================================================================
// assertion_handler.h
// Provides the SATELLITE_ASSERT macro used by all firmware modules to check
// invariants at runtime. Assertions remain active in flight builds.
//
// Category: PURE LOGIC (no hardware)
// =============================================================================

#ifndef ASSERTION_HANDLER_H
#define ASSERTION_HANDLER_H

#include <stdint.h>

// satellite_handle_assertion_failure() is always defined.
// On desktop (simulation): prints file and line to stderr, then exits.
// On chip (debug build): logs to PuTTY, then freezes for watchdog reset.
// On chip (flight build): notifies OBC, then triggers software reset.
void satellite_handle_assertion_failure(const char *file_path, int32_t line_number);

#define SATELLITE_ASSERT(condition)                                            \
    do {                                                                        \
        if (!(condition)) {                                                     \
            satellite_handle_assertion_failure(__FILE__, __LINE__);             \
        }                                                                       \
    } while (0)

#endif // ASSERTION_HANDLER_H
