// =============================================================================
// eps_simulation_runner.c
// Closed-loop EPS state machine simulation with multiple scenarios.
//
// This program simulates the complete EPS power system in a closed loop:
//   1. Battery model computes terminal voltage from SOC and current
//   2. Buck converter model computes panel voltage from duty cycle
//   3. Solar panel model computes panel current from voltage and irradiance
//   4. Power conservation determines battery current
//   5. All values converted to integer millivolts/milliamps and 12-bit ADC
//   6. The EXACT firmware state machine code processes one iteration
//   7. Output duty cycle feeds back to step 2
//   8. CSV data logged for analysis
//
// The firmware code in src/eps_state_machine.c is identical to what runs on
// the SAMD21 MCU. The physics models are float-based PC-only code.
//
// Usage:
//   ./run_eps_simulation [scenario_number]
//   scenario_number: 1-8 (default: 1)
//
// Category: SIMULATION (PC only)
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/assertion_handler.h"
#include "../src/mppt_algorithm.h"
#include "../src/eps_configuration_parameters.h"
#include "../src/eps_state_machine.h"
#include "solar_panel_simulator.h"
#include "buck_converter_model.h"
#include "battery_model.h"

// ── Simulation parameters ───────────────────────────────────────────────────

// Time step per iteration — must match expected firmware superloop period.
// The MCU runs at 48 MHz; one loop iteration is dominated by ADC conversion
// time (~100-200 us). Placeholder: 200 us.
#define SIMULATION_TIME_STEP_IN_SECONDS     0.0002

// Iterations per scenario (configurable per scenario below)
#define DEFAULT_ITERATIONS_PER_SCENARIO     500000

// ADC configuration matching the SAMD21 hardware
#define ADC_RESOLUTION_BITS                 12
#define VOLTAGE_ADC_REFERENCE_VOLTS         25.0
#define CURRENT_ADC_REFERENCE_AMPS          3.0
#define ADC_NOISE_FRACTION                  0.02

// Load current drawn by all subsystems (approximate, in amps)
// Average power ~8W at ~7.4V battery = ~1.08A
#define NOMINAL_LOAD_CURRENT_IN_AMPS        1.08

// ── Configuration thresholds (placeholder values per plan Part 8) ───────────

static void fill_default_configuration_thresholds(
    struct eps_configuration_thresholds *thresholds)
{
    SATELLITE_ASSERT(thresholds != (void *)0);

    thresholds->battery_voltage_maximum_in_millivolts = 8400u;
    thresholds->battery_voltage_full_threshold_in_millivolts = 8300u;
    thresholds->battery_voltage_charge_resume_threshold_in_millivolts = 8100u;
    thresholds->battery_voltage_minimum_in_millivolts = 5000u;
    thresholds->battery_voltage_critical_in_millivolts = 5500u;
    thresholds->battery_voltage_hysteresis_margin_in_millivolts = 200u;

    thresholds->battery_current_maximum_charge_in_milliamps = 2000;
    thresholds->battery_current_maximum_discharge_in_milliamps = -2000;
    thresholds->battery_current_minimum_charge_threshold_in_milliamps = 100;

    thresholds->solar_array_minimum_voltage_for_availability_in_millivolts = 8200u;

    thresholds->temperature_minimum_for_heater_activation_in_decidegrees = -100;
    thresholds->temperature_maximum_for_load_shedding_in_decidegrees = 600;
    thresholds->temperature_minimum_for_charging_allowed_in_decidegrees = 0;

    // Timeouts: at 200us per iteration, 120s = 600000 iterations
    thresholds->mppt_charge_timeout_for_insufficient_buffer_in_iterations = 3000000u;
    thresholds->cv_float_low_voltage_wait_timeout_in_iterations = 500000u;
    thresholds->obc_heartbeat_timeout_in_iterations = 600000u;

    thresholds->cv_float_duty_cycle_adjustment_step_size = 164u;
}

// ── CSV output ──────────────────────────────────────────────────────────────

static void print_csv_header(void)
{
    (void)printf(
        "iteration,time_seconds,pcu_mode,safe_mode,duty_cycle_percent,"
        "battery_voltage_mv,battery_current_ma,battery_soc_percent,"
        "solar_voltage_mv,panel_power_watts,temperature_decideg,"
        "heater_on,panel_efuse_on,loads_enabled\n");
}

static void print_csv_row(
    uint32_t iteration,
    double time_seconds,
    const struct eps_actuator_output_commands *commands,
    const struct eps_sensor_readings_this_iteration *readings,
    double battery_soc_fraction,
    double panel_power_watts)
{
    double duty_cycle_percent =
        (double)commands->buck_converter_duty_cycle_as_fraction_of_65535
        / 65535.0 * 100.0;

    uint8_t loads_count = 0u;
    for (uint8_t i = 0u; i < (uint8_t)EPS_LOAD_COUNT; i += 1u) {
        loads_count += commands->load_enable_flags[i];
    }

    (void)printf(
        "%u,%.4f,%u,%u,%.2f,"
        "%u,%d,%.6f,"
        "%u,%.3f,%d,"
        "%u,%u,%u\n",
        (unsigned)iteration,
        time_seconds,
        (unsigned)commands->current_pcu_mode_for_telemetry,
        (unsigned)commands->safe_mode_alert_flag_for_obc,
        duty_cycle_percent,
        (unsigned)readings->battery_voltage_in_millivolts,
        (int)readings->battery_current_in_milliamps,
        battery_soc_fraction * 100.0,
        (unsigned)readings->solar_array_voltage_in_millivolts,
        panel_power_watts,
        (int)readings->battery_temperature_in_decidegrees_celsius,
        (unsigned)commands->heater_should_be_enabled,
        (unsigned)commands->panel_efuse_should_be_enabled,
        (unsigned)loads_count);
}

// ── Conversion helpers (float physics → integer firmware) ───────────────────

static uint16_t convert_voltage_to_millivolts(double voltage_volts)
{
    if (voltage_volts < 0.0) { return 0u; }
    if (voltage_volts > 65.535) { return 65535u; }
    return (uint16_t)(voltage_volts * 1000.0 + 0.5);
}

static int16_t convert_current_to_milliamps(double current_amps)
{
    double milliamps = current_amps * 1000.0;
    if (milliamps > 32767.0) { return 32767; }
    if (milliamps < -32768.0) { return -32768; }
    return (int16_t)(milliamps + (milliamps >= 0.0 ? 0.5 : -0.5));
}

// ── Core simulation loop ────────────────────────────────────────────────────

static void run_one_scenario(
    uint8_t initial_pcu_mode,
    double initial_battery_soc,
    double initial_temperature_celsius,
    uint32_t total_iterations,
    double (*irradiance_function)(uint32_t iteration),
    double (*temperature_function)(uint32_t iteration,
                                   double initial_temperature),
    uint8_t obc_heartbeat_active,
    uint32_t log_every_n_iterations)
{
    // Initialize models
    struct solar_panel_parameters panel_parameters;
    solar_panel_initialize_parameters(&panel_parameters, 0);

    struct buck_converter_parameters converter_parameters;
    buck_converter_initialize_parameters(&converter_parameters, 7.4);

    struct battery_model_parameters battery_parameters;
    battery_model_initialize_parameters(&battery_parameters, initial_battery_soc);

    struct eps_configuration_thresholds configuration_thresholds;
    fill_default_configuration_thresholds(&configuration_thresholds);

    struct eps_state_machine_persistent_state eps_persistent_state;
    eps_state_machine_initialize(
        &eps_persistent_state, &configuration_thresholds, initial_pcu_mode);

    struct eps_sensor_readings_this_iteration sensor_readings;
    struct eps_actuator_output_commands actuator_commands;

    double previous_battery_current_amps = 0.0;

    print_csv_header();

    for (uint32_t iteration = 0u; iteration < total_iterations; iteration += 1u) {

        double time_seconds =
            (double)iteration * SIMULATION_TIME_STEP_IN_SECONDS;

        // ── Step 1: Get environmental conditions ────────────────────────
        double irradiance_fraction = irradiance_function(iteration);
        double temperature_celsius =
            temperature_function(iteration, initial_temperature_celsius);

        // ── Step 2: Battery terminal voltage ────────────────────────────
        double battery_voltage_volts =
            battery_model_compute_terminal_voltage_under_load(
                &battery_parameters, previous_battery_current_amps);

        // Update the buck converter model with current battery voltage
        converter_parameters.battery_voltage_volts = battery_voltage_volts;

        // ── Step 3: Panel operating point from duty cycle ───────────────
        double panel_voltage_volts = 0.0;
        double panel_current_amps = 0.0;
        double panel_power_watts = 0.0;

        uint16_t current_duty_cycle =
            eps_persistent_state.current_duty_cycle_as_fraction_of_65535;

        if ((irradiance_fraction > 0.001) && (current_duty_cycle > 0u)) {
            panel_voltage_volts =
                buck_converter_compute_panel_voltage_from_duty_cycle(
                    &converter_parameters, current_duty_cycle);

            if (panel_voltage_volts > 0.0) {
                panel_current_amps =
                    solar_panel_compute_current_at_voltage(
                        &panel_parameters,
                        panel_voltage_volts,
                        temperature_celsius,
                        irradiance_fraction);

                if (panel_current_amps < 0.0) {
                    panel_current_amps = 0.0;
                }
            }

            panel_power_watts =
                panel_voltage_volts * panel_current_amps;
        }

        // ── Step 4: Battery current from power balance ──────────────────
        double bus_current_from_solar_amps = 0.0;
        if (battery_voltage_volts > 0.1) {
            bus_current_from_solar_amps =
                panel_power_watts / battery_voltage_volts;
        }

        double load_current_amps = NOMINAL_LOAD_CURRENT_IN_AMPS;

        // Adjust load current based on which loads are enabled
        uint8_t enabled_count = 0u;
        for (uint8_t i = 0u; i < (uint8_t)EPS_LOAD_COUNT; i += 1u) {
            enabled_count +=
                eps_persistent_state.load_is_currently_enabled[i];
        }
        load_current_amps =
            NOMINAL_LOAD_CURRENT_IN_AMPS
            * ((double)enabled_count / (double)EPS_LOAD_COUNT);

        double battery_current_amps =
            bus_current_from_solar_amps - load_current_amps;

        previous_battery_current_amps = battery_current_amps;

        // ── Step 5: Update battery SOC ──────────────────────────────────
        battery_model_update_state_of_charge(
            &battery_parameters,
            battery_current_amps,
            SIMULATION_TIME_STEP_IN_SECONDS);

        // ── Step 6: Convert to firmware-format sensor readings ──────────
        (void)memset(&sensor_readings, 0, sizeof(sensor_readings));

        sensor_readings.battery_voltage_in_millivolts =
            convert_voltage_to_millivolts(battery_voltage_volts);
        sensor_readings.battery_current_in_milliamps =
            convert_current_to_milliamps(battery_current_amps);
        sensor_readings.solar_array_voltage_in_millivolts =
            convert_voltage_to_millivolts(panel_voltage_volts);
        sensor_readings.charging_rail_voltage_in_millivolts =
            convert_voltage_to_millivolts(battery_voltage_volts);
        sensor_readings.battery_temperature_in_decidegrees_celsius =
            (int16_t)(temperature_celsius * 10.0);

        // ADC readings for MPPT algorithm
        uint16_t voltage_adc_raw = 0u;
        uint16_t current_adc_raw = 0u;
        if (panel_voltage_volts > 0.0) {
            buck_converter_convert_to_adc_readings(
                panel_voltage_volts, panel_current_amps,
                VOLTAGE_ADC_REFERENCE_VOLTS, CURRENT_ADC_REFERENCE_AMPS,
                ADC_RESOLUTION_BITS, ADC_NOISE_FRACTION,
                &voltage_adc_raw, &current_adc_raw);
        }
        sensor_readings.solar_array_voltage_raw_adc_reading = voltage_adc_raw;
        sensor_readings.solar_array_current_raw_adc_reading = current_adc_raw;

        sensor_readings.obc_heartbeat_received_this_iteration =
            obc_heartbeat_active;
        sensor_readings.satellite_mode_commanded_by_obc =
            (uint8_t)EPS_SATELLITE_MODE_CHARGING;
        sensor_readings.safe_mode_sub_state_commanded_by_obc =
            (uint8_t)EPS_SAFE_SUB_STATE_COMMUNICATION;

        // ── Step 7: Run the firmware state machine ──────────────────────
        (void)memset(&actuator_commands, 0, sizeof(actuator_commands));

        eps_state_machine_run_one_iteration(
            &eps_persistent_state,
            &sensor_readings,
            &configuration_thresholds,
            &actuator_commands);

        // ── Step 8: Log CSV at configurable interval
        if ((iteration % log_every_n_iterations) == 0u) {
            print_csv_row(
                iteration, time_seconds,
                &actuator_commands, &sensor_readings,
                battery_parameters.current_state_of_charge_fraction,
                panel_power_watts);
        }
    }
}

// ── Irradiance and temperature functions per scenario ────────────────────────

static double irradiance_full_sun(uint32_t iteration)
{
    (void)iteration;
    return 1.0;
}

static double irradiance_eclipse_entry_at_halfway(uint32_t iteration)
{
    // Eclipse starts at iteration 250000 (50 seconds at 200us/iter)
    return (iteration < 250000u) ? 1.0 : 0.0;
}

static double irradiance_eclipse_exit_at_halfway(uint32_t iteration)
{
    // Sun appears at iteration 250000
    return (iteration < 250000u) ? 0.0 : 1.0;
}

static double irradiance_no_sun(uint32_t iteration)
{
    (void)iteration;
    return 0.0;
}

static double irradiance_full_orbit_cycle(uint32_t iteration)
{
    // Orbit period: 94 min = 5640 seconds = 28200000 iterations at 200us
    // Sun period: 57 min = 3420 seconds = 17100000 iterations
    // Eclipse period: 37 min = 2220 seconds = 11100000 iterations
    uint32_t orbit_period_iterations = 28200000u;
    uint32_t sun_period_iterations = 17100000u;
    uint32_t position_in_orbit = iteration % orbit_period_iterations;
    return (position_in_orbit < sun_period_iterations) ? 1.0 : 0.0;
}

static double irradiance_eclipse_heavy_orbit(uint32_t iteration)
{
    // Eclipse-heavy orbit: 30 min sun, 64 min eclipse = 94 min period
    // 30 min = 1800s = 9000000 iterations
    // 94 min = 5640s = 28200000 iterations
    uint32_t orbit_period_iterations = 28200000u;
    uint32_t sun_period_iterations = 9000000u;
    uint32_t position_in_orbit = iteration % orbit_period_iterations;
    return (position_in_orbit < sun_period_iterations) ? 1.0 : 0.0;
}

static double irradiance_rapid_cycling(uint32_t iteration)
{
    // 30 seconds on, 30 seconds off at 200us per iteration
    // 30s = 150000 iterations per half-cycle, 300000 per full cycle
    uint32_t cycle_period = 300000u;
    uint32_t sun_half = 150000u;
    uint32_t position_in_cycle = iteration % cycle_period;
    return (position_in_cycle < sun_half) ? 1.0 : 0.0;
}

static double temperature_constant(
    uint32_t iteration, double initial_temperature)
{
    (void)iteration;
    return initial_temperature;
}

static double temperature_ramp_cold_to_warm(
    uint32_t iteration, double initial_temperature)
{
    // Ramp from initial_temperature to +25C over 5 minutes (1500000 iterations)
    double total_iterations_for_ramp = 1500000.0;
    double target_temperature = 25.0;
    double fraction = (double)iteration / total_iterations_for_ramp;
    if (fraction > 1.0) { fraction = 1.0; }
    return initial_temperature + (fraction * (target_temperature - initial_temperature));
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    int32_t scenario_number = 1;

    if (argc >= 2) {
        scenario_number = atoi(argv[1]);
        if (scenario_number < 1 || scenario_number > 20) {
            (void)fprintf(stderr,
                "Usage: %s [scenario 1-20]\n", argv[0]);
            return 1;
        }
    }

    (void)fprintf(stderr, "Running EPS scenario %d...\n",
                  (int)scenario_number);

    if (scenario_number == 1) {
        // Full sun, battery at 50% SOC → charging
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.50, 25.0,
            500000u, irradiance_full_sun, temperature_constant, 1u, 1000u);
    } else if (scenario_number == 2) {
        // Eclipse entry at halfway
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.70, 25.0,
            500000u, irradiance_eclipse_entry_at_halfway,
            temperature_constant, 1u, 1000u);
    } else if (scenario_number == 3) {
        // Eclipse exit at halfway
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.70, 25.0,
            500000u, irradiance_eclipse_exit_at_halfway,
            temperature_constant, 1u, 1000u);
    } else if (scenario_number == 4) {
        // Battery critically low → safe mode
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.05, 25.0,
            500000u, irradiance_full_sun, temperature_constant, 1u, 1000u);
    } else if (scenario_number == 5) {
        // OBC heartbeat lost
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.70, 25.0,
            1000000u, irradiance_full_sun, temperature_constant, 0u, 1000u);
    } else if (scenario_number == 6) {
        // Cold temperature → heater on, charging forbidden
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.70, -20.0,
            500000u, irradiance_full_sun, temperature_constant, 1u, 1000u);
    } else if (scenario_number == 7) {
        // Eclipse with high load → overcurrent shedding
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.30, 25.0,
            500000u, irradiance_no_sun, temperature_constant, 1u, 1000u);
    } else if (scenario_number == 8) {
        // Full orbit cycle (57 min sun, 37 min eclipse)
        // Run 2 full orbits = ~56 million iterations
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.70, 25.0,
            56400000u, irradiance_full_orbit_cycle,
            temperature_constant, 1u, 1000u);
    } else if (scenario_number == 9) {
        // Battery near full (95% SOC) → expect CV_FLOAT / SA_LOAD_FOLLOW
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.95, 25.0,
            1000000u, irradiance_full_sun, temperature_constant, 1u, 1000u);
    } else if (scenario_number == 10) {
        // Long eclipse (10 minutes) → visible battery decline
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.70, 25.0,
            3000000u, irradiance_no_sun, temperature_constant, 1u, 1000u);
    } else if (scenario_number == 11) {
        // Rapid sun/eclipse cycling (30s on, 30s off for 5 minutes)
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.50, 25.0,
            1500000u, irradiance_rapid_cycling, temperature_constant, 1u, 1000u);
    } else if (scenario_number == 12) {
        // Temperature ramp from -20C to +25C over 5 min
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.70, -20.0,
            1500000u, irradiance_full_sun, temperature_ramp_cold_to_warm, 1u, 1000u);

    // ── Multi-day scenarios ─────────────────────────────────────────────
    } else if (scenario_number == 13) {
        // 1 day nominal (15.3 orbits). Log every 100000 iters = 4320 rows.
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.70, 25.0,
            432000000u, irradiance_full_orbit_cycle,
            temperature_constant, 1u, 100000u);
    } else if (scenario_number == 14) {
        // 3 days nominal (46 orbits). Log every 500000 iters = 2592 rows.
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.70, 25.0,
            1296000000u, irradiance_full_orbit_cycle,
            temperature_constant, 1u, 500000u);
    } else if (scenario_number == 15) {
        // 1 day eclipse-heavy orbit (30 min sun, 64 min eclipse per orbit)
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.90, 25.0,
            432000000u, irradiance_eclipse_heavy_orbit,
            temperature_constant, 1u, 100000u);
    } else if (scenario_number == 16) {
        // 1 day with OBC never responding (permanent autonomy)
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.70, 25.0,
            432000000u, irradiance_full_orbit_cycle,
            temperature_constant, 0u, 100000u);
    } else if (scenario_number == 17) {
        // 5 days nominal (76.6 orbits). Log every 1000000 iters = 2160 rows.
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.70, 25.0,
            2160000000u, irradiance_full_orbit_cycle,
            temperature_constant, 1u, 1000000u);

    // ── Short verification scenarios (log every iteration) ──────────────
    } else if (scenario_number == 18) {
        // VERIFY: Constant sun, 1000 iterations, log ALL
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.50, 25.0,
            1000u, irradiance_full_sun,
            temperature_constant, 1u, 1u);
    } else if (scenario_number == 19) {
        // VERIFY: Eclipse only, 1000 iterations, log ALL
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_BATTERY_DISCHARGE, 0.50, 25.0,
            1000u, irradiance_no_sun,
            temperature_constant, 1u, 1u);
    } else if (scenario_number == 20) {
        // VERIFY: Cold temp (-20C), 1000 iterations, log ALL
        run_one_scenario(
            (uint8_t)EPS_PCU_MODE_MPPT_CHARGE, 0.70, -20.0,
            1000u, irradiance_full_sun,
            temperature_constant, 1u, 1u);
    }

    (void)fprintf(stderr, "Scenario %d complete.\n",
                  (int)scenario_number);

    return 0;
}
