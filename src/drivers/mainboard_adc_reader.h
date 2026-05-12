/* =============================================================================
 * mainboard_adc_reader.h
 * Read-only ADC driver for EPS PCU testing board V4.1 analog monitor nets.
 *
 * BUILD TARGET: mainboard only
 * =============================================================================
 */

#ifndef MAINBOARD_ADC_READER_H
#define MAINBOARD_ADC_READER_H

#include <stdint.h>

#define MAINBOARD_ADC_CHANNEL_PV_IMON    12u
#define MAINBOARD_ADC_CHANNEL_BAT_IMON   13u
#define MAINBOARD_ADC_CHANNEL_OUTA1      14u
#define MAINBOARD_ADC_CHANNEL_OUTA2      15u
#define MAINBOARD_ADC_CHANNEL_OUTV1       2u
#define MAINBOARD_ADC_CHANNEL_OUTV2       3u

#define MAINBOARD_ADC_TELEMETRY_FLAG_READINGS_PRESENT       0x01u
#define MAINBOARD_ADC_TELEMETRY_FLAG_CONVERSIONS_NOMINAL    0x02u
#define MAINBOARD_ADC_TELEMETRY_FLAG_OUTA_SCALING_PROVISIONAL 0x04u
#define MAINBOARD_ADC_TELEMETRY_FLAG_OUTV_SCALING_PROVISIONAL 0x08u

typedef struct {
    uint16_t pv_imon_raw_adc;
    uint16_t bat_imon_raw_adc;
    uint16_t outa1_raw_adc;
    uint16_t outa2_raw_adc;
    uint16_t outv1_raw_adc;
    uint16_t outv2_raw_adc;
} mainboard_adc_readings_type;

typedef struct {
    mainboard_adc_readings_type raw;

    uint16_t pv_imon_pin_mv;
    uint16_t bat_imon_pin_mv;
    uint16_t outa1_pin_mv;
    uint16_t outa2_pin_mv;
    uint16_t outv1_pin_mv;
    uint16_t outv2_pin_mv;

    uint32_t pv_imon_ma;
    uint32_t bat_imon_ma;
    uint32_t outa1_ma;
    uint32_t outa2_ma;
    uint32_t outv1_mv;
    uint32_t outv2_mv;

    uint8_t scaling_flags;
} mainboard_adc_telemetry_type;

void mainboard_adc_reader_initialize(void);

uint16_t mainboard_adc_reader_read_raw_channel(uint8_t adc_channel_number);

void mainboard_adc_reader_read_all_channels(
    mainboard_adc_readings_type *readings_output);

void mainboard_adc_reader_convert_readings_to_telemetry(
    const mainboard_adc_readings_type *readings_input,
    mainboard_adc_telemetry_type *telemetry_output);

void mainboard_adc_reader_read_telemetry(
    mainboard_adc_telemetry_type *telemetry_output);

#endif /* MAINBOARD_ADC_READER_H */
