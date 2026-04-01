/* =============================================================================
 * uart_obc_sercom0_pa04_pa05.c
 * Non-blocking bidirectional UART driver for OBC communication.
 * Transmit and receive are both interrupt-driven using ring buffers.
 * The CPU writes to RAM buffers and returns immediately; the SERCOM0
 * interrupt handler moves bytes between the buffers and the hardware.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: SERCOM0 (UART, bidirectional)
 * Pins: PA04 (TX, SERCOM0 PAD[0], mux D), PA05 (RX, SERCOM0 PAD[1], mux D)
 * Clock: GCLK0 (48 MHz) connected to SERCOM0 functional clock
 * Interrupt: SERCOM0_Handler — fires on RXC (byte received) and DRE (ready
 *            to accept next transmit byte)
 *
 * Verified against:
 *   - SAMD21 datasheet Table 7-1 (p.29): PA04 = SERCOM0/PAD[0] mux D,
 *     PA05 = SERCOM0/PAD[1] mux D
 *   - SAMD21 datasheet Section 26.6.2.4 (p.421): TxDATA and RxDATA are
 *     separate hardware registers behind the same DATA address
 *   - DM320119 User Guide Table 4-4 (p.13): PA04/PA05 have no debugger
 *     connections
 * =============================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "samd21g17d.h"
#include "uart_obc_sercom0_pa04_pa05.h"

/* ── Constants ────────────────────────────────────────────────────────────── */

/* Buffer sizes must be powers of 2 for bitmask modulo. 256 bytes each
 * supports typical CHIPS protocol frames (max ~1030 bytes after byte
 * stuffing) with flow at 115200 baud. */
#define OBC_UART_BUFFER_SIZE_IN_BYTES         256u
#define OBC_UART_BUFFER_INDEX_MASK            (OBC_UART_BUFFER_SIZE_IN_BYTES - 1u)

#define TRANSMIT_PIN_NUMBER                   4u   /* PA04 */
#define RECEIVE_PIN_NUMBER                    5u   /* PA05 */

/* Mux D = 0x3 selects SERCOM-ALT function. For PA04 this routes to
 * SERCOM0 PAD[0] (transmit). For PA05 this routes to SERCOM0 PAD[1]
 * (receive). Verified from SAMD21 datasheet Table 7-1, page 29. */
#define PORT_MUX_FUNCTION_D_FOR_SERCOM_ALT    3u

/* BAUD = 65536 * (1 - 16 * (115200 / 48000000)) = 63019.
 * Timing error < 0.01%. UART tolerates up to ~2%. */
#define BAUD_REGISTER_VALUE_FOR_115200        63019u

/* Maximum iterations for bounded polling loops (conventions Rule C2). */
#define MAXIMUM_SYNC_WAIT_ITERATIONS          100000u

/* ── Module state ─────────────────────────────────────────────────────────── */

static struct uart_obc_module_state {
    /* Receive ring buffer — interrupt handler writes, main loop reads. */
    uint8_t           receive_buffer[OBC_UART_BUFFER_SIZE_IN_BYTES];
    volatile uint32_t receive_write_index;
    volatile uint32_t receive_read_index;
    volatile uint8_t  receive_overflow_has_occurred;

    /* Transmit ring buffer — main loop writes, interrupt handler reads. */
    uint8_t           transmit_buffer[OBC_UART_BUFFER_SIZE_IN_BYTES];
    volatile uint32_t transmit_write_index;
    volatile uint32_t transmit_read_index;
} obc_uart_state;

/* ── Private function prototypes ──────────────────────────────────────────── */

static void enable_sercom0_bus_clock_for_register_access(void);
static void connect_48mhz_gclk0_to_sercom0_functional_clock(void);
static void configure_pa04_as_sercom0_uart_transmit_pin(void);
static void configure_pa05_as_sercom0_uart_receive_pin(void);
static void reset_sercom0_to_known_clean_state(void);
static void configure_sercom0_as_bidirectional_uart_at_115200_baud(void);
static void enable_sercom0_uart_and_receive_interrupt(void);

/* ── Interrupt handler ────────────────────────────────────────────────────── */

/* SERCOM0_Handler is called by the hardware interrupt controller whenever
 * SERCOM0 has a pending interrupt. Two events can trigger it:
 *   - RXC (Receive Complete): a byte arrived on the RX wire and is sitting
 *     in the DATA register, ready to be read into our receive buffer.
 *   - DRE (Data Register Empty): the transmit DATA register is empty and
 *     ready to accept the next byte from our transmit buffer.
 * Both events are handled in a single call if they occur simultaneously. */
void SERCOM0_Handler(void);

void SERCOM0_Handler(void)
{
    uint8_t interrupt_flags = SERCOM0_REGS->USART_INT.SERCOM_INTFLAG;

    /* ── Handle received byte (RXC flag) ─────────────────────────────── */
    if ((interrupt_flags & SERCOM_USART_INT_INTFLAG_RXC_Msk) != 0u)
    {
        /* Reading DATA clears the RXC flag automatically (datasheet
         * Section 26.8.6). No explicit flag clear is needed. */
        uint8_t received_byte =
            (uint8_t)SERCOM0_REGS->USART_INT.SERCOM_DATA;

        uint32_t next_write =
            (obc_uart_state.receive_write_index + 1u)
            & OBC_UART_BUFFER_INDEX_MASK;

        if (next_write != obc_uart_state.receive_read_index)
        {
            obc_uart_state.receive_buffer[
                obc_uart_state.receive_write_index] = received_byte;
            obc_uart_state.receive_write_index = next_write;
        }
        else
        {
            obc_uart_state.receive_overflow_has_occurred = 1u;
        }
    }

    /* ── Handle transmit ready (DRE flag) ────────────────────────────── */
    if ((interrupt_flags & SERCOM_USART_INT_INTFLAG_DRE_Msk) != 0u)
    {
        if (obc_uart_state.transmit_read_index !=
            obc_uart_state.transmit_write_index)
        {
            /* Load next byte from transmit buffer into DATA register.
             * Writing to DATA clears the DRE flag and the hardware
             * begins sending the byte on the TX wire immediately. */
            SERCOM0_REGS->USART_INT.SERCOM_DATA =
                (uint16_t)obc_uart_state.transmit_buffer[
                    obc_uart_state.transmit_read_index];

            obc_uart_state.transmit_read_index =
                (obc_uart_state.transmit_read_index + 1u)
                & OBC_UART_BUFFER_INDEX_MASK;
        }
        else
        {
            /* Transmit buffer is empty. Disable the DRE interrupt to
             * prevent it from firing continuously when there is nothing
             * to send. The next call to uart_obc_send_bytes() will
             * re-enable it. */
            SERCOM0_REGS->USART_INT.SERCOM_INTENCLR =
                SERCOM_USART_INT_INTENCLR_DRE_Msk;
        }
    }
}

/* ── Public functions ─────────────────────────────────────────────────────── */

void uart_obc_initialize_sercom0_at_115200_baud(void)
{
    /* Zero all module state so buffers start empty. */
    obc_uart_state.receive_write_index = 0u;
    obc_uart_state.receive_read_index = 0u;
    obc_uart_state.receive_overflow_has_occurred = 0u;
    obc_uart_state.transmit_write_index = 0u;
    obc_uart_state.transmit_read_index = 0u;

    enable_sercom0_bus_clock_for_register_access();
    connect_48mhz_gclk0_to_sercom0_functional_clock();
    configure_pa04_as_sercom0_uart_transmit_pin();
    configure_pa05_as_sercom0_uart_receive_pin();
    reset_sercom0_to_known_clean_state();
    configure_sercom0_as_bidirectional_uart_at_115200_baud();
    enable_sercom0_uart_and_receive_interrupt();
}

void uart_obc_send_bytes(const uint8_t *bytes_to_send,
                         uint32_t number_of_bytes_to_send)
{
    if (bytes_to_send == (void *)0)
    {
        return;
    }

    /* Copy bytes into the transmit ring buffer. If the buffer becomes
     * full, remaining bytes are silently dropped rather than blocking
     * the CPU. A full buffer means we are producing data faster than
     * the wire can send it (unlikely at 115200 baud with normal usage). */
    for (uint32_t i = 0u; i < number_of_bytes_to_send; i += 1u)
    {
        uint32_t next_write =
            (obc_uart_state.transmit_write_index + 1u)
            & OBC_UART_BUFFER_INDEX_MASK;

        if (next_write == obc_uart_state.transmit_read_index)
        {
            break;  /* Buffer full — drop remaining bytes. */
        }

        obc_uart_state.transmit_buffer[
            obc_uart_state.transmit_write_index] = bytes_to_send[i];
        obc_uart_state.transmit_write_index = next_write;
    }

    /* Enable the DRE (Data Register Empty) interrupt. This tells the
     * SERCOM0 hardware: "interrupt me when the transmit DATA register
     * is empty so I can load the next byte." The interrupt handler will
     * disable this interrupt again once the buffer is fully drained. */
    SERCOM0_REGS->USART_INT.SERCOM_INTENSET =
        SERCOM_USART_INT_INTENSET_DRE_Msk;
}

uint32_t uart_obc_number_of_bytes_available_in_receive_buffer(void)
{
    return (obc_uart_state.receive_write_index
            - obc_uart_state.receive_read_index)
           & OBC_UART_BUFFER_INDEX_MASK;
}

uint8_t uart_obc_read_one_byte_from_receive_buffer(void)
{
    if (uart_obc_number_of_bytes_available_in_receive_buffer() == 0u)
    {
        return 0u;
    }

    uint8_t byte_read = obc_uart_state.receive_buffer[
        obc_uart_state.receive_read_index];

    obc_uart_state.receive_read_index =
        (obc_uart_state.receive_read_index + 1u)
        & OBC_UART_BUFFER_INDEX_MASK;

    return byte_read;
}

/* ── Private functions — SERCOM0 setup ───────────────────────────────────── */

static void enable_sercom0_bus_clock_for_register_access(void)
{
    /* SERCOM0 sits on the APB C bus. By default its clock gate is closed,
     * meaning the CPU cannot read or write SERCOM0 registers at all.
     * Setting this bit opens the gate. */
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_SERCOM0_Msk;
}

static void connect_48mhz_gclk0_to_sercom0_functional_clock(void)
{
    /* The bus clock (above) lets the CPU access SERCOM0 registers.
     * The functional clock is what the SERCOM actually uses to generate
     * baud timing and shift bits on the wire. Without it, the UART does
     * not operate even though registers appear writable.
     *
     * SERCOM0_GCLK_ID_CORE = 20 is the clock channel ID for SERCOM0. */
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk                                 |
        GCLK_CLKCTRL_GEN_GCLK0                                 |
        GCLK_CLKCTRL_ID(SERCOM0_GCLK_ID_CORE);

    /* This write crosses a clock domain boundary. Any SERCOM0 register
     * write before SYNCBUSY clears is silently discarded. */
    for (uint32_t i = 0u; i < MAXIMUM_SYNC_WAIT_ITERATIONS; i += 1u)
    {
        if ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) == 0u)
        {
            break;
        }
    }
}

static void configure_pa04_as_sercom0_uart_transmit_pin(void)
{
    /* PA04 defaults to GPIO after reset. We need to route it to SERCOM0.
     *
     * Step 1: Enable the peripheral multiplexer on PA04 (PMUXEN bit).
     *         Without this, the pin stays as GPIO regardless of the
     *         multiplexer setting.
     *
     * Step 2: Set the multiplexer function to D (SERCOM-ALT, value 0x3).
     *         For PA04, mux D routes the pin to SERCOM0 PAD[0].
     *         TXPO=0 in CTRLA will assign PAD[0] as the transmit output.
     *
     * PA04 is an even-numbered pin. PMUX register index = 4/2 = 2.
     * The even pin uses the lower nibble (bits 3:0) of PMUX[2]. */
    PORT_REGS->GROUP[0].PORT_PINCFG[TRANSMIT_PIN_NUMBER] |=
        PORT_PINCFG_PMUXEN_Msk;

    uint8_t current_pmux_value = PORT_REGS->GROUP[0].PORT_PMUX[2];
    /* Clear the even nibble (bits 3:0), preserve the odd nibble. */
    current_pmux_value &= 0xF0u;
    /* Set even nibble to mux D (0x03). */
    current_pmux_value |= PORT_MUX_FUNCTION_D_FOR_SERCOM_ALT;
    PORT_REGS->GROUP[0].PORT_PMUX[2] = current_pmux_value;
}

static void configure_pa05_as_sercom0_uart_receive_pin(void)
{
    /* PA05 must be configured as a SERCOM input pin.
     *
     * CRITICAL: In addition to PMUXEN, the INEN (Input Enable) bit must
     * be set in PINCFG. Without INEN, the pin's input buffer is disabled
     * and the SERCOM hardware cannot sample the voltage level on the wire.
     * The transmit pin (PA04) does not need INEN because it is an output.
     *
     * PA05 is an odd-numbered pin. PMUX register index = 5/2 = 2.
     * The odd pin uses the upper nibble (bits 7:4) of PMUX[2]. */
    PORT_REGS->GROUP[0].PORT_PINCFG[RECEIVE_PIN_NUMBER] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;

    uint8_t current_pmux_value = PORT_REGS->GROUP[0].PORT_PMUX[2];
    /* Clear the odd nibble (bits 7:4), preserve the even nibble. */
    current_pmux_value &= 0x0Fu;
    /* Set odd nibble to mux D (0x03). Shifted left by 4 for upper nibble. */
    current_pmux_value |=
        (uint8_t)(PORT_MUX_FUNCTION_D_FOR_SERCOM_ALT << 4u);
    PORT_REGS->GROUP[0].PORT_PMUX[2] = current_pmux_value;
}

static void reset_sercom0_to_known_clean_state(void)
{
    /* Software reset clears all SERCOM0 registers to their power-on
     * defaults. This ensures no leftover state from a previous boot,
     * watchdog reset, or debugger session affects the configuration. */
    SERCOM0_REGS->USART_INT.SERCOM_CTRLA =
        SERCOM_USART_INT_CTRLA_SWRST_Msk;

    /* The reset propagates across clock domains. Any write to SERCOM0
     * before SYNCBUSY clears is silently discarded. */
    for (uint32_t i = 0u; i < MAXIMUM_SYNC_WAIT_ITERATIONS; i += 1u)
    {
        if ((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY
             & SERCOM_USART_INT_SYNCBUSY_SWRST_Msk) == 0u)
        {
            break;
        }
    }
}

static void configure_sercom0_as_bidirectional_uart_at_115200_baud(void)
{
    /* CTRLA: master configuration — mode, pin routing, bit order.
     * Must be written while the peripheral is disabled (ENABLE=0). */
    SERCOM0_REGS->USART_INT.SERCOM_CTRLA =
        SERCOM_USART_INT_CTRLA_DORD_LSB             |  /* LSB first (standard UART) */
        SERCOM_USART_INT_CTRLA_MODE_USART_INT_CLK   |  /* internal clock for baud generation */
        SERCOM_USART_INT_CTRLA_TXPO(0u)              |  /* TXPO=0: transmit on PAD[0] = PA04 */
        SERCOM_USART_INT_CTRLA_RXPO(1u);                /* RXPO=1: receive on PAD[1] = PA05 */

    /* CTRLB: enable both the transmitter and receiver. */
    SERCOM0_REGS->USART_INT.SERCOM_CTRLB =
        SERCOM_USART_INT_CTRLB_TXEN_Msk             |  /* enable the transmit hardware */
        SERCOM_USART_INT_CTRLB_RXEN_Msk;               /* enable the receive hardware */

    /* CTRLB crosses a clock domain. Must wait before proceeding or the
     * write is silently discarded. */
    for (uint32_t i = 0u; i < MAXIMUM_SYNC_WAIT_ITERATIONS; i += 1u)
    {
        if ((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY
             & SERCOM_USART_INT_SYNCBUSY_CTRLB_Msk) == 0u)
        {
            break;
        }
    }

    /* BAUD register: sets the bit rate.
     * BAUD = 65536 * (1 - 16 * (115200 / 48000000)) = 63019.
     * Actual timing error: < 0.01%. UART tolerates up to ~2%. */
    SERCOM0_REGS->USART_INT.SERCOM_BAUD =
        BAUD_REGISTER_VALUE_FOR_115200;
}

static void enable_sercom0_uart_and_receive_interrupt(void)
{
    /* Activate the UART peripheral. Before this, no data is sent or
     * received even though all configuration registers are written. */
    SERCOM0_REGS->USART_INT.SERCOM_CTRLA |=
        SERCOM_USART_INT_CTRLA_ENABLE_Msk;

    /* ENABLE propagates across clock domains. Using the UART before this
     * clears results in it appearing enabled but not functioning. */
    for (uint32_t i = 0u; i < MAXIMUM_SYNC_WAIT_ITERATIONS; i += 1u)
    {
        if ((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY
             & SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk) == 0u)
        {
            break;
        }
    }

    /* Enable the RXC (Receive Complete) interrupt. This tells SERCOM0:
     * "interrupt the CPU every time a byte arrives on the RX wire."
     *
     * The DRE (Data Register Empty) interrupt is NOT enabled here.
     * It will be enabled on-demand by uart_obc_send_bytes() only when
     * there are bytes in the transmit buffer waiting to be sent. */
    SERCOM0_REGS->USART_INT.SERCOM_INTENSET =
        SERCOM_USART_INT_INTENSET_RXC_Msk;

    /* Enable the SERCOM0 interrupt line in the ARM interrupt controller.
     * Without this, the SERCOM0 hardware can set flags all it wants but
     * the CPU will never jump to SERCOM0_Handler. */
    NVIC_EnableIRQ(SERCOM0_IRQn);
}
