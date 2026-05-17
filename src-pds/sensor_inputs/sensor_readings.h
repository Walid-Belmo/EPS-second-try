/*
 * sensor_readings.h
 *
 * Layer 1 (application) of the three-layer sensor abstraction. The state
 * machine and the MPPT algorithm only call functions declared in this
 * header. They never touch I²C, ADC, or any specific chip.
 *
 * Each read function returns one number in engineering units (mV or mA).
 * Where the number actually came from is decided inside this module by a
 * single source selector:
 *
 *   PDS_SENSOR_SOURCE_INJECTED            : value from the operator-injected
 *                                            inputs (the legacy default; what
 *                                            the State page demos rely on).
 *   PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE : value from the real chips on the
 *                                            mainboard (INA226 over I²C,
 *                                            LT6108/TPS25940 IMON via ADC,
 *                                            voltage dividers via ADC).
 *
 * On the dev board build the real-hardware branch silently falls back to
 * the injected value because no such chips exist. On the mainboard build
 * the real-hardware branch reads the actual hardware via Layer 2 modules.
 *
 * Redundancy (each current path has three independent measurements: INA226,
 * LT6108, TPS25940-IMON) is hidden inside this module. v1 picks INA226
 * unconditionally on mainboard. Adding a voting/fusion strategy later is
 * a one-function change inside read_battery_current() / read_panel_current().
 */

#ifndef SRC_PDS_SENSOR_INPUTS_SENSOR_READINGS_H
#define SRC_PDS_SENSOR_INPUTS_SENSOR_READINGS_H

#include <stdint.h>

#include "eps_state_machine.h"

/* The two sources of sensor values that Layer 1 can route reads through. */
typedef enum {
    PDS_SENSOR_SOURCE_INJECTED            = 0,
    PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE = 1
} pds_sensor_source_type;

/* Pick which source the five read_* functions below will use. The choice
 * applies to all five reads at once. Default after boot is INJECTED, so
 * the State page demos behave identically to the previous build until
 * the operator deliberately switches the source. */
void set_sensor_source(pds_sensor_source_type source);
pds_sensor_source_type get_current_sensor_source(void);

/* Per-measurement reads. One number, one trip. The state machine and MPPT
 * algorithm only call these. */
uint16_t read_battery_voltage(void);          /* mV  */
int16_t  read_battery_current(void);          /* mA, signed: + = charging */
uint16_t read_panel_voltage(void);            /* mV  */
uint16_t read_panel_current(void);            /* mA  */
uint16_t read_charging_rail_voltage(void);    /* mV  */

/* One-shot fill of the existing state-machine input struct. Internally
 * calls the five read_* functions above, plus copies the OBC-supplied
 * fields (heartbeat, satellite mode, safe sub-state, battery temperature)
 * straight from the injected struct because no real sensor exists for
 * those today. The MPPT raw-ADC fields are also synthesised from the
 * panel mV/mA values when the source is INJECTED. */
void read_all_state_machine_sensor_inputs(
    struct eps_sensor_readings_this_iteration *output);

#endif /* SRC_PDS_SENSOR_INPUTS_SENSOR_READINGS_H */
