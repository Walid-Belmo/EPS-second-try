/*
 * functions_to_initialize_board_hardware_before_main_loop_runs.c
 *
 * The seven setup_* functions main() calls once at boot, in the
 * specific order they're listed in app/main.c, before the main
 * while(1) loop starts. Plus reset_watchdog_timer() which is called
 * once per main-loop iteration.
 *
 * Boot sequence (matches the order in app/main.c):
 *
 *   1. switch_mcu_clock_from_1mhz_to_48mhz()
 *        Crank the CPU from the 1 MHz default OSC8M/8 to the 48 MHz
 *        DFLL. Must run first because every other peripheral's
 *        timing constants (UART baud, millisecond tick, PWM
 *        frequency) assume 48 MHz.
 *
 *   2. setup_sercom0_uart_registers_for_esp32_link()
 *        Bring the OBC UART online so the ESP32 can talk to the
 *        board. Done early so any bring-up message can be transmitted.
 *
 *   3. setup_millisecond_timer_for_loop_timing()
 *        Start the ARM SysTick at 1 ms cadence. Anything that calls
 *        millisecond_tick_timer_get_milliseconds_since_boot() (the
 *        control-loop period gate, the stream-period gate, the
 *        heartbeat) needs this running first.
 *
 *   4. setup_sensor_reading_hardware()
 *        ADC + I²C + INA226 chips. Mainboard-only — devboard has no
 *        sensors.
 *
 *   5. setup_pwm_output_starting_off()
 *        TCC0 configured for 300 kHz buck PWM with the dead-time
 *        workaround for the PCB pin-pair design error. Initial duty
 *        is 0 so the converter is dark until the first mode-runner
 *        iteration commands a non-zero value.
 *
 *   6. setup_output_pins_starting_off()
 *        The four mainboard GPIO outputs (status LED, PV switch,
 *        BAT switch) configured as outputs at LOW, plus the four
 *        eFuse status inputs (PA18-21) configured as inputs.
 *
 *   7. start_esp32_command_reader_with_empty_message_state()
 *        (lives in a sibling file, NOT this one) — resets the CHIPS
 *        parser to its idle state so a stray RAM value isn't
 *        mistaken for a partial frame.
 *
 *   8. set_starting_runtime_state_to_safe_defaults()
 *        (lives in runtime_state/, NOT this one) — initialises every
 *        field of the runtime-state singleton.
 *
 * Then the while(1) loop starts. reset_watchdog_timer() is called
 * once per iteration; today it's an empty stub.
 */

#include "board_hardware_startup/functions_to_initialize_board_hardware_before_main_loop_runs.h"

#include "clock_configure_48mhz_dfll_open_loop.h"
#include "millisecond_tick_timer_using_arm_systick.h"
#include "pwm_buck_converter.h"
#include "uart_obc.h"

/* Mainboard-only headers. The dev board doesn't physically have any
 * of these chips/pins, so these drivers don't compile in. The whole
 * #ifdef block makes the same source compile for both targets. */
#ifdef __SAMD21J17D__
#include "mainboard_adc_reader.h"
#include "led_status_pb22_active_high_on_mainboard.h"
#include "gpio_pv_efuse_enable_pa16_on_mainboard.h"
#include "gpio_bat_efuse_enable_pa17_on_mainboard.h"
#include "gpio_efuse_status_inputs_pa18_to_pa21_on_mainboard.h"
#include "i2c_master_sercom3_pa22_pa23_on_mainboard.h"
#include "ina226_battery_on_mainboard.h"
#include "ina226_panel_on_mainboard.h"
#endif

/*
 * Step 1 of the boot sequence: jump the CPU from the 1 MHz default
 * (OSC8M divided by 8) up to 48 MHz using the on-chip DFLL in
 * open-loop mode. Must run before any peripheral that has timing
 * dependencies — UART baud divisors, the millisecond SysTick reload,
 * the buck-converter PWM period — all assume 48 MHz.
 *
 * The actual register dance lives in
 * src/drivers/clock_configure_48mhz_dfll_open_loop.c. The "open loop"
 * part means the DFLL runs on its factory-trimmed nominal value
 * without an external 32 kHz reference; the Curiosity Nano dev board
 * has no such crystal and the mainboard doesn't either.
 */
void switch_mcu_clock_from_1mhz_to_48mhz(void)
{
    configure_cpu_clock_to_48mhz_using_dfll_open_loop();
}

/*
 * Boot step 3: start the millisecond tick timer that the rest of the
 * firmware uses to measure elapsed time. Driven by the ARM Cortex-M0+
 * SysTick at 48 MHz, configured to interrupt every millisecond.
 *
 * Anything that calls millisecond_tick_timer_get_milliseconds_since_boot()
 * needs this running first: the control-loop period gate
 * (pds_control_loop_period_has_elapsed), the stream-period gate
 * (send_status_if_needed), and the heartbeat blink in main.c.
 */
void setup_millisecond_timer_for_loop_timing(void)
{
    millisecond_tick_timer_initialize_at_48mhz();
}

/*
 * Boot step 2: bring the OBC UART online at 115200 baud. On
 * mainboard this is SERCOM0 on PA10/PA11; on devboard it's also
 * SERCOM0 but on PA10/PA11 of the J3 header (or the Curiosity Nano
 * pin headers). The driver is interrupt-driven on RX so bytes
 * arriving while the main loop is busy still get buffered for the
 * next iteration.
 *
 * Done early in the boot sequence so any startup logging or first
 * status reply can be transmitted. Note: nothing actually transmits
 * during boot in this project — the operator's web app initiates
 * everything.
 */
void setup_sercom0_uart_registers_for_esp32_link(void)
{
    uart_obc_initialize_sercom0_at_115200_baud();
}

/*
 * Boot step 4: start the sensor-reading peripherals. Mainboard-only
 * because none of the relevant chips exist on the dev board.
 *
 * Order matters: the ADC reader and I²C master must be brought up
 * before the INA226 modules try to talk to their chips, because
 * those battery_/panel_ina226_initialize() calls do an actual I²C
 * write (the chip's CONFIGURATION and CALIBRATION registers).
 *
 * Devboard build: the whole block compiles out, sensors are
 * non-existent, and the Layer 1 sensor abstraction falls back to
 * injected values for everything.
 */
void setup_sensor_reading_hardware(void)
{
#ifdef __SAMD21J17D__
    /* ADC for analog sensors: LT6108 amplifier outputs (OUTA1,
     * OUTA2), TPS25940 IMON outputs (PV_IMON, BAT_IMON), and the
     * resistor-divider rail voltages (OUTV1, OUTV2). All mapped to
     * specific ADC channels in the driver header. */
    mainboard_adc_reader_initialize();

    /* I²C master on SERCOM3, PA22 (SDA) / PA23 (SCL), at 100 kHz.
     * Used by the INA226 chips. SERCOM choice details: see the
     * driver file's top comment. */
    i2c_master_initialize();

    /* Configure the two INA226 chips: write their CONFIGURATION
     * register (averaging, conversion times, mode) and their
     * CALIBRATION register (so the chip's internal CURRENT register
     * comes out in the units we picked). Both are independent — the
     * battery one is at I²C address 0x41, the panel one at 0x40
     * (initial guesses, see driver headers' caveats). */
    battery_ina226_initialize();
    panel_ina226_initialize();
#endif
}

/*
 * Boot step 5: configure TCC0 for the 300 kHz buck-converter PWM,
 * with the dead-time-via-two-slices workaround for the PCB pin-pair
 * design error (PA12 and PA13 are both upper-half outputs of
 * different DTI slices, so the natural complementary-pair mode of
 * TCC0 isn't usable directly — see docs/pcb_design_error_for_pwm.md
 * for the full story).
 *
 * Initial duty is 0 so the converter is dark until a mode runner
 * commands a non-zero value. This is critical: at boot the firmware
 * does NOT know what kind of load is on the panel or battery rails;
 * driving any PWM at all before the operator says so could destroy
 * something.
 */
void setup_pwm_output_starting_off(void)
{
    pwm_buck_converter_initialize_for_demo();
    pwm_buck_converter_set_duty_cycle(0u);
}

/*
 * Boot step 6: configure the four mainboard GPIO outputs (status
 * LED, PV-side eFuse enable, BAT-side eFuse enable) and the four
 * eFuse status input pins (PV_PGOOD, BAT_PGOOD, ~PV_FLT, ~BAT_FLT).
 *
 * All output pins start LOW: the LED is off, both eFuses are
 * disabled. Same reasoning as the PWM start-at-zero rule — at boot
 * we don't know what's on the rails downstream of those eFuses, so
 * leaving them off until the operator (or the EPS state machine in
 * future flight mode) explicitly enables them is the safe default.
 *
 * The four input pins are configured as inputs with INEN bit set so
 * the SAMD21's input buffer is connected to the pin. Without INEN
 * the chip cannot read the line's actual voltage; the read-back
 * function used by the manual mode runner depends on this being set
 * here.
 */
void setup_output_pins_starting_off(void)
{
#ifdef __SAMD21J17D__
    configure_pb22_as_gpio_output_for_status_led_on_mainboard();
    configure_pa16_as_gpio_output_for_pv_efuse_enable_on_mainboard();
    configure_pa17_as_gpio_output_for_bat_efuse_enable_on_mainboard();
    configure_pa18_to_pa21_as_gpio_inputs_for_efuse_status_on_mainboard();
#endif
}

/*
 * Called every main-loop iteration. The function exists in the
 * iteration sequence so the location is obvious when someone writes
 * the real watchdog driver — they only have to fill in this body and
 * add a watchdog_initialize() call to the boot sequence above.
 *
 * The SAMD21 has a built-in WDT peripheral (Watchdog Timer) that
 * resets the chip if a designated register is not written within a
 * configured timeout. Adding it here is roughly 30 lines of register
 * code in a new driver file (similar in shape to
 * src/drivers/clock_configure_48mhz_dfll_open_loop.c) plus a one-line
 * "kick" call from this function.
 *
 * Currently empty because the project hasn't reached the radiation/
 * fault-recovery phase yet. Listed in docs/requirements.md as a
 * known gap (MCU-11).
 */
void reset_watchdog_timer(void)
{
}
