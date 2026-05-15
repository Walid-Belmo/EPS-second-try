/*
 * Source Project Sylvester - first firmware skeleton.
 *
 * This file is intentionally only the top-level story.
 * The real implementations will live in separate files so the main loop stays
 * readable for the next person who studies the project.
 */

#include <stdint.h>

#include "board_outputs/functions_to_apply_allowed_pwm_to_board_hardware.h"
#include "board_outputs/functions_to_block_outputs_when_faults_are_injected.h"
#include "board_hardware_startup/functions_to_initialize_board_hardware_before_main_loop_runs.h"
#include "communication_with_esp32/functions_to_read_chips_commands_received_from_esp32.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "externally_controlled_board_behaviors/functions_to_apply_manually_requested_pwm_to_buck_converter.h"
#include "externally_controlled_board_behaviors/functions_to_keep_board_outputs_off_when_requested.h"
#include "externally_controlled_board_behaviors/functions_to_run_manual_control_mode.h"
#include "externally_controlled_board_behaviors/functions_to_run_mppt_algorithm_with_selected_input_source.h"
#include "externally_controlled_board_behaviors/functions_to_run_power_state_machine_with_injected_sensor_values.h"
#include "status_reporting_to_esp32/functions_to_stream_status_replies_to_esp32.h"

#ifdef __SAMD21J17D__
#include "led_status_pb22_active_high_on_mainboard.h"
#endif

/* The status LED blinks at 1 Hz (toggle every 500 ms) whenever the firmware
 * is running but not in manual-control mode. It is the cheapest possible
 * "the chip is alive and the main loop is turning" signal for anyone looking
 * at the board. Manual mode takes the LED over for direct operator control. */
#define STATUS_LED_HEARTBEAT_TOGGLE_PERIOD_IN_MILLISECONDS 500u

static uint32_t timestamp_of_last_status_led_heartbeat_toggle_in_milliseconds;

static void toggle_status_led_for_heartbeat_if_due_and_not_in_manual_mode(void);

int main(void)
{
    // The SAMD21 starts at 1 MHz; the existing drivers assume 48 MHz.
    switch_mcu_clock_from_1mhz_to_48mhz();

    // Configure the hardware blocks used by the demo firmware.
    setup_sercom0_uart_registers_for_esp32_link();
    setup_millisecond_timer_for_loop_timing();
    setup_sensor_reading_hardware();
    setup_pwm_output_starting_off();
    setup_output_pins_starting_off();

    // Start command parsing and all command-controlled RAM from safe values.
    start_esp32_command_reader_with_empty_message_state();
    set_starting_runtime_state_to_safe_defaults();

    timestamp_of_last_status_led_heartbeat_toggle_in_milliseconds =
        millisecond_tick_timer_get_milliseconds_since_boot();

    while (1) /* @non-terminating@ */
    {
        // Read computer commands that arrived through the ESP32.
        read_and_execute_commands_from_esp32();

        // Heartbeat blink runs in every mode except manual, so the operator
        // sees a steady 1 Hz LED pulse whenever the firmware is running and
        // not under their direct control.
        toggle_status_led_for_heartbeat_if_due_and_not_in_manual_mode();

        // Run exactly the behavior requested by the last valid command.
        if (requested_mode_is_off())
        {
            run_off_mode();
        }
        else if (requested_mode_is_flight())
        {
            run_full_satellite_logic();
        }
        else if (requested_mode_is_mppt_test())
        {
            run_mppt_test_only();
        }
        else if (requested_mode_is_state_test())
        {
            run_state_transition_test_only();
        }
        else if (requested_mode_is_fixed_pwm_test())
        {
            run_fixed_pwm_test_only();
        }
        else if (requested_mode_is_manual())
        {
            run_manual_control_mode();
        }

        // Safety and hardware writes stay after the mode decision.
        block_dangerous_outputs();
        apply_outputs_to_board();
        send_status_if_needed();
        reset_watchdog_timer();
    }
}

static void toggle_status_led_for_heartbeat_if_due_and_not_in_manual_mode(void)
{
#ifdef __SAMD21J17D__
    if (requested_mode_is_manual())
    {
        // Manual mode owns the LED. The runner re-asserts the requested
        // level every iteration, so we just need to leave it alone here.
        return;
    }

    uint32_t now_ms = millisecond_tick_timer_get_milliseconds_since_boot();
    uint32_t elapsed_since_last_toggle_ms =
        now_ms - timestamp_of_last_status_led_heartbeat_toggle_in_milliseconds;
    if (elapsed_since_last_toggle_ms
        < STATUS_LED_HEARTBEAT_TOGGLE_PERIOD_IN_MILLISECONDS)
    {
        return;
    }

    toggle_pb22_status_led_on_mainboard();
    timestamp_of_last_status_led_heartbeat_toggle_in_milliseconds = now_ms;
#else
    /* No status LED on the dev board. */
    (void)timestamp_of_last_status_led_heartbeat_toggle_in_milliseconds;
#endif
}
