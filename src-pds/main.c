/*
 * Source Project Sylvester - first firmware skeleton.
 *
 * This file is intentionally only the top-level story.
 * The real implementations will live in separate files so the main loop stays
 * readable for the next person who studies the project.
 */

#include <stdint.h>

#include "esp32_commands.h"

void switch_mcu_clock_from_1mhz_to_48mhz(void);
void setup_sercom0_uart_registers_for_esp32_link(void);
void setup_sensor_reading_hardware(void);
void setup_pwm_output_starting_off(void);
void setup_output_pins_starting_off(void);
void set_starting_ram_values_to_safe_defaults(void);

uint8_t requested_mode_is_off(void);
uint8_t requested_mode_is_flight(void);
uint8_t requested_mode_is_mppt_test(void);
uint8_t requested_mode_is_state_test(void);
uint8_t requested_mode_is_fixed_pwm_test(void);

void run_off_mode(void);
void run_full_satellite_logic(void);
void run_mppt_test_only(void);
void run_state_transition_test_only(void);
void run_fixed_pwm_test_only(void);

void block_dangerous_outputs(void);
void apply_outputs_to_board(void);
void send_status_if_needed(void);
void reset_watchdog_timer(void);

int main(void)
{
    // Requirement: UART, timers, ADC, and PWM must run at predictable speeds.
    // The SAMD21 starts at 1 MHz after reset, but the current drivers assume
    // the CPU clock has been switched to 48 MHz.
    switch_mcu_clock_from_1mhz_to_48mhz();

    // Requirement: the board must exchange bytes with the ESP32 over UART.
    // SERCOM0 is the SAMD21 hardware block used as UART here. This configures
    // its clocks, PA10/PA11 pins, 115200 baud rate, and receive interrupt.
    setup_sercom0_uart_registers_for_esp32_link();

    // Requirement: the first ESP32 command after reset must be read from a
    // clean CHIPS state. This clears the RAM used to remember a partly
    // received CHIPS message.
    start_esp32_command_reader_with_empty_message_state();

    // Requirement: the EPS must measure voltages, currents, and temperatures.
    // This prepares the sensor hardware so later loop steps can read values.
    // For the current board, this means configuring ADC pins, ADC clocks,
    // and ADC registers.
    setup_sensor_reading_hardware();

    // Requirement: the EPS must control the buck converter safely.
    // The PWM pins are configured, but the duty cycle starts at zero.
    setup_pwm_output_starting_off();

    // Requirement: the EPS must control board switches safely.
    // Load switches, eFuses, and heater outputs start OFF.
    setup_output_pins_starting_off();

    // Requirement: after reset, software decisions must start safe.
    // This writes known values into the RAM variables used by the main loop:
    // requested mode = OFF, requested PWM = 0, output requests = OFF,
    // telemetry streaming = disabled, and demo input values = safe defaults.
    set_starting_ram_values_to_safe_defaults();

    while (1)
    {
        // Computer -> ESP32 -> board:
        // Read bytes from UART, rebuild CHIPS messages, and execute commands.
        // A command can set requested mode to OFF, FLIGHT, MPPT_TEST,
        // STATE_TEST, or FIXED_PWM_TEST.
        read_and_execute_commands_from_esp32();

        // Decide what this loop will run right now.
        // The selected mode is stored in RAM and can be changed by a valid
        // command from the ESP32.
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

        // Even if the selected mode asks for PWM, loads, heater, or eFuses,
        // this final check can still force dangerous outputs off.
        block_dangerous_outputs();

        // Apply the final allowed outputs to real pins.
        apply_outputs_to_board();

        // Send status to the ESP32 so the computer UI can show exactly what ran.
        send_status_if_needed();

        // Requirement: the firmware must not silently freeze.
        // This tells the MCU safety timer that the main loop is still alive.
        reset_watchdog_timer();
    }
}
