/*
 * ina226_battery_on_mainboard.h
 *
 * Layer 2 module for the INA226 instance IC8 (battery side) on the EPS
 * PCU testing board V4.1. Knows the chip's I²C address and the shunt
 * resistor value soldered next to it; nothing else above it.
 *
 * BUILD TARGET: mainboard only.
 *
 * I²C address: 0x46 — verified 2026-05-15 from BAT.kicad_sch strap
 * trace. A1 wires to V+ and A0 wires to SDA, giving 0x46 per the INA226
 * datasheet address-select table. (Original guess was 0x41 which
 * corresponds to A1=GND, A0=V+ — wrong. See
 * research_logs/agent_M_ina226_placements_and_addresses.md.)
 *
 * Shunt resistor: 2 mΩ (R50 on the board, ROHM PMR25HZPJV2L0 ±5 %, per
 * testingPCU.csv BOM row 42).
 */

#ifndef SRC_DRIVERS_INA226_BATTERY_ON_MAINBOARD_H
#define SRC_DRIVERS_INA226_BATTERY_ON_MAINBOARD_H

#include <stdint.h>

/* Configures the chip: writes its CONFIGURATION and CALIBRATION registers
 * with values appropriate for the battery rail. Must be called once at
 * startup, after i2c_master_initialize(). Silently does nothing if the
 * chip does not respond — the read functions then return their stale
 * defaults (0). */
void battery_ina226_initialize(void);

/* Returns the latest bus voltage in millivolts. On bus error returns 0. */
uint16_t battery_ina226_read_voltage_mv(void);

/* Returns the latest current in milliamps, signed (positive = charging
 * direction). On bus error returns 0. */
int16_t battery_ina226_read_current_ma(void);

#endif /* SRC_DRIVERS_INA226_BATTERY_ON_MAINBOARD_H */
