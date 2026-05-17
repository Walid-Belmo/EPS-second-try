/*
 * sensor_readings.c
 *
 * Layer 1 implementation. See header for the architectural picture.
 */

#include <stdint.h>

#include "assertion_handler.h"
#include "eps_state_machine.h"
#include "runtime_state/structures_that_describe_pds_runtime_state.h"
#include "runtime_state/functions_to_access_pds_runtime_state.h"
#include "sensor_inputs/sensor_readings.h"

/* Layer 2 chip modules only exist on the mainboard build. On dev board the
 * REAL_BOARD_HARDWARE branch falls back to the INJECTED value because no
 * real chip exists. Any new chip module added later just needs an extra
 * include here plus a line in the relevant read_X() helper. */
#ifdef __SAMD21J17D__
#include "ina226_battery_on_mainboard.h"
#include "ina226_panel_on_mainboard.h"
#include "voltage_divider_rails_on_mainboard.h"
#endif

/* The single live source. Default is INJECTED so existing demos keep working
 * until the operator deliberately flips this. */
static pds_sensor_source_type current_sensor_source = PDS_SENSOR_SOURCE_INJECTED;

/* Forward declarations for the static helpers that read each measurement
 * in INJECTED mode (legacy behaviour) or REAL_BOARD_HARDWARE mode (mainboard
 * chips). Splitting them keeps read_X() bodies short and obvious. */
static uint16_t read_battery_voltage_from_injected(void);
static int16_t  read_battery_current_from_injected(void);
static uint16_t read_panel_voltage_from_injected(void);
static uint16_t read_panel_current_from_injected(void);
static uint16_t read_charging_rail_voltage_from_injected(void);

#ifdef __SAMD21J17D__
static uint16_t read_battery_voltage_from_real_hardware(void);
static int16_t  read_battery_current_from_real_hardware(void);
static uint16_t read_panel_voltage_from_real_hardware(void);
static int16_t  read_panel_current_from_real_hardware_or_zero(void);
static uint16_t read_charging_rail_voltage_from_real_hardware(void);
#endif

static uint16_t convert_millivolts_to_synthetic_panel_raw_adc(uint16_t mv);
static uint16_t convert_milliamps_to_synthetic_panel_raw_adc(uint16_t ma);

/* ----------------------------------------------------------------------
 * Source selector
 * ---------------------------------------------------------------------- */

void set_sensor_source(pds_sensor_source_type source)
{
    /* Defensive — only the two enum values are legal. Any other value is a
     * caller bug and we want it to be loud rather than silently coerce. */
    SATELLITE_ASSERT(source == PDS_SENSOR_SOURCE_INJECTED
                     || source == PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE);
    current_sensor_source = source;
}

pds_sensor_source_type get_current_sensor_source(void)
{
    return current_sensor_source;
}

/* ----------------------------------------------------------------------
 * Per-measurement reads. Each one branches on the source.
 * ---------------------------------------------------------------------- */

uint16_t read_battery_voltage(void)
{
    if (current_sensor_source == PDS_SENSOR_SOURCE_INJECTED)
    {
        return read_battery_voltage_from_injected();
    }
#ifdef __SAMD21J17D__
    return read_battery_voltage_from_real_hardware();
#else
    /* Dev board has no real chips. Fall back so the state machine still
     * gets a sensible value when the operator flips the source by mistake. */
    return read_battery_voltage_from_injected();
#endif
}

int16_t read_battery_current(void)
{
    if (current_sensor_source == PDS_SENSOR_SOURCE_INJECTED)
    {
        return read_battery_current_from_injected();
    }
#ifdef __SAMD21J17D__
    /* TODO redundancy: today this returns the INA226 reading only. The
     * board also exposes LT6108 (battery_lt6108_read_current_ma) and
     * TPS25940 IMON (battery_tps25940_read_imon_ma) for the same current.
     * A future revision can vote between the three or fall back when one
     * disagrees by more than a tolerance — that decision lives ENTIRELY
     * inside this function. The state machine never sees the difference. */
    return read_battery_current_from_real_hardware();
#else
    return read_battery_current_from_injected();
#endif
}

uint16_t read_panel_voltage(void)
{
    if (current_sensor_source == PDS_SENSOR_SOURCE_INJECTED)
    {
        return read_panel_voltage_from_injected();
    }
#ifdef __SAMD21J17D__
    return read_panel_voltage_from_real_hardware();
#else
    return read_panel_voltage_from_injected();
#endif
}

uint16_t read_panel_current(void)
{
    if (current_sensor_source == PDS_SENSOR_SOURCE_INJECTED)
    {
        return read_panel_current_from_injected();
    }
#ifdef __SAMD21J17D__
    /* TODO redundancy: same shape as read_battery_current(). Today returns
     * the INA226 reading only. */
    int16_t signed_value = read_panel_current_from_real_hardware_or_zero();
    return (signed_value < 0) ? 0u : (uint16_t)signed_value;
#else
    return read_panel_current_from_injected();
#endif
}

uint16_t read_charging_rail_voltage(void)
{
    if (current_sensor_source == PDS_SENSOR_SOURCE_INJECTED)
    {
        return read_charging_rail_voltage_from_injected();
    }
#ifdef __SAMD21J17D__
    return read_charging_rail_voltage_from_real_hardware();
#else
    return read_charging_rail_voltage_from_injected();
#endif
}

/* ----------------------------------------------------------------------
 * One-shot struct filler
 * ---------------------------------------------------------------------- */

void read_all_state_machine_sensor_inputs(
    struct eps_sensor_readings_this_iteration *output)
{
    SATELLITE_ASSERT(output != (void *)0);

    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    const pds_state_demo_inputs_type *injected_supplementary_inputs =
        &runtime_state->injected_state_inputs;

    /* The five real measurements go through the source selector. */
    output->battery_voltage_in_millivolts          = read_battery_voltage();
    output->battery_current_in_milliamps           = read_battery_current();
    output->solar_array_voltage_in_millivolts      = read_panel_voltage();
    output->charging_rail_voltage_in_millivolts    = read_charging_rail_voltage();

    uint16_t panel_current_in_milliamps = read_panel_current();

    /* The MPPT algorithm wants raw 12-bit ADC counts so it can do its
     * cross-multiplication math on integers. When the source is INJECTED
     * we synthesise plausible raw counts from the engineering values; when
     * the source is real hardware we already have the raw counts and would
     * read them directly here in a future revision. For now we synthesise
     * either way to keep the contract identical to the legacy state runner. */
    output->solar_array_voltage_raw_adc_reading =
        convert_millivolts_to_synthetic_panel_raw_adc(
            output->solar_array_voltage_in_millivolts);
    output->solar_array_current_raw_adc_reading =
        convert_milliamps_to_synthetic_panel_raw_adc(panel_current_in_milliamps);

    /* The remaining fields have no real sensor today (battery temp needs
     * the SPI driver, and the OBC fields are command inputs not sensors).
     * Pull them straight from the injected struct in both source modes. */
    output->battery_temperature_in_decidegrees_celsius =
        injected_supplementary_inputs->battery_temperature_in_decidegrees_celsius;
    output->obc_heartbeat_received_this_iteration =
        injected_supplementary_inputs->heartbeat_received;
    output->satellite_mode_commanded_by_obc =
        injected_supplementary_inputs->satellite_mode_from_obc;
    output->safe_mode_sub_state_commanded_by_obc =
        injected_supplementary_inputs->safe_mode_substate_from_obc;
}

/* ----------------------------------------------------------------------
 * INJECTED-source helpers (same numbers the legacy state runner used)
 * ---------------------------------------------------------------------- */

static uint16_t read_battery_voltage_from_injected(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    return runtime_state->injected_state_inputs.battery_voltage_in_millivolts;
}

static int16_t read_battery_current_from_injected(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    return runtime_state->injected_state_inputs.battery_current_in_milliamps;
}

static uint16_t read_panel_voltage_from_injected(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    return runtime_state->injected_state_inputs.panel_voltage_in_millivolts;
}

static uint16_t read_panel_current_from_injected(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    return runtime_state->injected_state_inputs.panel_current_in_milliamps;
}

static uint16_t read_charging_rail_voltage_from_injected(void)
{
    pds_runtime_state_type *runtime_state =
        get_pointer_to_pds_runtime_state();
    return runtime_state->injected_state_inputs.charging_rail_voltage_in_millivolts;
}

/* ----------------------------------------------------------------------
 * REAL-HARDWARE helpers (mainboard only)
 * ---------------------------------------------------------------------- */

#ifdef __SAMD21J17D__

static uint16_t read_battery_voltage_from_real_hardware(void)
{
    /* INA226 measures the bus voltage on the rail it is connected to. */
    return battery_ina226_read_voltage_mv();
}

static int16_t read_battery_current_from_real_hardware(void)
{
    return battery_ina226_read_current_ma();
}

static uint16_t read_panel_voltage_from_real_hardware(void)
{
    return panel_ina226_read_voltage_mv();
}

static int16_t read_panel_current_from_real_hardware_or_zero(void)
{
    return panel_ina226_read_current_ma();
}

static uint16_t read_charging_rail_voltage_from_real_hardware(void)
{
    /* The charging rail is read through a resistive divider into the ADC,
     * not through any I²C chip. */
    return rail_divider_read_charging_rail_voltage_mv();
}

#endif /* __SAMD21J17D__ */

/* ----------------------------------------------------------------------
 * MPPT raw-count synthesis. Pure logic. Identical to the helpers that
 * lived in functions_to_run_power_state_machine_with_injected_sensor_values.c
 * before the move. They synthesise a 12-bit ADC count from the engineering
 * value so the MPPT algorithm has integers to cross-multiply.
 * ---------------------------------------------------------------------- */

static uint16_t convert_millivolts_to_synthetic_panel_raw_adc(uint16_t mv)
{
    uint16_t raw =
        (uint16_t)(mv / PDS_PANEL_RAW_ADC_TO_MILLIVOLTS_SCALE);
    return (raw > 4095u) ? 4095u : raw;
}

static uint16_t convert_milliamps_to_synthetic_panel_raw_adc(uint16_t ma)
{
    uint16_t raw =
        (uint16_t)(ma / PDS_PANEL_RAW_ADC_TO_MILLIAMPS_SCALE);
    return (raw > 4095u) ? 4095u : raw;
}
