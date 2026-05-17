/*
 * ina226_register_protocol.h
 *
 * Pure-logic helpers that know the INA226 register map and the math the
 * datasheet specifies, but know NOTHING about I²C, the SAMD21, or any
 * hardware. Both INA226 device modules (battery side and panel side) call
 * these to encode register values to write and decode register values
 * they have read back.
 *
 * Keeping the math in its own file means it compiles on every build target
 * (dev board too), can be exercised by a built-in self test on boot, and
 * can be unit-tested on a laptop without a chip.
 *
 * Reference: TI INA226 datasheet
 * https://www.ti.com/lit/gpn/INA226 (rev May 2018).
 *
 *   Register     Address  LSB         Notes
 *   ----------   -------  ----------  -------------------------
 *   CONFIG       0x00     -           averaging, conv. time, mode
 *   SHUNT_V      0x01     2.5  µV     signed
 *   BUS_V        0x02     1.25 mV     unsigned, range 0..40.96 V
 *   POWER        0x03     -           = current_LSB × 25 (W)
 *   CURRENT      0x04     current_LSB signed
 *   CALIBRATION  0x05     -           CAL = 0.00512 / (current_LSB × R)
 *   MASK_ENABLE  0x06     -           alert config
 *   ALERT_LIMIT  0x07     -           alert threshold
 *   MFG_ID       0xFE     -           always reads 0x5449 ("TI")
 *   DIE_ID       0xFF     -           always reads 0x2260
 */

#ifndef SRC_PDS_SENSOR_INPUTS_INA226_REGISTER_PROTOCOL_H
#define SRC_PDS_SENSOR_INPUTS_INA226_REGISTER_PROTOCOL_H

#include <stdint.h>

/* INA226 register addresses. */
#define INA226_REGISTER_CONFIGURATION   0x00u
#define INA226_REGISTER_SHUNT_VOLTAGE   0x01u
#define INA226_REGISTER_BUS_VOLTAGE     0x02u
#define INA226_REGISTER_POWER           0x03u
#define INA226_REGISTER_CURRENT         0x04u
#define INA226_REGISTER_CALIBRATION     0x05u
#define INA226_REGISTER_MASK_ENABLE     0x06u
#define INA226_REGISTER_ALERT_LIMIT     0x07u
#define INA226_REGISTER_MANUFACTURER_ID 0xFEu
#define INA226_REGISTER_DIE_ID          0xFFu

/* Constants the chip is hard-coded to return. Used by the device module
 * to confirm the chip on the bus is actually an INA226 before trusting it. */
#define INA226_EXPECTED_MANUFACTURER_ID 0x5449u   /* ASCII "TI" */
#define INA226_EXPECTED_DIE_ID          0x2260u

/* ----------------------------------------------------------------------
 * Decoders. Take a raw register value the I²C driver read, return an
 * engineering value.
 * ---------------------------------------------------------------------- */

/* Shunt voltage register: signed 16-bit, LSB = 2.5 µV.
 * Output is signed microvolts, fits comfortably in int32_t even at full
 * scale (2.5 µV × 32767 = 81.9 mV = 81920 µV). */
int32_t decode_ina226_shunt_voltage_register_to_microvolts(uint16_t reg);

/* Bus voltage register: unsigned 15-bit (top bit reserved 0), LSB = 1.25 mV.
 * Returns millivolts in 0..40960 range. */
uint16_t decode_ina226_bus_voltage_register_to_millivolts(uint16_t reg);

/* Current register: signed 16-bit, LSB = current_LSB (the calibration the
 * caller chose at configure time). The caller passes that current_LSB
 * back here in microamps so the math is integer.
 *
 * Example: if current_LSB was chosen as 152 µA per bit, pass 152. */
int32_t decode_ina226_current_register_to_milliamps(
    uint16_t reg,
    uint32_t current_lsb_in_microamps);

/* ----------------------------------------------------------------------
 * Encoders / pickers. Compute the value the caller should write to the
 * given register at configure time.
 * ---------------------------------------------------------------------- */

/* The CALIBRATION value the chip needs so that its internal CURRENT
 * register expresses values in the chosen current_LSB.
 *
 * Inputs in integer engineering units to keep the API obvious:
 *   expected_max_current_in_milliamps : highest current the rail will see
 *                                       in normal operation (e.g. 5000 for 5 A)
 *   shunt_resistance_in_microohms     : the R_shunt soldered on the board
 *                                       (e.g. 2000 for 2 mΩ)
 *
 * Returns the 16-bit value to load into INA226 register 0x05. The caller
 * is also responsible for remembering the current_LSB they implicitly
 * chose, because they will need it again to decode register 0x04. The
 * companion function compute_ina226_current_lsb_in_microamps() returns
 * that value so caller and chip stay in sync. */
uint16_t compute_ina226_calibration_constant(
    uint32_t expected_max_current_in_milliamps,
    uint32_t shunt_resistance_in_microohms);

/* The current_LSB the calibration above implicitly chose. Caller stores
 * this and passes it back to decode_ina226_current_register_to_milliamps()
 * every time it reads the CURRENT register. */
uint32_t compute_ina226_current_lsb_in_microamps(
    uint32_t expected_max_current_in_milliamps);

/* A reasonable default value to write into INA226's CONFIGURATION register
 * (0x00). Picks 16-sample averaging, 1.1 ms conversion times for both
 * shunt and bus, and continuous-conversion mode on both channels. The
 * exact bit pattern is documented in the .c file. */
uint16_t default_ina226_configuration_register_value(void);

/* ----------------------------------------------------------------------
 * Built-in self test
 * ---------------------------------------------------------------------- */

/* Runs the encode/decode functions against known input/output pairs from
 * the datasheet examples. Returns 1 if all checks passed, 0 if any check
 * failed. Caller is expected to log the result on boot. */
uint8_t ina226_register_protocol_built_in_self_test(void);

#endif /* SRC_PDS_SENSOR_INPUTS_INA226_REGISTER_PROTOCOL_H */
