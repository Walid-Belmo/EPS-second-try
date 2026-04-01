/* =============================================================================
 * main.c
 * CHIPS protocol integration: receives commands from the OBC (or ESP32 test
 * harness) over UART, parses CHIPS frames, dispatches commands, and sends
 * responses. Runs boot self-tests at startup to verify CRC and frame codec.
 *
 * Category: APPLICATION
 * Peripheral: PORT group B (LED, button), EIC (EXTINT11), SERCOM0 (OBC UART),
 *             SERCOM5 (debug UART via DMA), ARM SysTick (millisecond timer)
 * Pins: PB10 (user LED), PB11 (user button), PA04/PA05 (OBC UART),
 *       PA22 (debug UART TX)
 * Clock: 48 MHz (DFLL48M open-loop)
 * =============================================================================
 */

#include "samd21g17d.h"
#include <stdint.h>
#include "clock_configure_48mhz_dfll_open_loop.h"
#include "debug_functions.h"
#include "assertion_handler.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "uart_obc_sercom0_pa04_pa05.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "chips_protocol_dispatch_commands_and_build_responses.h"

/* ── Constants ────────────────────────────────────────────────────────────── */

#define USER_LED_PIN_NUMBER    10u
#define USER_BUTTON_PIN_NUMBER 11u

/* LED heartbeat toggle interval in milliseconds. */
#define HEARTBEAT_TOGGLE_INTERVAL_MILLISECONDS  500u

/* OBC autonomy timeout: if no valid CHIPS frame from the OBC for this
 * many milliseconds, the EPS may assume autonomy and act alone.
 * Source: CHESS mission document, PDF page 125. */
#define OBC_AUTONOMY_TIMEOUT_MILLISECONDS       120000u

/* ── Module state ────────────────────────────────────────────────────────── */

static volatile uint32_t button_press_count = 0u;

/* CHIPS parser state — persists across main loop iterations. */
static chips_frame_parser_state_type chips_frame_parser_state;
static chips_parsed_frame_type most_recently_parsed_frame;

/* Timing for OBC timeout and heartbeat. */
static uint32_t millisecond_timestamp_of_last_valid_obc_message = 0u;
static uint8_t obc_timeout_has_been_reported_since_last_message = 0u;
static uint32_t millisecond_timestamp_of_last_heartbeat_toggle = 0u;
static uint32_t heartbeat_toggle_count = 0u;

/* ── ISR prototype (public — called by NVIC, needs prototype for -Wmissing) */

void EIC_Handler(void);

/* ── Private function prototypes ──────────────────────────────────────────── */

static void configure_pb10_as_gpio_output_for_user_led(void);
static void toggle_user_led(void);
static void configure_pb11_button_interrupt_on_extint11(void);
static void run_chips_protocol_boot_self_tests(void);
static void feed_all_available_uart_bytes_to_chips_parser(void);
static void toggle_heartbeat_led_if_interval_has_elapsed(void);
static void check_obc_communication_timeout(void);

/* ── Public entry point ───────────────────────────────────────────────────── */

int main(void)
{
    configure_cpu_clock_to_48mhz_using_dfll_open_loop();
    configure_pb10_as_gpio_output_for_user_led();
    DEBUG_LOG_INIT();
    millisecond_tick_timer_initialize_at_48mhz();
    uart_obc_initialize_sercom0_at_115200_baud();
    configure_pb11_button_interrupt_on_extint11();

    /* ── Boot info ─────────────────────────────────────────────────────── */

    DEBUG_LOG_TEXT("=== BOOT OK — CHIPS Phase 4 ===");
    DEBUG_LOG_UINT("CPU clock Hz", SystemCoreClock);
    DEBUG_LOG_UINT("reset cause", PM_REGS->PM_RCAUSE);
    DEBUG_LOG_TEXT("OBC UART initialized on SERCOM0 PA04/PA05");

    /* ── CHIPS self-tests ──────────────────────────────────────────────── */

    run_chips_protocol_boot_self_tests();

    /* ── Initialize CHIPS protocol layers ──────────────────────────────── */

    chips_parser_initialize_state_machine_to_idle(
        &chips_frame_parser_state);
    chips_command_dispatch_initialize();

    millisecond_timestamp_of_last_valid_obc_message =
        millisecond_tick_timer_get_milliseconds_since_boot();

    DEBUG_LOG_TEXT("CHIPS protocol ready — waiting for OBC commands");

    /* ── Main loop ─────────────────────────────────────────────────────── */

    while (1) /* @non-terminating@ */
    {
        feed_all_available_uart_bytes_to_chips_parser();
        toggle_heartbeat_led_if_interval_has_elapsed();
        check_obc_communication_timeout();
    }

    return 0;
}

/* ── ISR — button press on EXTINT11 ──────────────────────────────────────── */

void EIC_Handler(void)
{
    if ((EIC_REGS->EIC_INTFLAG & EIC_INTFLAG_EXTINT11_Msk) != 0u)
    {
        /* Clear the interrupt flag IMMEDIATELY. If not cleared before
         * returning, the interrupt fires again instantly. */
        EIC_REGS->EIC_INTFLAG = EIC_INTFLAG_EXTINT11_Msk;

        button_press_count += 1u;
    }
}

/* ── Private functions — boot self-tests ─────────────────────────────────── */

static void run_chips_protocol_boot_self_tests(void)
{
    DEBUG_LOG_TEXT("--- Running CHIPS self-tests ---");

    /* Test 1: CRC-16/KERMIT lookup table verification.
     * If this fails, the board halts with an assertion error. */
    chips_verify_crc16_kermit_lookup_table_is_correct();

    /* Test 2: Frame build-then-parse round-trip.
     * Builds a frame, feeds it to the parser, checks all fields match. */
    chips_verify_frame_build_and_parse_round_trip();

    DEBUG_LOG_TEXT("--- All CHIPS self-tests PASSED ---");
}

/* ── Private functions — CHIPS parser feeding ────────────────────────────── */

static void feed_all_available_uart_bytes_to_chips_parser(void)
{
    uint32_t number_of_bytes_waiting =
        uart_obc_number_of_bytes_available_in_receive_buffer();

    for (uint32_t i = 0u; i < number_of_bytes_waiting; i += 1u)
    {
        uint8_t one_received_byte =
            uart_obc_read_one_byte_from_receive_buffer();

        chips_parser_result_type parser_result =
            chips_parser_process_one_received_byte(
                &chips_frame_parser_state,
                one_received_byte,
                &most_recently_parsed_frame);

        if (parser_result == CHIPS_PARSER_RESULT_FRAME_READY)
        {
            millisecond_timestamp_of_last_valid_obc_message =
                millisecond_tick_timer_get_milliseconds_since_boot();
            obc_timeout_has_been_reported_since_last_message = 0u;

            DEBUG_LOG_TEXT("CHIPS frame received");
            DEBUG_LOG_UINT("  seq",
                (uint32_t)most_recently_parsed_frame.sequence_number);
            DEBUG_LOG_UINT("  cmd",
                (uint32_t)most_recently_parsed_frame.command_id);
            DEBUG_LOG_UINT("  rsp",
                (uint32_t)most_recently_parsed_frame.response_flag);
            DEBUG_LOG_UINT("  payload_len",
                (uint32_t)most_recently_parsed_frame.payload_length_in_bytes);

            chips_dispatch_received_command_and_send_response(
                &most_recently_parsed_frame);
        }
        else if (parser_result == CHIPS_PARSER_RESULT_ERROR_CRC_MISMATCH)
        {
            DEBUG_LOG_TEXT("CHIPS CRC error");
        }
        else if (parser_result == CHIPS_PARSER_RESULT_ERROR_FRAME_TOO_LONG)
        {
            DEBUG_LOG_TEXT("CHIPS frame too long");
        }
    }
}

/* ── Private functions — heartbeat LED ───────────────────────────────────── */

static void toggle_heartbeat_led_if_interval_has_elapsed(void)
{
    uint32_t now_milliseconds =
        millisecond_tick_timer_get_milliseconds_since_boot();

    uint32_t elapsed_since_last_toggle =
        now_milliseconds - millisecond_timestamp_of_last_heartbeat_toggle;

    if (elapsed_since_last_toggle >= HEARTBEAT_TOGGLE_INTERVAL_MILLISECONDS)
    {
        toggle_user_led();
        millisecond_timestamp_of_last_heartbeat_toggle = now_milliseconds;
        heartbeat_toggle_count += 1u;

        /* Log heartbeat every 10 toggles (every 5 seconds) to avoid
         * flooding the debug UART. */
        if ((heartbeat_toggle_count % 10u) == 0u)
        {
            DEBUG_LOG_UINT("heartbeat", heartbeat_toggle_count);
        }
    }
}

/* ── Private functions — OBC timeout ─────────────────────────────────────── */

static void check_obc_communication_timeout(void)
{
    /* Only report the timeout once per silence period. After reporting,
     * the flag stays set until a new valid OBC message arrives. */
    if (obc_timeout_has_been_reported_since_last_message != 0u)
    {
        return;
    }

    uint32_t now_milliseconds =
        millisecond_tick_timer_get_milliseconds_since_boot();

    uint32_t milliseconds_since_last_obc_message =
        now_milliseconds - millisecond_timestamp_of_last_valid_obc_message;

    if (milliseconds_since_last_obc_message >= OBC_AUTONOMY_TIMEOUT_MILLISECONDS)
    {
        DEBUG_LOG_TEXT("!!! OBC TIMEOUT — 120s without communication !!!");
        DEBUG_LOG_TEXT("Entering autonomous mode");
        obc_timeout_has_been_reported_since_last_message = 1u;

        /* Phase 4: log only. Phase 8 will implement actual autonomous
         * behavior (continue battery protection, maintain power bus
         * regulation, etc.). */
    }
}

/* ── Private functions — LED ──────────────────────────────────────────────── */

static void configure_pb10_as_gpio_output_for_user_led(void)
{
    /* PB10 drives the on-board user LED. Setting DIRSET makes it an output.
     * OUTSET sets the pin high, which turns the LED OFF (active low). */
    PORT_REGS->GROUP[1].PORT_DIRSET = (1u << USER_LED_PIN_NUMBER);
    PORT_REGS->GROUP[1].PORT_OUTSET = (1u << USER_LED_PIN_NUMBER);
}

static void toggle_user_led(void)
{
    /* OUTTGL atomically toggles the pin — no read-modify-write needed. */
    PORT_REGS->GROUP[1].PORT_OUTTGL = (1u << USER_LED_PIN_NUMBER);
}

/* ── Private functions — button interrupt ─────────────────────────────────── */

static void configure_pb11_button_interrupt_on_extint11(void)
{
    /* 1. Enable EIC bus clock on APB A bus. Without this, writes to
     *    EIC registers are silently ignored. */
    PM_REGS->PM_APBAMASK |= PM_APBAMASK_EIC_Msk;

    /* 2. Connect GCLK0 (48 MHz) to EIC functional clock.
     *    The EIC needs a clock source for edge detection and the
     *    digital glitch filter. Without it, edge sensing does not work. */
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk     |
        GCLK_CLKCTRL_GEN_GCLK0     |
        GCLK_CLKCTRL_ID(EIC_GCLK_ID);

    while ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for GCLK synchronization — writing EIC registers before
         * this clears produces undefined behavior. */
    }

    /* 3. Configure PB11 for EIC EXTINT11 (mux A).
     *    PB11 is an odd pin, so we write the PMUXO (upper nibble) at
     *    PMUX index 11/2 = 5. Mux A = 0x00.
     *    INEN enables the input buffer so the EIC can sample the pin.
     *    PULLEN enables the internal pull-up (direction set via OUTSET). */
    PORT_REGS->GROUP[1].PORT_PINCFG[USER_BUTTON_PIN_NUMBER] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk | PORT_PINCFG_PULLEN_Msk;

    /* Set pull direction to UP (button pulls LOW when pressed). */
    PORT_REGS->GROUP[1].PORT_OUTSET = (1u << USER_BUTTON_PIN_NUMBER);

    PORT_REGS->GROUP[1].PORT_PMUX[5] =
        (PORT_REGS->GROUP[1].PORT_PMUX[5] & 0x0Fu)
        | (PORT_PMUX_PMUXO_A_Val << 4u);

    /* 4. Configure EXTINT11 in CONFIG[1] register.
     *    EXTINT 8-15 use CONFIG[1]. EXTINT11 is at index 3 within CONFIG[1]
     *    (11 - 8 = 3). Each EXTINT uses 4 bits: 3 for SENSE + 1 for FILTEN.
     *    SENSE3 = FALL (0x2): detect falling edge (button press = HIGH->LOW).
     *    FILTEN3 = 1: enable hardware glitch filter for debounce. */
    EIC_REGS->EIC_CONFIG[1] =
        (EIC_REGS->EIC_CONFIG[1]
         & ~(EIC_CONFIG_SENSE3_Msk | EIC_CONFIG_FILTEN3_Msk))
        | EIC_CONFIG_SENSE3_FALL
        | EIC_CONFIG_FILTEN3_Msk;

    /* 5. Enable the EXTINT11 interrupt in the EIC. */
    EIC_REGS->EIC_INTENSET = EIC_INTENSET_EXTINT11_Msk;

    /* 6. Enable the EIC peripheral. Configuration registers must be
     *    written BEFORE enabling (they are locked while enabled). */
    EIC_REGS->EIC_CTRL = EIC_CTRL_ENABLE_Msk;
    while ((EIC_REGS->EIC_STATUS & EIC_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for EIC enable synchronization — using the EIC before
         * this clears means edge detection does not function. */
    }

    /* 7. Enable the EIC interrupt in the NVIC (ARM interrupt controller).
     *    Without this, the EIC hardware can set flags but the CPU never
     *    jumps to EIC_Handler. */
    NVIC_EnableIRQ(EIC_IRQn);
}
