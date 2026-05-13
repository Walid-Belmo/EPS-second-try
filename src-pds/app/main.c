/*
 * Source Project Sylvester - first firmware skeleton.
 *
 * This file is intentionally only the top-level story.
 * The real implementations will live in separate files so the main loop stays
 * readable for the next person who studies the project.
 */

#include "board_outputs/functions_to_apply_allowed_pwm_to_board_hardware.h"
#include "board_outputs/functions_to_block_outputs_when_faults_are_injected.h"
#include "board_hardware_startup/functions_to_initialize_board_hardware_before_main_loop_runs.h"
#include "communication_with_esp32/functions_to_read_chips_commands_received_from_esp32.h"
#include "command_controlled_ram_values/functions_to_store_values_changed_by_esp32_commands.h"
#include "externally_controlled_board_behaviors/functions_to_apply_manually_requested_pwm_to_buck_converter.h"
#include "externally_controlled_board_behaviors/functions_to_keep_board_outputs_off_when_requested.h"
#include "externally_controlled_board_behaviors/functions_to_run_mppt_algorithm_with_simulated_solar_panel_curve.h"
#include "externally_controlled_board_behaviors/functions_to_run_power_state_machine_with_injected_sensor_values.h"
#include "status_reporting_to_esp32/functions_to_stream_status_replies_to_esp32.h"

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
    set_starting_ram_values_to_safe_defaults();

    while (1) /* @non-terminating@ */
    {
        // Read computer commands that arrived through the ESP32.
        read_and_execute_commands_from_esp32();

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

        // Safety and hardware writes stay after the mode decision.
        block_dangerous_outputs();
        apply_outputs_to_board();
        send_status_if_needed();
        reset_watchdog_timer();
    }
}
