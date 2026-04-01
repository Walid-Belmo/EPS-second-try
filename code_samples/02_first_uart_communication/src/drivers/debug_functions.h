/* =============================================================================
 * debug_functions.h
 * Non-blocking DMA-based debug logging over UART (SERCOM5, PA22, 115200 baud).
 *
 * In debug builds (-DDEBUG_LOGGING_ENABLED): macros call real functions that
 * write to a circular buffer drained by DMA. CPU cost: ~1 microsecond per call.
 *
 * In flight builds (no define): macros compile to nothing — zero bytes,
 * zero CPU cost. All logging disappears from the binary entirely.
 * =============================================================================
 */

#ifndef DEBUG_FUNCTIONS_H
#define DEBUG_FUNCTIONS_H

#include <stdint.h>

#ifdef DEBUG_LOGGING_ENABLED

    void debug_log_initialize_dma_uart_on_sercom5_pa22(void);
    void debug_log_write_text_line(const char *message);
    void debug_log_write_labeled_unsigned_integer(const char *label, uint32_t value);
    void debug_log_write_labeled_signed_integer(const char *label, int32_t value);

    #define DEBUG_LOG_INIT() \
        debug_log_initialize_dma_uart_on_sercom5_pa22()

    #define DEBUG_LOG_TEXT(msg) \
        debug_log_write_text_line(msg)

    #define DEBUG_LOG_UINT(label, val) \
        debug_log_write_labeled_unsigned_integer(label, val)

    #define DEBUG_LOG_INT(label, val) \
        debug_log_write_labeled_signed_integer(label, val)

#else

    #define DEBUG_LOG_INIT()            ((void)0)
    #define DEBUG_LOG_TEXT(msg)          ((void)0)
    #define DEBUG_LOG_UINT(label, val)   ((void)0)
    #define DEBUG_LOG_INT(label, val)    ((void)0)

#endif /* DEBUG_LOGGING_ENABLED */

#endif /* DEBUG_FUNCTIONS_H */
