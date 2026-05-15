/* =============================================================================
 * uart_obc_sercom0_pa10_pa11_on_mainboard.c
 *
 * Non-blocking bidirectional UART driver for the OBC link. This is the wire
 * the operator's commands and the firmware's status replies travel over:
 *
 *     [computer USB serial] --- [ESP32 USB-to-UART bridge] --- [SAMD21 UART]
 *                                                                    |
 *                                                  THIS DRIVER lives here
 *
 * BUILD TARGET: mainboard only.
 *
 * Pinout, mux choice, and TXPO/RXPO selection (verified against
 * docs/mainboard_pinout_pcu_v4_1.md):
 *   PA10 = UART_TX = SERCOM0 PAD[2], peripheral mux C, TXPO = 1
 *   PA11 = UART_RX = SERCOM0 PAD[3], peripheral mux C, RXPO = 3
 *
 * Why those numbers (none of them are arbitrary):
 *   - PAD[2] / PAD[3] is the only PAD pair on SERCOM0 that PA10 / PA11
 *     can connect to. The PAD-to-pin mapping is fixed in silicon.
 *   - "Mux C" is the SAMD21 peripheral-mux setting that routes the pin
 *     to a SERCOM peripheral function instead of GPIO. The mux table for
 *     PA10/PA11 lists C as SERCOM0; other muxes would route to a
 *     different SERCOM, the EIC, or stay GPIO.
 *   - TXPO = 1 tells SERCOM0 "transmit on PAD[2]"; TXPO = 0 would mean
 *     PAD[0]. We're using PAD[2] for TX, so TXPO = 1.
 *   - RXPO = 3 tells SERCOM0 "receive on PAD[3]". The four possible
 *     RXPO values map to PAD[0..3] respectively.
 *
 * I/O architecture: both directions are interrupt-driven through ring
 * buffers. The CPU never blocks on UART I/O — it just reads or writes
 * RAM and lets the SERCOM0 ISR do the actual byte shuffling.
 *
 *   RX path: SERCOM0_Handler (RXC interrupt) reads one byte from the
 *            DATA register, stores it in receive_buffer at write_index,
 *            advances write_index. Main loop's read_one_byte_from_buffer
 *            reads from read_index and advances read_index.
 *
 *   TX path: send_bytes() copies caller bytes into transmit_buffer at
 *            write_index, advances write_index, enables the DRE
 *            interrupt. SERCOM0_Handler (DRE interrupt) reads from
 *            transmit_buffer at read_index, writes to DATA, advances
 *            read_index. When the buffer empties, the ISR DISABLES
 *            the DRE interrupt — otherwise it would fire continuously
 *            with nothing to send.
 *
 * Buffer sizes are 256 bytes each, chosen as the smallest power of 2
 * that comfortably holds one full CHIPS frame plus a margin. Power of 2
 * lets the wrap-around use a bitmask (& INDEX_MASK) instead of a divide.
 *
 * BAUD register magic number derivation (115200 baud on a 48 MHz clock):
 *   BAUD = 65536 × (1 - 16 × baud / clock)
 *        = 65536 × (1 - 16 × 115200 / 48000000)
 *        = 65536 × (1 - 0.0384)
 *        = 65536 × 0.9616
 *        = 63019  (rounded)
 *
 *   Resulting actual baud rate is 115200 to within ~0.01%. Standard UARTs
 *   tolerate ~3% mismatch before frame errors appear, so we have a wide
 *   margin against temperature drift and DFLL open-loop variation.
 * =============================================================================
 */

#include <stdint.h>
#include "samd21j17d.h"
#include "uart_obc_sercom0_pa10_pa11_on_mainboard.h"

/* Power of 2 so wrap-around uses bitmask instead of modulo (cheaper on
 * Cortex-M0+ which has no hardware divider). */
#define OBC_UART_BUFFER_SIZE_IN_BYTES          256u
#define OBC_UART_BUFFER_INDEX_MASK             (OBC_UART_BUFFER_SIZE_IN_BYTES - 1u)

#define TRANSMIT_PIN_NUMBER                    10u  /* PA10 */
#define RECEIVE_PIN_NUMBER                     11u  /* PA11 */

/* Mux value for PORT_PMUX. The SAMD21 PMUX register packs two pins per
 * byte: even-numbered pins in the low nibble, odd in the high nibble.
 * Value 2 means "function C" (SERCOM0 for both PA10 and PA11). */
#define PORT_MUX_FUNCTION_C_FOR_SERCOM         2u

/* See header comment for the derivation. 63019 = 0xF62B. */
#define BAUD_REGISTER_VALUE_FOR_115200         63019u

/* Bound on the SYNCBUSY polling spins. Real syncs settle in tens of
 * cycles; 100k is just "loud enough that a deadlocked sync exits
 * cleanly instead of hanging the firmware loop." */
#define MAXIMUM_SYNC_WAIT_ITERATIONS           100000u

/* All UART module state in one struct (per the conventions doc). The
 * ring-buffer indices are volatile because the ISR writes them and
 * the main loop reads them; without volatile the compiler could cache
 * a stale value in a register and the main loop would never see new
 * arrivals. */
static struct uart_obc_module_state {
    uint8_t           receive_buffer[OBC_UART_BUFFER_SIZE_IN_BYTES];
    volatile uint32_t receive_write_index;          /* ISR writes, main reads */
    volatile uint32_t receive_read_index;           /* main writes, ISR reads */
    volatile uint8_t  receive_overflow_has_occurred;/* ISR sets if buffer full */

    uint8_t           transmit_buffer[OBC_UART_BUFFER_SIZE_IN_BYTES];
    volatile uint32_t transmit_write_index;         /* main writes, ISR reads */
    volatile uint32_t transmit_read_index;          /* ISR writes, main reads */
} obc_uart_state;

static void enable_sercom0_bus_clock_for_register_access(void);
static void connect_48mhz_gclk0_to_sercom0_functional_clock(void);
static void configure_pa10_as_sercom0_uart_transmit_pin(void);
static void configure_pa11_as_sercom0_uart_receive_pin(void);
static void reset_sercom0_to_known_clean_state(void);
static void configure_sercom0_as_bidirectional_uart_at_115200_baud(void);
static void enable_sercom0_uart_and_receive_interrupt(void);

/*
 * SERCOM0 ISR. Fires whenever:
 *   - RXC (Receive Complete): a byte arrived, DATA register holds it
 *     and will be overwritten if we don't read it before the next byte
 *     finishes shifting in.
 *   - DRE (Data Register Empty): the transmitter has finished sending
 *     the previous byte and is ready for the next one. Auto-disabled
 *     by the ISR when the transmit buffer is empty (otherwise this
 *     interrupt would re-fire endlessly with nothing to send).
 *
 * Both branches are non-blocking and sub-microsecond — they read or
 * write one byte and update a ring-buffer index. Per the conventions
 * doc, ISRs do nothing else.
 *
 * The function is declared as a weak alias to Dummy_Handler in the
 * startup file; defining it here overrides the alias so the linker
 * uses our version.
 */
void SERCOM0_Handler(void);

void SERCOM0_Handler(void)
{
    /* Read INTFLAG once into a local so we don't keep re-reading the
     * register inside the ifs (each register read crosses the AHB/APB
     * boundary and is slow relative to RAM access). */
    uint8_t interrupt_flags = SERCOM0_REGS->USART_INT.SERCOM_INTFLAG;

    if ((interrupt_flags & SERCOM_USART_INT_INTFLAG_RXC_Msk) != 0u)
    {
        /* Reading DATA implicitly clears the RXC flag — that's how the
         * SAMD21 architects chose to ack the interrupt. Casting to
         * uint8_t drops the receive-error bits packed into the upper
         * half of DATA; we don't act on them today. */
        uint8_t received_byte =
            (uint8_t)SERCOM0_REGS->USART_INT.SERCOM_DATA;

        /* Compute the next write index BEFORE storing, so the
         * "buffer full" check below uses the post-store position. */
        uint32_t next_write =
            (obc_uart_state.receive_write_index + 1u)
            & OBC_UART_BUFFER_INDEX_MASK;

        /* "Buffer full" condition: write index would overrun read
         * index. If that happens we drop the byte and set the
         * overflow flag — the alternative (overwrite the oldest byte)
         * would silently corrupt a partially-parsed frame, which is
         * worse than losing one. */
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

    if ((interrupt_flags & SERCOM_USART_INT_INTFLAG_DRE_Msk) != 0u)
    {
        if (obc_uart_state.transmit_read_index !=
            obc_uart_state.transmit_write_index)
        {
            /* Pop one byte from the transmit ring buffer, write it to
             * DATA, advance read_index. Writing DATA implicitly
             * clears the DRE flag and starts the next character
             * shifting out the TX pin. */
            SERCOM0_REGS->USART_INT.SERCOM_DATA =
                (uint16_t)obc_uart_state.transmit_buffer[
                    obc_uart_state.transmit_read_index];

            obc_uart_state.transmit_read_index =
                (obc_uart_state.transmit_read_index + 1u)
                & OBC_UART_BUFFER_INDEX_MASK;
        }
        else
        {
            /* Buffer empty — disable the DRE interrupt so the ISR
             * doesn't spin re-firing with nothing to send. The next
             * call to uart_obc_send_bytes will re-enable it. */
            SERCOM0_REGS->USART_INT.SERCOM_INTENCLR =
                SERCOM_USART_INT_INTENCLR_DRE_Msk;
        }
    }
}

/*
 * One-shot bring-up. Called once at boot from
 * setup_sercom0_uart_registers_for_esp32_link(). After this returns,
 * the UART is live and the ISR is active — bytes arriving from the
 * ESP32 land in receive_buffer automatically.
 *
 * Order of operations matters: clock first, then pins, then a soft
 * reset, then config, then enable. Doing them out of order produces
 * silent failures (writes get lost, the SERCOM never starts shifting,
 * etc.) — see the comments inside each helper for the specifics.
 */
void uart_obc_initialize_sercom0_at_115200_baud(void)
{
    /* Zero the ring-buffer indices BEFORE enabling the ISR so the
     * first arriving byte lands in slot 0. */
    obc_uart_state.receive_write_index = 0u;
    obc_uart_state.receive_read_index = 0u;
    obc_uart_state.receive_overflow_has_occurred = 0u;
    obc_uart_state.transmit_write_index = 0u;
    obc_uart_state.transmit_read_index = 0u;

    enable_sercom0_bus_clock_for_register_access();
    connect_48mhz_gclk0_to_sercom0_functional_clock();
    configure_pa10_as_sercom0_uart_transmit_pin();
    configure_pa11_as_sercom0_uart_receive_pin();
    reset_sercom0_to_known_clean_state();
    configure_sercom0_as_bidirectional_uart_at_115200_baud();
    enable_sercom0_uart_and_receive_interrupt();
}

/*
 * Push N bytes into the transmit ring buffer and arm the DRE
 * interrupt. Returns immediately — no waiting for transmission to
 * finish. If the buffer is full, the excess bytes are silently
 * dropped (the for-loop's break exits early).
 */
void uart_obc_send_bytes(const uint8_t *bytes_to_send,
                         uint32_t number_of_bytes_to_send)
{
    if (bytes_to_send == (void *)0)
    {
        return;
    }

    /* Bounded loop — caller controls the bound. */
    for (uint32_t byte_index = 0u;
         byte_index < number_of_bytes_to_send;
         byte_index += 1u)
    {
        uint32_t next_write =
            (obc_uart_state.transmit_write_index + 1u)
            & OBC_UART_BUFFER_INDEX_MASK;

        /* Buffer full — drop the rest of this batch. The right
         * behaviour at this layer is to bail; the upper layer can
         * detect short writes by checking how many bytes it expected
         * to send vs. what arrived on the other end. */
        if (next_write == obc_uart_state.transmit_read_index)
        {
            break;
        }

        obc_uart_state.transmit_buffer[
            obc_uart_state.transmit_write_index] = bytes_to_send[byte_index];
        obc_uart_state.transmit_write_index = next_write;
    }

    /* Re-enable the DRE interrupt so the ISR starts draining the
     * buffer. INTENSET is write-1-to-set; bits not written stay as
     * they are. The DRE flag is already true (the transmitter has
     * been idle), so the interrupt fires immediately on the next
     * cycle and the first byte starts shifting out. */
    SERCOM0_REGS->USART_INT.SERCOM_INTENSET =
        SERCOM_USART_INT_INTENSET_DRE_Msk;
}

/*
 * How many bytes the main loop hasn't yet consumed. The unsigned
 * subtraction handles the wrap-around correctly because both indices
 * are masked back into [0, 255]. Used by the CHIPS reader's
 * "while there are bytes" loop.
 */
uint32_t uart_obc_number_of_bytes_available_in_receive_buffer(void)
{
    return (obc_uart_state.receive_write_index
            - obc_uart_state.receive_read_index)
           & OBC_UART_BUFFER_INDEX_MASK;
}

/*
 * Consume one byte from the receive buffer and return it. If the
 * buffer is empty, returns 0 — caller is expected to check
 * uart_obc_number_of_bytes_available_in_receive_buffer first.
 */
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

/* ────────────────────────────────────────────────────────────────────
 * Initialisation helpers — split into one function per concern so the
 * boot sequence above reads as English and the WHY of each register
 * write lives with the write itself.
 * ──────────────────────────────────────────────────────────────────── */

/*
 * Power up the SERCOM0 register block on the APB-C bus. By default
 * SERCOM0 is unpowered: writing any of its registers does nothing
 * until this bit is set. The PM peripheral (Power Manager) is the
 * SAMD21 block that gates clocks to other peripherals.
 *
 * Without this, the entire driver silently fails — the SERCOM0
 * registers all read as 0xFFFFFFFF and writes are dropped on the
 * floor.
 */
static void enable_sercom0_bus_clock_for_register_access(void)
{
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_SERCOM0_Msk;
}

/*
 * Even with the bus clock enabled (above), SERCOM0 needs a SECOND
 * clock — the "functional clock" — to actually shift bits and
 * generate baud timing. The bus clock lets the CPU access registers;
 * the functional clock lets the peripheral DO things.
 *
 * GCLK0 is the 48 MHz CPU clock (set up earlier by the clock
 * driver). We connect it to SERCOM0's functional-clock channel
 * (CORE) by writing GCLK_CLKCTRL with:
 *   CLKEN  = 1: enable this connection
 *   GEN    = 0 (GCLK0): source is generator 0
 *   ID     = SERCOM0_GCLK_ID_CORE: destination is SERCOM0's CORE
 *
 * The write crosses a clock domain boundary. Reading SERCOM0
 * registers before SYNCBUSY clears would return undefined values.
 * Bounded poll instead of an infinite loop so a stuck sync exits
 * cleanly.
 */
static void connect_48mhz_gclk0_to_sercom0_functional_clock(void)
{
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk |
        GCLK_CLKCTRL_GEN_GCLK0 |
        GCLK_CLKCTRL_ID(SERCOM0_GCLK_ID_CORE);

    for (uint32_t wait_count = 0u;
         wait_count < MAXIMUM_SYNC_WAIT_ITERATIONS;
         wait_count += 1u)
    {
        if ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) == 0u)
        {
            break;
        }
    }
}

/*
 * Route PA10 to SERCOM0's TX function. Two register writes:
 *
 *   PINCFG[10].PMUXEN = 1  → "use the PMUX setting, ignore the GPIO
 *                            DIR/OUT registers". Without this, the pin
 *                            stays a regular GPIO regardless of PMUX.
 *
 *   PMUX[5] half nibble    → "function C" (SERCOM0). PMUX packs two
 *                            pins per byte: PA10 is the LOW nibble of
 *                            byte 5 (since PA10 / 2 = 5 and PA10 is
 *                            even-numbered).
 *
 * The PMUX read-modify-write preserves whatever PA11 (the high nibble)
 * was set to, since this function is called BEFORE the PA11 helper
 * below — but writing the low nibble alone is required so a future
 * caller doesn't accidentally clobber PA11's mux.
 */
static void configure_pa10_as_sercom0_uart_transmit_pin(void)
{
    PORT_REGS->GROUP[0].PORT_PINCFG[TRANSMIT_PIN_NUMBER] =
        PORT_PINCFG_PMUXEN_Msk;

    uint8_t current_pmux_value = PORT_REGS->GROUP[0].PORT_PMUX[5];
    current_pmux_value &= 0xF0u;                              /* clear PA10 nibble (low) */
    current_pmux_value |= PORT_MUX_FUNCTION_C_FOR_SERCOM;     /* set PA10 nibble */
    PORT_REGS->GROUP[0].PORT_PMUX[5] = current_pmux_value;
}

/*
 * Route PA11 to SERCOM0's RX function. Three differences from the TX
 * pin setup above:
 *
 *   1. PINCFG[11].INEN = 1 (input enable). Required for any pin used
 *      as an INPUT — without it the input buffer is disconnected and
 *      the SERCOM cannot read the line voltage. This is the most
 *      common SAMD21 RX bug. TX pins do not need INEN because they
 *      are outputs.
 *
 *   2. PA11 is odd-numbered, so its mux value goes in the HIGH nibble
 *      of PMUX byte 5 (shift left by 4).
 *
 *   3. The read-modify-write preserves PA10's mux that the previous
 *      function just set.
 */
static void configure_pa11_as_sercom0_uart_receive_pin(void)
{
    PORT_REGS->GROUP[0].PORT_PINCFG[RECEIVE_PIN_NUMBER] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;

    uint8_t current_pmux_value = PORT_REGS->GROUP[0].PORT_PMUX[5];
    current_pmux_value &= 0x0Fu;                                            /* clear PA11 nibble (high) */
    current_pmux_value |= (uint8_t)(PORT_MUX_FUNCTION_C_FOR_SERCOM << 4u);  /* set PA11 nibble */
    PORT_REGS->GROUP[0].PORT_PMUX[5] = current_pmux_value;
}

/*
 * Software-reset SERCOM0 before configuring it. Always-clean state is
 * the safe starting point — SERCOM0 may have been left in any state
 * by a previous boot, by a watchdog reset, or by the debugger. SWRST
 * forces every SERCOM0 register back to its post-power-on default.
 *
 * The reset propagates across two clock domains. Writing further
 * configuration before SYNCBUSY.SWRST clears would silently discard
 * those writes — which would look like "the configuration didn't
 * work" with no error.
 */
static void reset_sercom0_to_known_clean_state(void)
{
    SERCOM0_REGS->USART_INT.SERCOM_CTRLA =
        SERCOM_USART_INT_CTRLA_SWRST_Msk;

    for (uint32_t wait_count = 0u;
         wait_count < MAXIMUM_SYNC_WAIT_ITERATIONS;
         wait_count += 1u)
    {
        if ((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY
             & SERCOM_USART_INT_SYNCBUSY_SWRST_Msk) == 0u)
        {
            break;
        }
    }
}

/*
 * Configure CTRLA + CTRLB + BAUD for 115200 8N1 with TX on PAD[2] and
 * RX on PAD[3]. The CTRLA write is a single big OR because every bit
 * needs to be set in one transaction (some of these fields cannot be
 * changed once ENABLE is set).
 *
 *   DORD = LSB                 → standard UART bit order (LSB first)
 *   MODE = USART_INT_CLK       → asynchronous UART using the chip's
 *                                internal clock for baud generation
 *                                (no external clock pin needed)
 *   TXPO = 1                   → transmit on PAD[2] (PA10)
 *   RXPO = 3                   → receive on PAD[3] (PA11)
 *
 * CTRLB needs a separate write because some of its bits (TXEN, RXEN)
 * cannot be set at the same time as CTRLA.ENABLE. Setting them
 * BEFORE ENABLE means they take effect when ENABLE is later set.
 *
 * BAUD goes last; see the top-of-file comment for the magic-number
 * derivation. Could be set before or after CTRLB; convention is "all
 * config writes after CTRLA, then ENABLE last".
 */
static void configure_sercom0_as_bidirectional_uart_at_115200_baud(void)
{
    SERCOM0_REGS->USART_INT.SERCOM_CTRLA =
        SERCOM_USART_INT_CTRLA_DORD_LSB |
        SERCOM_USART_INT_CTRLA_MODE_USART_INT_CLK |
        SERCOM_USART_INT_CTRLA_TXPO(1u) |
        SERCOM_USART_INT_CTRLA_RXPO(3u);

    SERCOM0_REGS->USART_INT.SERCOM_CTRLB =
        SERCOM_USART_INT_CTRLB_TXEN_Msk |
        SERCOM_USART_INT_CTRLB_RXEN_Msk;

    /* CTRLB crosses a clock domain — must wait before further writes. */
    for (uint32_t wait_count = 0u;
         wait_count < MAXIMUM_SYNC_WAIT_ITERATIONS;
         wait_count += 1u)
    {
        if ((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY
             & SERCOM_USART_INT_SYNCBUSY_CTRLB_Msk) == 0u)
        {
            break;
        }
    }

    SERCOM0_REGS->USART_INT.SERCOM_BAUD =
        BAUD_REGISTER_VALUE_FOR_115200;
}

/*
 * Final step: flip the ENABLE bit, wait for the sync, arm the RX
 * interrupt, and tell the NVIC to route SERCOM0_IRQn to the CPU.
 *
 *   - Without ENABLE, the SERCOM ignores its TX pin and shifts no
 *     bits even though every other register is correct.
 *   - Without INTENSET.RXC, the RXC flag would set on every
 *     received byte but the ISR would never fire and the buffer
 *     would never fill.
 *   - Without NVIC_EnableIRQ, the SERCOM0 interrupt request is
 *     blocked at the CPU even if the peripheral asserts it.
 *
 * DRE is NOT enabled here. send_bytes() arms it on demand and the
 * ISR auto-disarms it when the transmit buffer empties.
 */
static void enable_sercom0_uart_and_receive_interrupt(void)
{
    SERCOM0_REGS->USART_INT.SERCOM_CTRLA |=
        SERCOM_USART_INT_CTRLA_ENABLE_Msk;

    /* ENABLE crosses two clock domains and is slow. Writes to other
     * SERCOM registers before this clears are silently dropped. */
    for (uint32_t wait_count = 0u;
         wait_count < MAXIMUM_SYNC_WAIT_ITERATIONS;
         wait_count += 1u)
    {
        if ((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY
             & SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk) == 0u)
        {
            break;
        }
    }

    SERCOM0_REGS->USART_INT.SERCOM_INTENSET =
        SERCOM_USART_INT_INTENSET_RXC_Msk;

    NVIC_EnableIRQ(SERCOM0_IRQn);
}
