/* =============================================================================
 * mainboard_adc_reader.c
 * Read-only SAMD21 ADC driver for EPS PCU testing board V4.1.
 *
 * BUILD TARGET: mainboard only
 *
 * Signals read:
 *   PB04 / AIN12 = PV_IMON
 *   PB05 / AIN13 = BAT_IMON
 *   PB06 / AIN14 = OUTA1
 *   PB07 / AIN15 = OUTA2
 *   PB08 / AIN2  = OUTV1
 *   PB09 / AIN3  = OUTV2
 *
 * This driver does not drive buck PWM, eFuse enables, heater, or power-switch
 * GPIOs. It only routes six package pins to peripheral function B and samples
 * them with the ADC.
 * =============================================================================
 */

#include <stdint.h>

#include "samd21j17d.h"
#include "assertion_handler.h"
#include "mainboard_adc_reader.h"

#define PORT_GROUP_INDEX_FOR_PORT_B                1u
#define PORT_MUX_FUNCTION_B_FOR_ANALOG             1u
#define MAXIMUM_ADC_WAIT_ITERATIONS           100000u
#define ADC_RESULT_MASK_12_BITS                  0x0FFFu

static uint8_t adc_reader_has_been_initialized;
static uint8_t most_recently_selected_adc_channel = 0xFFu;

static void enable_adc_bus_clock_for_register_access(void);
static void connect_48mhz_gclk0_to_adc_functional_clock(void);
static void route_mainboard_monitor_pins_to_adc_function_b(void);
static void route_two_adjacent_port_b_pins_to_function_b(uint8_t even_pin_number);
static void reset_adc_to_known_clean_state(void);
static void load_adc_factory_calibration_from_otp4_fuses(void);
static void configure_adc_for_single_ended_vddana_range_reads(void);
static void enable_adc_peripheral(void);
static void select_adc_channel(uint8_t adc_channel_number);
static uint16_t read_one_adc_conversion(void);
static uint8_t wait_until_adc_is_synchronized(void);
static uint8_t wait_until_adc_result_is_ready(void);

void mainboard_adc_reader_initialize(void)
{
    enable_adc_bus_clock_for_register_access();
    connect_48mhz_gclk0_to_adc_functional_clock();
    route_mainboard_monitor_pins_to_adc_function_b();
    reset_adc_to_known_clean_state();
    load_adc_factory_calibration_from_otp4_fuses();
    configure_adc_for_single_ended_vddana_range_reads();
    enable_adc_peripheral();

    adc_reader_has_been_initialized = 1u;
}

uint16_t mainboard_adc_reader_read_raw_channel(uint8_t adc_channel_number)
{
    SATELLITE_ASSERT(adc_reader_has_been_initialized == 1u);
    SATELLITE_ASSERT(adc_channel_number <= 19u);

    select_adc_channel(adc_channel_number);

    if (adc_channel_number != most_recently_selected_adc_channel)
    {
        (void)read_one_adc_conversion();
        most_recently_selected_adc_channel = adc_channel_number;
    }

    return read_one_adc_conversion();
}

void mainboard_adc_reader_read_all_channels(
    mainboard_adc_readings_type *readings_output)
{
    SATELLITE_ASSERT(readings_output != (void *)0);

    readings_output->pv_imon_raw_adc =
        mainboard_adc_reader_read_raw_channel(MAINBOARD_ADC_CHANNEL_PV_IMON);
    readings_output->bat_imon_raw_adc =
        mainboard_adc_reader_read_raw_channel(MAINBOARD_ADC_CHANNEL_BAT_IMON);
    readings_output->outa1_raw_adc =
        mainboard_adc_reader_read_raw_channel(MAINBOARD_ADC_CHANNEL_OUTA1);
    readings_output->outa2_raw_adc =
        mainboard_adc_reader_read_raw_channel(MAINBOARD_ADC_CHANNEL_OUTA2);
    readings_output->outv1_raw_adc =
        mainboard_adc_reader_read_raw_channel(MAINBOARD_ADC_CHANNEL_OUTV1);
    readings_output->outv2_raw_adc =
        mainboard_adc_reader_read_raw_channel(MAINBOARD_ADC_CHANNEL_OUTV2);
}

static void enable_adc_bus_clock_for_register_access(void)
{
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_ADC_Msk;
}

static void connect_48mhz_gclk0_to_adc_functional_clock(void)
{
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk |
        GCLK_CLKCTRL_GEN_GCLK0 |
        GCLK_CLKCTRL_ID(ADC_GCLK_ID);

    while ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for GCLK synchronization. */
    }
}

static void route_mainboard_monitor_pins_to_adc_function_b(void)
{
    route_two_adjacent_port_b_pins_to_function_b(4u);
    route_two_adjacent_port_b_pins_to_function_b(6u);
    route_two_adjacent_port_b_pins_to_function_b(8u);
}

static void route_two_adjacent_port_b_pins_to_function_b(uint8_t even_pin_number)
{
    uint8_t odd_pin_number = (uint8_t)(even_pin_number + 1u);
    uint8_t pmux_index = (uint8_t)(even_pin_number / 2u);

    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_B].PORT_PINCFG[even_pin_number] =
        PORT_PINCFG_PMUXEN_Msk;
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_B].PORT_PINCFG[odd_pin_number] =
        PORT_PINCFG_PMUXEN_Msk;

    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_B].PORT_PMUX[pmux_index] =
        (uint8_t)((PORT_MUX_FUNCTION_B_FOR_ANALOG << 4u)
                  | PORT_MUX_FUNCTION_B_FOR_ANALOG);
}

static void reset_adc_to_known_clean_state(void)
{
    ADC_REGS->ADC_CTRLA = ADC_CTRLA_SWRST_Msk;
    SATELLITE_ASSERT(wait_until_adc_is_synchronized() != 0u);
}

static void load_adc_factory_calibration_from_otp4_fuses(void)
{
    uint32_t otp4_word_0 = OTP4_FUSES_REGS->FUSES_OTP4_WORD_0;
    uint32_t otp4_word_1 = OTP4_FUSES_REGS->FUSES_OTP4_WORD_1;

    uint32_t linearity_calibration =
        ((otp4_word_0 & FUSES_OTP4_WORD_0_ADC_LINEARITY_0_Msk)
         >> FUSES_OTP4_WORD_0_ADC_LINEARITY_0_Pos)
        | (((otp4_word_1 & FUSES_OTP4_WORD_1_ADC_LINEARITY_1_Msk)
            >> FUSES_OTP4_WORD_1_ADC_LINEARITY_1_Pos)
           << 5u);

    uint32_t bias_calibration =
        (otp4_word_1 & FUSES_OTP4_WORD_1_ADC_BIASCAL_Msk)
        >> FUSES_OTP4_WORD_1_ADC_BIASCAL_Pos;

    ADC_REGS->ADC_CALIB =
        ADC_CALIB_LINEARITY_CAL(linearity_calibration)
        | ADC_CALIB_BIAS_CAL(bias_calibration);
}

static void configure_adc_for_single_ended_vddana_range_reads(void)
{
    ADC_REGS->ADC_REFCTRL = ADC_REFCTRL_REFSEL_INTVCC1;
    ADC_REGS->ADC_AVGCTRL = ADC_AVGCTRL_SAMPLENUM_1;
    ADC_REGS->ADC_SAMPCTRL = ADC_SAMPCTRL_SAMPLEN(15u);
    ADC_REGS->ADC_CTRLB =
        ADC_CTRLB_PRESCALER_DIV512 |
        ADC_CTRLB_RESSEL_12BIT;
    SATELLITE_ASSERT(wait_until_adc_is_synchronized() != 0u);

    ADC_REGS->ADC_INPUTCTRL =
        ADC_INPUTCTRL_MUXNEG_GND |
        ADC_INPUTCTRL_GAIN_DIV2 |
        ADC_INPUTCTRL_MUXPOS(MAINBOARD_ADC_CHANNEL_OUTV1);
    SATELLITE_ASSERT(wait_until_adc_is_synchronized() != 0u);
}

static void enable_adc_peripheral(void)
{
    ADC_REGS->ADC_CTRLA |= ADC_CTRLA_ENABLE_Msk;
    SATELLITE_ASSERT(wait_until_adc_is_synchronized() != 0u);
}

static void select_adc_channel(uint8_t adc_channel_number)
{
    ADC_REGS->ADC_INPUTCTRL =
        ADC_INPUTCTRL_MUXNEG_GND |
        ADC_INPUTCTRL_GAIN_DIV2 |
        ADC_INPUTCTRL_MUXPOS(adc_channel_number);
    SATELLITE_ASSERT(wait_until_adc_is_synchronized() != 0u);
}

static uint16_t read_one_adc_conversion(void)
{
    ADC_REGS->ADC_INTFLAG = ADC_INTFLAG_RESRDY_Msk;
    ADC_REGS->ADC_SWTRIG = ADC_SWTRIG_START_Msk;
    SATELLITE_ASSERT(wait_until_adc_is_synchronized() != 0u);
    SATELLITE_ASSERT(wait_until_adc_result_is_ready() != 0u);

    return (uint16_t)(ADC_REGS->ADC_RESULT & ADC_RESULT_MASK_12_BITS);
}

static uint8_t wait_until_adc_is_synchronized(void)
{
    for (uint32_t wait_count = 0u;
         wait_count < MAXIMUM_ADC_WAIT_ITERATIONS;
         wait_count += 1u)
    {
        if ((ADC_REGS->ADC_STATUS & ADC_STATUS_SYNCBUSY_Msk) == 0u)
        {
            return 1u;
        }
    }

    return 0u;
}

static uint8_t wait_until_adc_result_is_ready(void)
{
    for (uint32_t wait_count = 0u;
         wait_count < MAXIMUM_ADC_WAIT_ITERATIONS;
         wait_count += 1u)
    {
        if ((ADC_REGS->ADC_INTFLAG & ADC_INTFLAG_RESRDY_Msk) != 0u)
        {
            return 1u;
        }
    }

    return 0u;
}
