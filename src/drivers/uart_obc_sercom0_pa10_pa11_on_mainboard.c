/* =============================================================================
 * uart_obc_sercom0_pa10_pa11_on_mainboard.c
 * Non-blocking bidirectional UART driver for the real EPS board OBC link.
 *
 * BUILD TARGET: mainboard only
 *
 * Uses the PCU testing board V4.1 UART nets:
 *   PA10 = UART_TX = SERCOM0 PAD[2], mux C, TXPO=1
 *   PA11 = UART_RX = SERCOM0 PAD[3], mux C, RXPO=3
 * =============================================================================
 */

#include <stdint.h>
#include "samd21j17d.h"
#include "uart_obc_sercom0_pa10_pa11_on_mainboard.h"

#define OBC_UART_BUFFER_SIZE_IN_BYTES          256u
#define OBC_UART_BUFFER_INDEX_MASK             (OBC_UART_BUFFER_SIZE_IN_BYTES - 1u)

#define TRANSMIT_PIN_NUMBER                    10u  /* PA10 */
#define RECEIVE_PIN_NUMBER                     11u  /* PA11 */
#define PORT_MUX_FUNCTION_C_FOR_SERCOM         2u
#define BAUD_REGISTER_VALUE_FOR_115200         63019u
#define MAXIMUM_SYNC_WAIT_ITERATIONS           100000u

static struct uart_obc_module_state {
    uint8_t           receive_buffer[OBC_UART_BUFFER_SIZE_IN_BYTES];
    volatile uint32_t receive_write_index;
    volatile uint32_t receive_read_index;
    volatile uint8_t  receive_overflow_has_occurred;

    uint8_t           transmit_buffer[OBC_UART_BUFFER_SIZE_IN_BYTES];
    volatile uint32_t transmit_write_index;
    volatile uint32_t transmit_read_index;
} obc_uart_state;

static void enable_sercom0_bus_clock_for_register_access(void);
static void connect_48mhz_gclk0_to_sercom0_functional_clock(void);
static void configure_pa10_as_sercom0_uart_transmit_pin(void);
static void configure_pa11_as_sercom0_uart_receive_pin(void);
static void reset_sercom0_to_known_clean_state(void);
static void configure_sercom0_as_bidirectional_uart_at_115200_baud(void);
static void enable_sercom0_uart_and_receive_interrupt(void);

void SERCOM0_Handler(void);

void SERCOM0_Handler(void)
{
    uint8_t interrupt_flags = SERCOM0_REGS->USART_INT.SERCOM_INTFLAG;

    if ((interrupt_flags & SERCOM_USART_INT_INTFLAG_RXC_Msk) != 0u)
    {
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

    if ((interrupt_flags & SERCOM_USART_INT_INTFLAG_DRE_Msk) != 0u)
    {
        if (obc_uart_state.transmit_read_index !=
            obc_uart_state.transmit_write_index)
        {
            SERCOM0_REGS->USART_INT.SERCOM_DATA =
                (uint16_t)obc_uart_state.transmit_buffer[
                    obc_uart_state.transmit_read_index];

            obc_uart_state.transmit_read_index =
                (obc_uart_state.transmit_read_index + 1u)
                & OBC_UART_BUFFER_INDEX_MASK;
        }
        else
        {
            SERCOM0_REGS->USART_INT.SERCOM_INTENCLR =
                SERCOM_USART_INT_INTENCLR_DRE_Msk;
        }
    }
}

void uart_obc_initialize_sercom0_at_115200_baud(void)
{
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

void uart_obc_send_bytes(const uint8_t *bytes_to_send,
                         uint32_t number_of_bytes_to_send)
{
    if (bytes_to_send == (void *)0)
    {
        return;
    }

    for (uint32_t byte_index = 0u;
         byte_index < number_of_bytes_to_send;
         byte_index += 1u)
    {
        uint32_t next_write =
            (obc_uart_state.transmit_write_index + 1u)
            & OBC_UART_BUFFER_INDEX_MASK;

        if (next_write == obc_uart_state.transmit_read_index)
        {
            break;
        }

        obc_uart_state.transmit_buffer[
            obc_uart_state.transmit_write_index] = bytes_to_send[byte_index];
        obc_uart_state.transmit_write_index = next_write;
    }

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

static void enable_sercom0_bus_clock_for_register_access(void)
{
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_SERCOM0_Msk;
}

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

static void configure_pa10_as_sercom0_uart_transmit_pin(void)
{
    PORT_REGS->GROUP[0].PORT_PINCFG[TRANSMIT_PIN_NUMBER] =
        PORT_PINCFG_PMUXEN_Msk;

    uint8_t current_pmux_value = PORT_REGS->GROUP[0].PORT_PMUX[5];
    current_pmux_value &= 0xF0u;
    current_pmux_value |= PORT_MUX_FUNCTION_C_FOR_SERCOM;
    PORT_REGS->GROUP[0].PORT_PMUX[5] = current_pmux_value;
}

static void configure_pa11_as_sercom0_uart_receive_pin(void)
{
    PORT_REGS->GROUP[0].PORT_PINCFG[RECEIVE_PIN_NUMBER] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;

    uint8_t current_pmux_value = PORT_REGS->GROUP[0].PORT_PMUX[5];
    current_pmux_value &= 0x0Fu;
    current_pmux_value |= (uint8_t)(PORT_MUX_FUNCTION_C_FOR_SERCOM << 4u);
    PORT_REGS->GROUP[0].PORT_PMUX[5] = current_pmux_value;
}

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

static void enable_sercom0_uart_and_receive_interrupt(void)
{
    SERCOM0_REGS->USART_INT.SERCOM_CTRLA |=
        SERCOM_USART_INT_CTRLA_ENABLE_Msk;

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
