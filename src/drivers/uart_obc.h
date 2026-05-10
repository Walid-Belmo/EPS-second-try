/* =============================================================================
 * uart_obc.h
 * Board-selected OBC UART API.
 *
 * The application and CHIPS command code include this generic header. The
 * Makefile selects exactly one implementation file for the current board.
 * =============================================================================
 */

#ifndef UART_OBC_H
#define UART_OBC_H

#include <stdint.h>

void uart_obc_initialize_sercom0_at_115200_baud(void);

void uart_obc_send_bytes(const uint8_t *bytes_to_send,
                         uint32_t number_of_bytes_to_send);

uint32_t uart_obc_number_of_bytes_available_in_receive_buffer(void);

uint8_t uart_obc_read_one_byte_from_receive_buffer(void);

#endif /* UART_OBC_H */
