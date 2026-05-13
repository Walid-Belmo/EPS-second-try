#include "board_hardware_startup/functions_to_initialize_board_hardware_before_main_loop_runs.h"

#include "clock_configure_48mhz_dfll_open_loop.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "pwm_buck_converter.h"
#include "uart_obc.h"

#ifdef __SAMD21J17D__
#include "mainboard_adc_reader.h"
#endif

void switch_mcu_clock_from_1mhz_to_48mhz(void)
{
    configure_cpu_clock_to_48mhz_using_dfll_open_loop();
}

void setup_millisecond_timer_for_loop_timing(void)
{
    millisecond_tick_timer_initialize_at_48mhz();
}

void setup_sercom0_uart_registers_for_esp32_link(void)
{
    uart_obc_initialize_sercom0_at_115200_baud();
}

void setup_sensor_reading_hardware(void)
{
#ifdef __SAMD21J17D__
    mainboard_adc_reader_initialize();
#endif
}

void setup_pwm_output_starting_off(void)
{
    pwm_buck_converter_initialize_for_demo();
    pwm_buck_converter_set_duty_cycle(0u);
}

void setup_output_pins_starting_off(void)
{
    /*
     * The current working src demo only drives PWM. There is no cleaned PDS
     * driver yet for load switches, eFuses, or heater GPIOs, so this function
     * is intentionally quiet until those drivers are added.
     */
}

void reset_watchdog_timer(void)
{
    /*
     * No watchdog driver exists in the current working code. This function
     * keeps the main loop shape correct and gives us one obvious place to add
     * the real watchdog reset when that driver is written.
     */
}
