#include <stdint.h>

#include "i2c_master_sercom3_pa22_pa23_on_mainboard.h"
#include "ina226_battery_on_mainboard.h"
#include "sensor_inputs/ina226_register_protocol.h"

#define BATTERY_INA226_SEVEN_BIT_ADDRESS    0x46u
#define BATTERY_RAIL_EXPECTED_MAX_CURRENT_IN_MILLIAMPS 5000u
#define BATTERY_RAIL_SHUNT_RESISTANCE_IN_MICROOHMS     2000u

/* Cached current_LSB so the runtime read does not have to recompute it
 * on every call. Set by battery_ina226_initialize(). */
static uint32_t cached_current_lsb_in_microamps = 0u;

void battery_ina226_initialize(void)
{
    cached_current_lsb_in_microamps =
        compute_ina226_current_lsb_in_microamps(
            BATTERY_RAIL_EXPECTED_MAX_CURRENT_IN_MILLIAMPS);

    /* Configuration register first so the chip starts converting. */
    (void)i2c_master_write_register(
        BATTERY_INA226_SEVEN_BIT_ADDRESS,
        INA226_REGISTER_CONFIGURATION,
        default_ina226_configuration_register_value());

    /* Calibration register — the chip uses this to scale the internal
     * shunt-voltage sample into the CURRENT register so we can read
     * current directly without doing the multiplication ourselves. */
    (void)i2c_master_write_register(
        BATTERY_INA226_SEVEN_BIT_ADDRESS,
        INA226_REGISTER_CALIBRATION,
        compute_ina226_calibration_constant(
            BATTERY_RAIL_EXPECTED_MAX_CURRENT_IN_MILLIAMPS,
            BATTERY_RAIL_SHUNT_RESISTANCE_IN_MICROOHMS));
}

uint16_t battery_ina226_read_voltage_mv(void)
{
    uint16_t register_value = 0u;
    if (i2c_master_read_register(
            BATTERY_INA226_SEVEN_BIT_ADDRESS,
            INA226_REGISTER_BUS_VOLTAGE,
            &register_value) != I2C_MASTER_STATUS_OK)
    {
        return 0u;
    }
    return decode_ina226_bus_voltage_register_to_millivolts(register_value);
}

int16_t battery_ina226_read_current_ma(void)
{
    if (cached_current_lsb_in_microamps == 0u)
    {
        /* Driver was never initialised — return 0 instead of dividing by
         * an undefined LSB. */
        return 0;
    }

    uint16_t register_value = 0u;
    if (i2c_master_read_register(
            BATTERY_INA226_SEVEN_BIT_ADDRESS,
            INA226_REGISTER_CURRENT,
            &register_value) != I2C_MASTER_STATUS_OK)
    {
        return 0;
    }
    int32_t milliamps =
        decode_ina226_current_register_to_milliamps(
            register_value, cached_current_lsb_in_microamps);

    /* Clamp to int16_t range. The Layer 1 contract is int16_t. At
     * ±5 A peak we have headroom but a bus glitch could decode an
     * out-of-range value. */
    if (milliamps > 32767)
    {
        return (int16_t)32767;
    }
    if (milliamps < -32768)
    {
        return (int16_t)-32768;
    }
    return (int16_t)milliamps;
}
