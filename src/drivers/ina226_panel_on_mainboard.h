/*
 * ina226_panel_on_mainboard.h
 *
 * Layer 2 module for the INA226 instance IC5 (solar-panel side) on the
 * EPS PCU testing board V4.1.
 *
 * BUILD TARGET: mainboard only.
 *
 * I²C address: 0x45 — verified 2026-05-15 from PV.kicad_sch strap
 * trace. Both A0 and A1 strap pins wire to V+, giving 0x45 per the
 * INA226 datasheet address-select table. (Original guess was 0x40
 * which corresponds to A0=GND, A1=GND — wrong. See
 * research_logs/agent_M_ina226_placements_and_addresses.md.)
 *
 * Shunt resistor: 2 mΩ (R49 on the board, ROHM PMR25HZPJV2L0 ±5 %).
 */

#ifndef SRC_DRIVERS_INA226_PANEL_ON_MAINBOARD_H
#define SRC_DRIVERS_INA226_PANEL_ON_MAINBOARD_H

#include <stdint.h>

void panel_ina226_initialize(void);
uint16_t panel_ina226_read_voltage_mv(void);
int16_t  panel_ina226_read_current_ma(void);

#endif /* SRC_DRIVERS_INA226_PANEL_ON_MAINBOARD_H */
