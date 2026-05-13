#ifndef BOARD_STARTUP_H
#define BOARD_STARTUP_H

void switch_mcu_clock_from_1mhz_to_48mhz(void);
void setup_millisecond_timer_for_loop_timing(void);
void setup_sercom0_uart_registers_for_esp32_link(void);
void setup_sensor_reading_hardware(void);
void setup_pwm_output_starting_off(void);
void setup_output_pins_starting_off(void);
void reset_watchdog_timer(void);

#endif /* BOARD_STARTUP_H */
