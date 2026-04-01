/* =============================================================================
 * uart_obc_sercom0_pa04_pa05.h
 * Non-blocking bidirectional UART driver for OBC communication.
 * Uses SERCOM0 on PA04 (transmit) and PA05 (receive) at 115200 baud.
 *
 * Both transmit and receive are interrupt-driven with ring buffers.
 * The CPU never blocks — it writes to a RAM buffer and the SERCOM0 interrupt
 * handler drains the transmit buffer and fills the receive buffer in the
 * background.
 * =============================================================================
 */

#ifndef UART_OBC_SERCOM0_PA04_PA05_H
#define UART_OBC_SERCOM0_PA04_PA05_H

#include <stdint.h>

/* Initialize SERCOM0 as a bidirectional UART at 115200 baud.
 * Configures PA04 as transmit, PA05 as receive, enables the receive
 * complete interrupt, and registers the SERCOM0 interrupt in the
 * interrupt controller. Must be called after the 48 MHz clock is running. */
void uart_obc_initialize_sercom0_at_115200_baud(void);

/* Queue bytes for transmission. Copies the data into an internal transmit
 * ring buffer and enables the transmit interrupt. Returns immediately —
 * the interrupt handler sends bytes in the background.
 * If the buffer does not have enough space, excess bytes are silently dropped. */
void uart_obc_send_bytes(const uint8_t *bytes_to_send,
                         uint32_t number_of_bytes_to_send);

/* Return the number of bytes currently waiting in the receive ring buffer.
 * Zero means no data has been received since the last read. */
uint32_t uart_obc_number_of_bytes_available_in_receive_buffer(void);

/* Read and remove one byte from the receive ring buffer.
 * The caller must check uart_obc_number_of_bytes_available_in_receive_buffer()
 * first — calling this when the buffer is empty returns 0 and is a bug. */
uint8_t uart_obc_read_one_byte_from_receive_buffer(void);

#endif /* UART_OBC_SERCOM0_PA04_PA05_H */
