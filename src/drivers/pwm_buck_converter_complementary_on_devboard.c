/* =============================================================================
 * pwm_buck_converter_complementary_on_devboard.c
 * Dev-board TCC0 complementary PWM demo output on PA18/PA20.
 *
 * BUILD TARGET: devboard only
 *
 * Generates 300 kHz complementary PWM with TCC0 dead-time insertion:
 *   PA18 = TCC0 WO[2], mux F
 *   PA20 = TCC0 WO[6], mux F
 * =============================================================================
 */

#include <stdint.h>

#include "sam.h"
#include "assertion_handler.h"
#include "debug_functions.h"
#include "pwm_buck_converter.h"

#define TCC0_PERIOD_FOR_300KHZ                         159u
#define TCC0_DEAD_TIME_LOW_SIDE_IN_GCLK_COUNTS          2u
#define TCC0_DEAD_TIME_HIGH_SIDE_IN_GCLK_COUNTS         2u
#define BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE             8u
#define BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE           151u
#define DUTY_CYCLE_INPUT_FULL_SCALE                 65535u

static uint8_t pwm_has_been_initialized;
static uint8_t pwm_outputs_are_routed_to_tcc0;

static void enable_tcc0_bus_clock_on_apbc(void);
static void connect_48mhz_gclk0_to_tcc0_peripheral_clock(void);
static void route_pa18_as_tcc0_wo2_and_pa20_as_tcc0_wo6(void);
static void force_pa18_and_pa20_low_as_gpio_outputs(void);
static void reset_tcc0_to_known_clean_state(void);
static void configure_tcc0_waveform_generation_as_normal_pwm(void);
static void configure_tcc0_dead_time_insertion_on_channel_2(void);
static void configure_tcc0_fault_safe_output_levels(void);
static void set_tcc0_period_for_300khz_switching_frequency(void);
static void set_tcc0_initial_duty_cycle_to_zero(void);
static void enable_tcc0_pwm_output(void);

void pwm_buck_converter_initialize_for_demo(void)
{
    enable_tcc0_bus_clock_on_apbc();
    connect_48mhz_gclk0_to_tcc0_peripheral_clock();
    route_pa18_as_tcc0_wo2_and_pa20_as_tcc0_wo6();
    reset_tcc0_to_known_clean_state();
    configure_tcc0_waveform_generation_as_normal_pwm();
    configure_tcc0_dead_time_insertion_on_channel_2();
    configure_tcc0_fault_safe_output_levels();
    set_tcc0_period_for_300khz_switching_frequency();
    set_tcc0_initial_duty_cycle_to_zero();
    enable_tcc0_pwm_output();

    pwm_has_been_initialized = 1u;
    force_pa18_and_pa20_low_as_gpio_outputs();
    DEBUG_LOG_TEXT("PWM demo: TCC0 PA18/PA20 300kHz");
}

void pwm_buck_converter_set_duty_cycle(
    uint16_t duty_cycle_as_fraction_of_65535)
{
    SATELLITE_ASSERT(pwm_has_been_initialized == 1u);

    if (duty_cycle_as_fraction_of_65535 == 0u)
    {
        force_pa18_and_pa20_low_as_gpio_outputs();
        return;
    }

    uint8_t outputs_need_to_be_routed_after_compare_update =
        (pwm_outputs_are_routed_to_tcc0 == 0u) ? 1u : 0u;

    uint32_t compare_value_for_tcc0 =
        ((uint32_t)duty_cycle_as_fraction_of_65535
         * (uint32_t)TCC0_PERIOD_FOR_300KHZ)
        / (uint32_t)DUTY_CYCLE_INPUT_FULL_SCALE;

    if (compare_value_for_tcc0 > 0u)
    {
        if (compare_value_for_tcc0 < BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE)
        {
            compare_value_for_tcc0 = BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE;
        }
        if (compare_value_for_tcc0 > BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE)
        {
            compare_value_for_tcc0 = BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE;
        }
    }

    SATELLITE_ASSERT(
        (compare_value_for_tcc0 == 0u)
        || ((compare_value_for_tcc0 >= BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE)
            && (compare_value_for_tcc0 <= BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE)));

    TCC0_REGS->TCC_CCB[2] = compare_value_for_tcc0;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_CCB2_Msk) != 0u)
    {
        /* Wait for compare-buffer synchronization. */
    }

    if (outputs_need_to_be_routed_after_compare_update != 0u)
    {
        route_pa18_as_tcc0_wo2_and_pa20_as_tcc0_wo6();
    }
}

static void enable_tcc0_bus_clock_on_apbc(void)
{
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_TCC0_Msk;
}

static void connect_48mhz_gclk0_to_tcc0_peripheral_clock(void)
{
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk |
        GCLK_CLKCTRL_GEN_GCLK0 |
        GCLK_CLKCTRL_ID_TCC0_TCC1;

    while ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for GCLK synchronization. */
    }
}

static void route_pa18_as_tcc0_wo2_and_pa20_as_tcc0_wo6(void)
{
    PORT_REGS->GROUP[0].PORT_PINCFG[18] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;
    PORT_REGS->GROUP[0].PORT_PMUX[9] =
        (PORT_REGS->GROUP[0].PORT_PMUX[9] & 0xF0u)
        | PORT_PMUX_PMUXE_F_Val;

    PORT_REGS->GROUP[0].PORT_PINCFG[20] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;
    PORT_REGS->GROUP[0].PORT_PMUX[10] =
        (PORT_REGS->GROUP[0].PORT_PMUX[10] & 0xF0u)
        | PORT_PMUX_PMUXE_F_Val;

    pwm_outputs_are_routed_to_tcc0 = 1u;
}

static void force_pa18_and_pa20_low_as_gpio_outputs(void)
{
    TCC0_REGS->TCC_CCB[2] = 0u;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_CCB2_Msk) != 0u)
    {
        /* Wait for compare-buffer synchronization before disconnecting pins. */
    }

    PORT_REGS->GROUP[0].PORT_PINCFG[18] = 0u;
    PORT_REGS->GROUP[0].PORT_PINCFG[20] = 0u;
    PORT_REGS->GROUP[0].PORT_OUTCLR = (1u << 18u) | (1u << 20u);
    PORT_REGS->GROUP[0].PORT_DIRSET = (1u << 18u) | (1u << 20u);

    pwm_outputs_are_routed_to_tcc0 = 0u;
}

static void reset_tcc0_to_known_clean_state(void)
{
    TCC0_REGS->TCC_CTRLA = TCC_CTRLA_SWRST_Msk;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_SWRST_Msk) != 0u)
    {
        /* Wait for TCC0 reset. */
    }
}

static void configure_tcc0_waveform_generation_as_normal_pwm(void)
{
    TCC0_REGS->TCC_WAVE = TCC_WAVE_WAVEGEN_NPWM;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_WAVE_Msk) != 0u)
    {
        /* Wait for WAVE synchronization. */
    }
}

static void configure_tcc0_dead_time_insertion_on_channel_2(void)
{
    TCC0_REGS->TCC_WEXCTRL =
        TCC_WEXCTRL_DTIEN2_Msk |
        TCC_WEXCTRL_OTMX(0u) |
        TCC_WEXCTRL_DTLS(TCC0_DEAD_TIME_LOW_SIDE_IN_GCLK_COUNTS) |
        TCC_WEXCTRL_DTHS(TCC0_DEAD_TIME_HIGH_SIDE_IN_GCLK_COUNTS);
}

static void configure_tcc0_fault_safe_output_levels(void)
{
    TCC0_REGS->TCC_DRVCTRL =
        TCC_DRVCTRL_NRE2_Msk |
        TCC_DRVCTRL_NRE6_Msk;
}

static void set_tcc0_period_for_300khz_switching_frequency(void)
{
    TCC0_REGS->TCC_PER = TCC0_PERIOD_FOR_300KHZ;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_PER_Msk) != 0u)
    {
        /* Wait for PER synchronization. */
    }
}

static void set_tcc0_initial_duty_cycle_to_zero(void)
{
    TCC0_REGS->TCC_CC[2] = 0u;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_CC2_Msk) != 0u)
    {
        /* Wait for CC synchronization. */
    }
}

static void enable_tcc0_pwm_output(void)
{
    TCC0_REGS->TCC_CTRLA |= TCC_CTRLA_ENABLE_Msk;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_ENABLE_Msk) != 0u)
    {
        /* Wait for TCC0 enable. */
    }
}
