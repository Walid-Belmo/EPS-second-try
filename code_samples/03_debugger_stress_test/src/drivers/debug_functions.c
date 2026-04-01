/* =============================================================================
 * debug_functions.c
 * Non-blocking DMA-based debug logging system.
 * Transmits log messages from a circular RAM buffer to SERCOM5 UART (PA22)
 * using DMAC channel 0. The CPU writes bytes into the buffer (~1 us cost)
 * and the DMA hardware drains them in the background.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: SERCOM5 (UART TX), DMAC channel 0, PORT group A
 * Pins: PA22 (TX, mux D → SERCOM5 PAD[0])
 * Clock: GCLK0 (48 MHz) connected to SERCOM5 functional clock
 * Interrupt: DMAC_Handler — fires on DMA transfer complete
 *
 * Sources:
 *   - aykevl.nl DMA tutorial: https://aykevl.nl/2019/09/samd21-dma/
 *   - Microchip AT07683: SAM D DMAC Driver Application Note
 *   - SAMD21 datasheet, Section 21 (DMAC), Section 26 (SERCOM)
 * =============================================================================
 */

#ifdef DEBUG_LOGGING_ENABLED

#include <stdint.h>
#include <stdbool.h>
#include "samd21g17d.h"
#include "debug_functions.h"

/* ── Constants ────────────────────────────────────────────────────────────── */

/* Buffer size must be a power of 2 for bitmask modulo. 512 bytes supports
 * a 100 Hz control loop logging ~30 bytes per iteration without overflow. */
#define LOG_CIRCULAR_BUFFER_SIZE_IN_BYTES       512u
#define LOG_CIRCULAR_BUFFER_INDEX_MASK          (LOG_CIRCULAR_BUFFER_SIZE_IN_BYTES - 1u)

#define UART_TRANSMIT_PIN_NUMBER                22u  /* PA22 */
#define PORT_MUX_FUNCTION_D_FOR_SERCOM_ALT      3u   /* mux D = 0x3 */

/* BAUD = 65536 * (1 - 16 * (115200 / 48000000)) = 63019.
 * Timing error < 0.01%. UART tolerates up to ~3%. */
#define UART_BAUD_REGISTER_VALUE_FOR_115200     63019u

/* ── Module state ─────────────────────────────────────────────────────────── */

static struct debug_log_module_state {
    uint8_t           circular_buffer[LOG_CIRCULAR_BUFFER_SIZE_IN_BYTES];
    volatile uint32_t write_position;
    volatile uint32_t read_position;
    volatile uint8_t  dma_transfer_is_active;
} log_state;

/* DMA descriptor arrays. The DMAC reads the base descriptor to know what
 * to transfer. It writes its progress into the writeback descriptor.
 * Both must be in SRAM (not stack), at 8-byte aligned addresses. */
__attribute__((aligned(8)))
static dmac_descriptor_registers_t dma_base_descriptors[DMAC_CH_NUM];

__attribute__((aligned(8)))
static dmac_descriptor_registers_t dma_writeback_descriptors[DMAC_CH_NUM];

/* ── Private function prototypes ──────────────────────────────────────────── */

static void enable_sercom5_bus_clock_for_register_access(void);
static void connect_48mhz_gclk0_to_sercom5_functional_clock(void);
static void configure_pa22_as_sercom5_uart_transmit_pin(void);
static void reset_sercom5_to_known_clean_state(void);
static void configure_sercom5_as_uart_transmitter_at_115200_baud(void);
static void enable_sercom5_uart_peripheral(void);
static void configure_dmac_channel_0_for_sercom5_transmit(void);
static void start_dma_transfer_from_buffer_to_sercom5(uint32_t number_of_bytes_to_send);
static void write_byte_into_circular_buffer(uint8_t byte_to_write);
static void write_string_into_circular_buffer(const char *string_to_write);

/* ── ISR ──────────────────────────────────────────────────────────────────── */

void DMAC_Handler(void)
{
    /* Select channel 0 so subsequent register reads/writes apply to it. */
    DMAC_REGS->DMAC_CHID = 0u;

    /* Clear the Transfer Complete interrupt flag IMMEDIATELY.
     * If not cleared before returning, the interrupt fires again
     * instantly, creating an infinite loop that locks the CPU. */
    DMAC_REGS->DMAC_CHINTFLAG = DMAC_CHINTFLAG_TCMPL_Msk;

    /* Calculate how many bytes are still pending in the buffer. */
    uint32_t bytes_pending =
        (log_state.write_position - log_state.read_position)
        & LOG_CIRCULAR_BUFFER_INDEX_MASK;

    if (bytes_pending > 0u)
    {
        /* More data accumulated while the last transfer was running.
         * Start a new DMA transfer for the pending bytes. */
        start_dma_transfer_from_buffer_to_sercom5(bytes_pending);
    }
    else
    {
        /* Buffer is empty. Go idle. The next LOG call will restart DMA. */
        log_state.dma_transfer_is_active = 0u;
    }
}

/* ── Public functions ─────────────────────────────────────────────────────── */

void debug_log_initialize_dma_uart_on_sercom5_pa22(void)
{
    /* Initialize module state to known zeros. */
    log_state.write_position = 0u;
    log_state.read_position = 0u;
    log_state.dma_transfer_is_active = 0u;

    /* Configure SERCOM5 as UART transmitter. */
    enable_sercom5_bus_clock_for_register_access();
    connect_48mhz_gclk0_to_sercom5_functional_clock();
    configure_pa22_as_sercom5_uart_transmit_pin();
    reset_sercom5_to_known_clean_state();
    configure_sercom5_as_uart_transmitter_at_115200_baud();
    enable_sercom5_uart_peripheral();

    /* Configure DMAC channel 0 for SERCOM5 TX. */
    configure_dmac_channel_0_for_sercom5_transmit();
}

void debug_log_write_text_line(const char *message)
{
    write_string_into_circular_buffer(message);
    write_byte_into_circular_buffer('\r');
    write_byte_into_circular_buffer('\n');

    if (log_state.dma_transfer_is_active == 0u)
    {
        uint32_t bytes_pending =
            (log_state.write_position - log_state.read_position)
            & LOG_CIRCULAR_BUFFER_INDEX_MASK;

        if (bytes_pending > 0u)
        {
            start_dma_transfer_from_buffer_to_sercom5(bytes_pending);
        }
    }
}

void debug_log_write_labeled_unsigned_integer(const char *label, uint32_t value)
{
    /* Output format: "label: value\r\n" */
    write_string_into_circular_buffer(label);
    write_string_into_circular_buffer(": ");

    /* Convert the integer to decimal ASCII digits.
     * Maximum uint32_t is 4294967295 (10 digits). */
    char digit_buffer[10];
    int32_t digit_count = 0;

    if (value == 0u)
    {
        digit_buffer[0] = '0';
        digit_count = 1;
    }
    else
    {
        /* Extract digits in reverse order (least significant first). */
        uint32_t remaining_value = value;
        while (remaining_value > 0u)
        {
            digit_buffer[digit_count] = (char)('0' + (remaining_value % 10u));
            remaining_value /= 10u;
            digit_count += 1;
        }
    }

    /* Write digits in correct order (most significant first). */
    for (int32_t i = digit_count - 1; i >= 0; i -= 1)
    {
        write_byte_into_circular_buffer((uint8_t)digit_buffer[i]);
    }

    write_byte_into_circular_buffer('\r');
    write_byte_into_circular_buffer('\n');

    if (log_state.dma_transfer_is_active == 0u)
    {
        uint32_t bytes_pending =
            (log_state.write_position - log_state.read_position)
            & LOG_CIRCULAR_BUFFER_INDEX_MASK;

        if (bytes_pending > 0u)
        {
            start_dma_transfer_from_buffer_to_sercom5(bytes_pending);
        }
    }
}

void debug_log_write_labeled_signed_integer(const char *label, int32_t value)
{
    write_string_into_circular_buffer(label);
    write_string_into_circular_buffer(": ");

    if (value < 0)
    {
        write_byte_into_circular_buffer('-');
        /* Negate carefully: -(-2147483648) overflows int32_t.
         * Cast to uint32_t first to handle the full range. */
        uint32_t absolute_value = (uint32_t)(-(value + 1)) + 1u;

        char digit_buffer[10];
        int32_t digit_count = 0;
        while (absolute_value > 0u)
        {
            digit_buffer[digit_count] = (char)('0' + (absolute_value % 10u));
            absolute_value /= 10u;
            digit_count += 1;
        }
        for (int32_t i = digit_count - 1; i >= 0; i -= 1)
        {
            write_byte_into_circular_buffer((uint8_t)digit_buffer[i]);
        }
    }
    else
    {
        char digit_buffer[10];
        int32_t digit_count = 0;
        if (value == 0)
        {
            digit_buffer[0] = '0';
            digit_count = 1;
        }
        else
        {
            uint32_t remaining = (uint32_t)value;
            while (remaining > 0u)
            {
                digit_buffer[digit_count] = (char)('0' + (remaining % 10u));
                remaining /= 10u;
                digit_count += 1;
            }
        }
        for (int32_t i = digit_count - 1; i >= 0; i -= 1)
        {
            write_byte_into_circular_buffer((uint8_t)digit_buffer[i]);
        }
    }

    write_byte_into_circular_buffer('\r');
    write_byte_into_circular_buffer('\n');

    if (log_state.dma_transfer_is_active == 0u)
    {
        uint32_t bytes_pending =
            (log_state.write_position - log_state.read_position)
            & LOG_CIRCULAR_BUFFER_INDEX_MASK;

        if (bytes_pending > 0u)
        {
            start_dma_transfer_from_buffer_to_sercom5(bytes_pending);
        }
    }
}

/* ── Private functions — UART setup ───────────────────────────────────────── */

static void enable_sercom5_bus_clock_for_register_access(void)
{
    /* SERCOM5 is on the APB C bus. By default its clock gate is closed,
     * meaning the CPU cannot read or write SERCOM5 registers at all.
     * This bit opens the gate. */
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_SERCOM5_Msk;
}

static void connect_48mhz_gclk0_to_sercom5_functional_clock(void)
{
    /* The bus clock (above) lets the CPU access SERCOM5 registers.
     * The functional clock is what the SERCOM actually uses to generate
     * baud timing and shift bits. Without it, the UART does not operate
     * even though registers appear writable.
     *
     * SERCOM5_GCLK_ID_CORE = 25 is the clock channel ID for SERCOM5. */
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk                                 |
        GCLK_CLKCTRL_GEN_GCLK0                                 |
        GCLK_CLKCTRL_ID(SERCOM5_GCLK_ID_CORE);

    /* This write crosses a clock domain boundary. Any SERCOM5 register
     * write before SYNCBUSY clears is silently discarded. */
    while ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for GCLK synchronization */
    }
}

static void configure_pa22_as_sercom5_uart_transmit_pin(void)
{
    /* PA22 defaults to GPIO after reset. We need to route it to SERCOM5.
     *
     * On the DM320119 Curiosity Nano, PA22 connects to the nEDBG UART RX
     * input, which forwards received bytes to the USB CDC virtual COM port.
     *
     * Step 1: Enable the peripheral mux on PA22 (PMUXEN bit in PINCFG).
     *         Without this, the pin stays as GPIO regardless of PMUX setting.
     *
     * Step 2: Set the mux function to D (SERCOM-ALT).
     *         PA22 can reach two different SERCOMs depending on the mux:
     *           Mux C = SERCOM (primary)  → SERCOM3 PAD[0]
     *           Mux D = SERCOM-ALT        → SERCOM5 PAD[0] (TX)
     *         Using the wrong mux routes PA22 to SERCOM3 instead of SERCOM5.
     *
     * PORT_PMUX register: PA22 is pin 22. PMUX index = 22/2 = 11.
     * PA22 is an even pin, so we write to the PMUXE (even) nibble (bits 3:0). */
    PORT_REGS->GROUP[0].PORT_PINCFG[22] |= PORT_PINCFG_PMUXEN_Msk;

    uint8_t current_pmux_value = PORT_REGS->GROUP[0].PORT_PMUX[11];
    /* Clear the even nibble (bits 3:0), preserve the odd nibble (bits 7:4). */
    current_pmux_value &= 0xF0u;
    /* Set even nibble to mux D (0x03). */
    current_pmux_value |= PORT_MUX_FUNCTION_D_FOR_SERCOM_ALT;
    PORT_REGS->GROUP[0].PORT_PMUX[11] = current_pmux_value;
}

static void reset_sercom5_to_known_clean_state(void)
{
    /* Software reset clears all SERCOM5 registers to their power-on defaults.
     * This ensures no leftover state from a previous boot, watchdog reset,
     * or debugger session affects the configuration. */
    SERCOM5_REGS->USART_INT.SERCOM_CTRLA = SERCOM_USART_INT_CTRLA_SWRST_Msk;

    /* The reset propagates across clock domains. Any write to SERCOM5
     * before this clears is silently discarded. */
    while ((SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY
            & SERCOM_USART_INT_SYNCBUSY_SWRST_Msk) != 0u)
    {
        /* Wait for software reset to complete */
    }
}

static void configure_sercom5_as_uart_transmitter_at_115200_baud(void)
{
    /* CTRLA configuration — sets the operating mode and pin routing.
     * SERCOM must be disabled (ENABLE=0) while writing CTRLA. */
    SERCOM5_REGS->USART_INT.SERCOM_CTRLA =
        SERCOM_USART_INT_CTRLA_DORD_LSB         |   /* LSB first (standard UART) */
        SERCOM_USART_INT_CTRLA_MODE_USART_INT_CLK |  /* internal clock mode */
        SERCOM_USART_INT_CTRLA_TXPO(0u);             /* TXPO=0: TX on PAD[0] = PA22 */

    /* CTRLB configuration — enables the transmitter. */
    SERCOM5_REGS->USART_INT.SERCOM_CTRLB =
        SERCOM_USART_INT_CTRLB_TXEN_Msk;            /* enable TX, RX stays disabled */

    /* CTRLB crosses a clock domain. Must wait before proceeding. */
    while ((SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY
            & SERCOM_USART_INT_SYNCBUSY_CTRLB_Msk) != 0u)
    {
        /* Wait for CTRLB synchronization */
    }

    /* BAUD register — sets the bit rate.
     * BAUD = 65536 * (1 - 16 * (115200 / 48000000)) = 63019.
     * Actual timing error: < 0.01%. UART tolerates up to ~3%. */
    SERCOM5_REGS->USART_INT.SERCOM_BAUD = UART_BAUD_REGISTER_VALUE_FOR_115200;
}

static void enable_sercom5_uart_peripheral(void)
{
    /* The UART does not transmit anything until ENABLE is set, even though
     * all configuration registers are written correctly. */
    SERCOM5_REGS->USART_INT.SERCOM_CTRLA |= SERCOM_USART_INT_CTRLA_ENABLE_Msk;

    /* ENABLE propagates across clock domains. Using the UART before this
     * clears results in it appearing enabled but not functioning. */
    while ((SERCOM5_REGS->USART_INT.SERCOM_SYNCBUSY
            & SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk) != 0u)
    {
        /* Wait for ENABLE synchronization */
    }
}

/* ── Private functions — DMA setup ────────────────────────────────────────── */

static void configure_dmac_channel_0_for_sercom5_transmit(void)
{
    /* Enable DMAC clocks. The DMAC needs both the AHB clock (for the
     * DMA engine itself) and the APB B clock (for CPU register access). */
    PM_REGS->PM_AHBMASK  |= PM_AHBMASK_DMAC_Msk;
    PM_REGS->PM_APBBMASK |= PM_APBBMASK_DMAC_Msk;

    /* Disable the DMAC before configuring. BASEADDR and WRBADDR can only
     * be written while the DMAC is disabled. */
    DMAC_REGS->DMAC_CTRL &= ~DMAC_CTRL_DMAENABLE_Msk;

    /* Tell the DMAC where the descriptor arrays live in SRAM. */
    DMAC_REGS->DMAC_BASEADDR = (uint32_t)dma_base_descriptors;
    DMAC_REGS->DMAC_WRBADDR  = (uint32_t)dma_writeback_descriptors;

    /* Enable the DMAC with all priority levels active. */
    DMAC_REGS->DMAC_CTRL =
        DMAC_CTRL_DMAENABLE_Msk |
        DMAC_CTRL_LVLEN0_Msk   |
        DMAC_CTRL_LVLEN1_Msk   |
        DMAC_CTRL_LVLEN2_Msk   |
        DMAC_CTRL_LVLEN3_Msk;

    /* Select channel 0 for configuration. The CHID register determines
     * which channel subsequent CHCTRLA/CHCTRLB/CHINTENSET writes apply to. */
    DMAC_REGS->DMAC_CHID = 0u;

    /* Configure channel 0 trigger:
     * - TRIGSRC = SERCOM5_DMAC_ID_TX (12): SERCOM5 TX empty triggers DMA
     * - TRIGACT = BEAT: one byte transferred per trigger event */
    DMAC_REGS->DMAC_CHCTRLB =
        DMAC_CHCTRLB_TRIGSRC(SERCOM5_DMAC_ID_TX) |
        DMAC_CHCTRLB_TRIGACT_BEAT;

    /* Enable the Transfer Complete interrupt for channel 0. When all bytes
     * in the current descriptor are sent, this interrupt fires so we can
     * either start a new transfer (if more data arrived) or go idle. */
    DMAC_REGS->DMAC_CHINTENSET = DMAC_CHINTENSET_TCMPL_Msk;

    /* Enable the DMAC interrupt in the NVIC (ARM interrupt controller). */
    NVIC_EnableIRQ(DMAC_IRQn);
}

/* ── Private functions — circular buffer and DMA transfer ─────────────────── */

static void write_byte_into_circular_buffer(uint8_t byte_to_write)
{
    uint32_t next_write_position =
        (log_state.write_position + 1u) & LOG_CIRCULAR_BUFFER_INDEX_MASK;

    /* If the buffer is full, silently drop the byte rather than blocking
     * the CPU or corrupting memory. Truncated log lines are preferable
     * to a stalled control loop. */
    if (next_write_position == log_state.read_position)
    {
        return;
    }

    log_state.circular_buffer[log_state.write_position] = byte_to_write;
    log_state.write_position = next_write_position;
}

static void write_string_into_circular_buffer(const char *string_to_write)
{
    while (*string_to_write != '\0')
    {
        write_byte_into_circular_buffer((uint8_t)*string_to_write);
        string_to_write++;
    }
}

static void start_dma_transfer_from_buffer_to_sercom5(
    uint32_t number_of_bytes_to_send)
{
    log_state.dma_transfer_is_active = 1u;

    uint32_t read_index =
        log_state.read_position & LOG_CIRCULAR_BUFFER_INDEX_MASK;

    /* Handle wraparound: if the pending data crosses the end of the
     * circular buffer, only send up to the end in this transfer.
     * The ISR will start a second transfer for the wrapped portion. */
    uint32_t bytes_until_buffer_end =
        LOG_CIRCULAR_BUFFER_SIZE_IN_BYTES - read_index;

    if (number_of_bytes_to_send > bytes_until_buffer_end)
    {
        number_of_bytes_to_send = bytes_until_buffer_end;
    }

    /* Advance read_position now. The ISR will see the updated value
     * when the transfer completes. */
    log_state.read_position =
        (log_state.read_position + number_of_bytes_to_send)
        & LOG_CIRCULAR_BUFFER_INDEX_MASK;

    /* Fill the DMA descriptor for channel 0.
     *
     * CRITICAL: SRCADDR must point to the END of the source data,
     * not the start. This is a SAMD21-specific convention. When SRCINC
     * is enabled, the DMAC decrements SRCADDR before each beat, so it
     * needs to start at the end and work backward.
     * Source: https://aykevl.nl/2019/09/samd21-dma/ */
    dma_base_descriptors[0].DMAC_BTCNT =
        (uint16_t)number_of_bytes_to_send;

    dma_base_descriptors[0].DMAC_SRCADDR =
        (uint32_t)&log_state.circular_buffer[read_index]
        + number_of_bytes_to_send;

    /* DSTADDR is the SERCOM5 DATA register. It does NOT increment —
     * every byte goes to the same hardware register address. */
    dma_base_descriptors[0].DMAC_DSTADDR =
        (uint32_t)&SERCOM5_REGS->USART_INT.SERCOM_DATA;

    /* No linked descriptor — this is a single-shot transfer. */
    dma_base_descriptors[0].DMAC_DESCADDR = 0u;

    /* Block Transfer Control:
     * - VALID: this descriptor is valid and should be processed
     * - BEATSIZE_BYTE: transfer one byte per beat
     * - SRCINC: increment source address after each byte
     * - DSTINC is NOT set: destination (SERCOM DATA register) is fixed */
    dma_base_descriptors[0].DMAC_BTCTRL =
        DMAC_BTCTRL_VALID_Msk       |
        DMAC_BTCTRL_BEATSIZE_BYTE   |
        DMAC_BTCTRL_SRCINC_Msk;

    /* Start the transfer by enabling channel 0. */
    DMAC_REGS->DMAC_CHID = 0u;
    DMAC_REGS->DMAC_CHCTRLA = DMAC_CHCTRLA_ENABLE_Msk;
}

#endif /* DEBUG_LOGGING_ENABLED */
