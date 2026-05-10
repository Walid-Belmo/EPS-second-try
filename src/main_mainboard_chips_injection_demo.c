/* =============================================================================
 * main_mainboard_chips_injection_demo.c
 * Real-board semester demo firmware entry point.
 *
 * BUILD TARGET: mainboard only
 *
 * This application exposes the EPS demo through the real board's J3 UART:
 *   board UART_TX = SAMD21 PA10
 *   board UART_RX = SAMD21 PA11
 *
 * It deliberately does not drive PWM_H/PWM_L, POWER_SW, or HEATER_SW yet.
 * The first real-board demo step is communication visibility and injected
 * values over CHIPS.
 * =============================================================================
 */

#include <stdint.h>

#include "samd21j17d.h"
#include "clock_configure_48mhz_dfll_open_loop.h"
#include "led_status_pb22_active_high_on_mainboard.h"
#include "mainboard_adc_reader.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "uart_obc.h"
#include "chips_protocol_encode_decode_frames_with_crc16_kermit.h"
#include "eps_demo_chips_command_dispatch.h"

#define HEARTBEAT_LED_PERIOD_MS        500u

static chips_frame_parser_state_type chips_parser_state;
static chips_parsed_frame_type most_recently_parsed_frame;

static void process_any_obc_uart_bytes_as_chips_frames(void);
static void handle_one_chips_parser_result(
    chips_parser_result_type parser_result);

int main(void)
{
    configure_cpu_clock_to_48mhz_using_dfll_open_loop();
    configure_pb22_as_gpio_output_for_status_led_on_mainboard();
    mainboard_adc_reader_initialize();
    millisecond_tick_timer_initialize_at_48mhz();
    uart_obc_initialize_sercom0_at_115200_baud();

    chips_verify_crc16_kermit_lookup_table_is_correct();
    chips_verify_frame_build_and_parse_round_trip();
    chips_parser_initialize_state_machine_to_idle(&chips_parser_state);
    eps_demo_chips_command_dispatch_initialize();

    uint32_t last_heartbeat_timestamp_ms =
        millisecond_tick_timer_get_milliseconds_since_boot();

    while (1)
    {
        process_any_obc_uart_bytes_as_chips_frames();
        eps_demo_chips_run_control_loop_if_due();
        eps_demo_chips_send_periodic_telemetry_if_due();

        uint32_t now_ms =
            millisecond_tick_timer_get_milliseconds_since_boot();
        if ((now_ms - last_heartbeat_timestamp_ms)
            >= HEARTBEAT_LED_PERIOD_MS)
        {
            toggle_pb22_status_led_on_mainboard();
            last_heartbeat_timestamp_ms = now_ms;
        }
    }
}

static void process_any_obc_uart_bytes_as_chips_frames(void)
{
    while (uart_obc_number_of_bytes_available_in_receive_buffer() > 0u)
    {
        uint8_t received_byte =
            uart_obc_read_one_byte_from_receive_buffer();

        chips_parser_result_type parser_result =
            chips_parser_process_one_received_byte(
                &chips_parser_state,
                received_byte,
                &most_recently_parsed_frame);

        handle_one_chips_parser_result(parser_result);
    }
}

static void handle_one_chips_parser_result(
    chips_parser_result_type parser_result)
{
    if (parser_result == CHIPS_PARSER_RESULT_INCOMPLETE)
    {
        return;
    }

    if (parser_result == CHIPS_PARSER_RESULT_FRAME_READY)
    {
        eps_demo_chips_note_valid_frame_received();
        eps_demo_chips_dispatch_received_command_and_send_response(
            &most_recently_parsed_frame);
        return;
    }

    eps_demo_chips_note_parser_result(parser_result);
}
