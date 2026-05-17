/*
 * ina226_register_protocol.c
 *
 * See header for the architectural picture and the datasheet citation.
 * Every function here is pure — no hardware, no globals, no side effects
 * beyond the return value. That lets the self-test exercise them on every
 * build target.
 */

#include <stdint.h>

#include "assertion_handler.h"
#include "sensor_inputs/ina226_register_protocol.h"

/* ----------------------------------------------------------------------
 * Decoders
 * ---------------------------------------------------------------------- */

int32_t decode_ina226_shunt_voltage_register_to_microvolts(uint16_t reg)
{
    /* The register is signed 16-bit in two's complement, LSB = 2.5 µV.
     * Sign-extend to int32_t first, then multiply. The multiplication is
     * by 5/2 because 2.5 cannot be represented exactly in integer math. */
    int16_t signed_register = (int16_t)reg;
    int32_t scaled = (int32_t)signed_register * 5;
    /* Round-to-nearest division by 2. */
    if (scaled >= 0)
    {
        return (scaled + 1) / 2;
    }
    return (scaled - 1) / 2;
}

uint16_t decode_ina226_bus_voltage_register_to_millivolts(uint16_t reg)
{
    /* Bus voltage register is unsigned. LSB = 1.25 mV.
     * millivolts = (reg * 5) / 4. Max register value 0x7FFF gives
     * 32767 * 5 / 4 = 40958 mV which fits in uint16_t (max 65535). */
    uint32_t scaled = (uint32_t)reg * 5u;
    return (uint16_t)(scaled / 4u);
}

int32_t decode_ina226_current_register_to_milliamps(
    uint16_t reg,
    uint32_t current_lsb_in_microamps)
{
    SATELLITE_ASSERT(current_lsb_in_microamps > 0u);

    /* Current register is signed 16-bit. Result in mA = (signed_reg *
     * current_LSB_uA) / 1000. Sign-extend before multiplying. */
    int16_t signed_register = (int16_t)reg;
    int64_t product =
        (int64_t)signed_register * (int64_t)current_lsb_in_microamps;
    /* Round-to-nearest division by 1000. */
    if (product >= 0)
    {
        return (int32_t)((product + 500) / 1000);
    }
    return (int32_t)((product - 500) / 1000);
}

/* ----------------------------------------------------------------------
 * Encoders / pickers
 * ---------------------------------------------------------------------- */

uint32_t compute_ina226_current_lsb_in_microamps(
    uint32_t expected_max_current_in_milliamps)
{
    SATELLITE_ASSERT(expected_max_current_in_milliamps > 0u);

    /* Current_LSB = max_current / 32768. Use the 32-bit-friendly form
     *   current_LSB_uA = (max_current_mA * 1000 + 16384) / 32768
     * The +16384 is round-to-nearest. The result is at least 1 because
     * the assertion above rules out zero input. */
    uint32_t result =
        (expected_max_current_in_milliamps * 1000u + 16384u) / 32768u;
    if (result == 0u)
    {
        result = 1u;
    }
    return result;
}

uint16_t compute_ina226_calibration_constant(
    uint32_t expected_max_current_in_milliamps,
    uint32_t shunt_resistance_in_microohms)
{
    SATELLITE_ASSERT(expected_max_current_in_milliamps > 0u);
    SATELLITE_ASSERT(shunt_resistance_in_microohms > 0u);

    /* From the datasheet: CAL = 0.00512 / (current_LSB × R_shunt) where
     * current_LSB is in amps and R_shunt is in ohms. Convert to integer
     * units (µA and µΩ respectively):
     *
     *   CAL = 0.00512 / ((current_LSB_uA / 1e6) × (R_uOhm / 1e6))
     *       = 0.00512 × 1e12 / (current_LSB_uA × R_uOhm)
     *       = 5_120_000_000_000 / (current_LSB_uA × R_uOhm)
     *
     * The dividend is 5.12e12 — wider than uint32_t — so a uint64_t
     * intermediate is required. uint64_t arithmetic is software-emulated
     * on the Cortex-M0+, which is acceptable here because this function
     * is called once per chip at configure time, not in the control loop. */
    uint32_t current_lsb_in_microamps =
        compute_ina226_current_lsb_in_microamps(
            expected_max_current_in_milliamps);
    uint64_t denominator =
        (uint64_t)current_lsb_in_microamps
        * (uint64_t)shunt_resistance_in_microohms;
    SATELLITE_ASSERT(denominator > 0u);

    uint64_t cal = 5120000000000ULL / denominator;
    if (cal > 65535u)
    {
        cal = 65535u;
    }
    return (uint16_t)cal;
}

uint16_t default_ina226_configuration_register_value(void)
{
    /* Bit layout per datasheet:
     *   15    RST            : 0 (do not soft-reset)
     *   14    reserved       : 1 (post-reset value, datasheet says always 1)
     *   13:9  AVG[2:0]       : 3-bit average count selector
     *                          010 = 16 samples (good middle ground for noise vs. latency)
     *   11:6  VBUSCT[2:0]    : bus conversion time
     *                          100 = 1.1 ms (default after reset)
     *   8:6   VSHCT[2:0]     : shunt conversion time
     *                          100 = 1.1 ms
     *   2:0   MODE[2:0]      : 111 = shunt + bus, continuous
     *
     * Reading bit positions left-to-right gives 0100 0001 0010 0111 = 0x4127.
     * That's the chip's default after power-on reset, which is already
     * what we want for the first revision. */
    return 0x4127u;
}

/* ----------------------------------------------------------------------
 * Built-in self test. Datasheet-documented examples used as oracles.
 * ---------------------------------------------------------------------- */

uint8_t ina226_register_protocol_built_in_self_test(void)
{
    /* 1. Bus voltage decode: register value 0x0FA0 (= 4000 decimal) at
     *    1.25 mV per LSB should give 5000 mV. */
    if (decode_ina226_bus_voltage_register_to_millivolts(0x0FA0u) != 5000u)
    {
        return 0u;
    }

    /* 2. Bus voltage decode: register 0x7FFF at 1.25 mV per LSB → 40958 mV. */
    if (decode_ina226_bus_voltage_register_to_millivolts(0x7FFFu) != 40958u)
    {
        return 0u;
    }

    /* 3. Shunt voltage decode: positive value 0x0014 (20) at 2.5 µV/LSB
     *    → 50 µV. */
    if (decode_ina226_shunt_voltage_register_to_microvolts(0x0014u) != 50)
    {
        return 0u;
    }

    /* 4. Shunt voltage decode: negative two's-complement 0xFFEC (-20) at
     *    2.5 µV/LSB → -50 µV. */
    if (decode_ina226_shunt_voltage_register_to_microvolts(0xFFECu) != -50)
    {
        return 0u;
    }

    /* 5. Current decode: register 0x03E8 (1000) with current_LSB = 100 µA
     *    → 100 mA. */
    if (decode_ina226_current_register_to_milliamps(0x03E8u, 100u) != 100)
    {
        return 0u;
    }

    /* 6. Current decode: negative two's-complement 0xFC18 (-1000) with
     *    current_LSB = 100 µA → -100 mA. */
    if (decode_ina226_current_register_to_milliamps(0xFC18u, 100u) != -100)
    {
        return 0u;
    }

    /* 7. Current LSB picker: max 5000 mA → ~152.6 µA per LSB, rounds to 153. */
    if (compute_ina226_current_lsb_in_microamps(5000u) != 153u)
    {
        return 0u;
    }

    /* 8. Calibration constant: max 5000 mA, shunt 2000 µΩ.
     *    current_LSB = 153 µA. CAL = 5.12e12 / (153 * 2000) = 16732. */
    uint16_t cal_for_typical_solar_rail =
        compute_ina226_calibration_constant(5000u, 2000u);
    if (cal_for_typical_solar_rail != 16732u)
    {
        return 0u;
    }

    /* 9. Default configuration value matches the datasheet's post-reset
     *    bit pattern. */
    if (default_ina226_configuration_register_value() != 0x4127u)
    {
        return 0u;
    }

    return 1u;
}
