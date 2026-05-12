/* =============================================================================
 * pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c
 * Mainboard TCC0 buck PWM driver for EPS PCU testing board V4.1.
 *
 * BUILD TARGET: mainboard only
 *
 * The PCB routes:
 *   PA12 = PWM_H = TCC0 WO[6], mux F
 *   PA13 = PWM_L = TCC0 WO[7], mux F
 *
 * These pins are not one natural TCC0 DTI pair. The workaround keeps a single
 * TCC0 counter and uses two DTI slices:
 *   DTI2: CC[2] -> WO[2]/WO[6], with SWAP2 so WO[6] gets the LS-shaped output
 *   DTI3: CC[3] -> WO[3]/WO[7], unswapped so WO[7] gets the HS-shaped output
 *
 * CC[2] and CC[3] are always updated together with the same compare value.
 * =============================================================================
 */

#include <stdint.h>

#include "samd21j17d.h"
#include "assertion_handler.h"
#include "pwm_buck_converter.h"

#define PORT_GROUP_INDEX_FOR_PORT_A                    0u
#define MAINBOARD_PWM_HIGH_SIDE_PIN_NUMBER            12u
#define MAINBOARD_PWM_LOW_SIDE_PIN_NUMBER             13u
#define MAINBOARD_PWM_PA12_PA13_PMUX_INDEX             6u
#define PORT_MUX_FUNCTION_F_FOR_TCC0                   5u

#define TCC0_PERIOD_FOR_300KHZ                       159u
#define TCC0_DEAD_TIME_LOW_SIDE_IN_GCLK_COUNTS        6u
#define TCC0_DEAD_TIME_HIGH_SIDE_IN_GCLK_COUNTS       6u
#define BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE           8u
#define BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE         151u
#define DUTY_CYCLE_INPUT_FULL_SCALE               65535u

static uint8_t pwm_has_been_initialized;
static uint8_t pwm_outputs_are_routed_to_tcc0;

static void enable_tcc0_bus_clock_on_apbc(void);
static void connect_48mhz_gclk0_to_tcc0_peripheral_clock(void);
static void route_pa12_as_tcc0_wo6_and_pa13_as_tcc0_wo7(void);
static void force_pa12_and_pa13_low_as_gpio_outputs(void);
static void reset_tcc0_to_known_clean_state(void);
static void configure_tcc0_waveform_generation_with_swap2(void);
static void configure_tcc0_dead_time_insertion_on_slices_2_and_3(void);
static void configure_tcc0_fault_safe_output_levels(void);
static void set_tcc0_period_for_300khz_switching_frequency(void);
static void set_tcc0_initial_duty_cycle_to_zero(void);
static void enable_tcc0_pwm_output(void);
static uint32_t convert_fractional_duty_cycle_to_compare_value(
    uint16_t duty_cycle_as_fraction_of_65535);
static void write_compare_value_to_channels_2_and_3(uint32_t compare_value);
static void write_buffered_compare_value_to_channels_2_and_3(
    uint32_t compare_value);
static void force_tcc0_buffered_register_update(void);

void pwm_buck_converter_initialize_for_demo(void)
{
    force_pa12_and_pa13_low_as_gpio_outputs();
    enable_tcc0_bus_clock_on_apbc();
    connect_48mhz_gclk0_to_tcc0_peripheral_clock();
    reset_tcc0_to_known_clean_state();
    configure_tcc0_waveform_generation_with_swap2();
    configure_tcc0_dead_time_insertion_on_slices_2_and_3();
    configure_tcc0_fault_safe_output_levels();
    set_tcc0_period_for_300khz_switching_frequency();
    set_tcc0_initial_duty_cycle_to_zero();
    enable_tcc0_pwm_output();

    pwm_has_been_initialized = 1u;
    force_pa12_and_pa13_low_as_gpio_outputs();
}

void pwm_buck_converter_set_duty_cycle(
    uint16_t duty_cycle_as_fraction_of_65535)
{
    SATELLITE_ASSERT(pwm_has_been_initialized == 1u);

    if (duty_cycle_as_fraction_of_65535 == 0u)
    {
        force_pa12_and_pa13_low_as_gpio_outputs();
        write_buffered_compare_value_to_channels_2_and_3(0u);
        force_tcc0_buffered_register_update();
        return;
    }

    uint8_t outputs_need_to_be_routed_after_compare_update =
        (pwm_outputs_are_routed_to_tcc0 == 0u) ? 1u : 0u;

    uint32_t compare_value_for_tcc0 =
        convert_fractional_duty_cycle_to_compare_value(
            duty_cycle_as_fraction_of_65535);

    SATELLITE_ASSERT(
        (compare_value_for_tcc0 >= BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE)
        && (compare_value_for_tcc0 <= BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE));

    write_buffered_compare_value_to_channels_2_and_3(compare_value_for_tcc0);

    if (outputs_need_to_be_routed_after_compare_update != 0u)
    {
        force_tcc0_buffered_register_update();
        route_pa12_as_tcc0_wo6_and_pa13_as_tcc0_wo7();
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

static void route_pa12_as_tcc0_wo6_and_pa13_as_tcc0_wo7(void)
{
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_A].
        PORT_PINCFG[MAINBOARD_PWM_HIGH_SIDE_PIN_NUMBER] =
            PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_A].
        PORT_PINCFG[MAINBOARD_PWM_LOW_SIDE_PIN_NUMBER] =
            PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;

    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_A].
        PORT_PMUX[MAINBOARD_PWM_PA12_PA13_PMUX_INDEX] =
            (uint8_t)((PORT_MUX_FUNCTION_F_FOR_TCC0 << 4u)
                      | PORT_MUX_FUNCTION_F_FOR_TCC0);

    pwm_outputs_are_routed_to_tcc0 = 1u;
}

static void force_pa12_and_pa13_low_as_gpio_outputs(void)
{
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_A].
        PORT_PINCFG[MAINBOARD_PWM_HIGH_SIDE_PIN_NUMBER] = 0u;
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_A].
        PORT_PINCFG[MAINBOARD_PWM_LOW_SIDE_PIN_NUMBER] = 0u;
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_A].PORT_OUTCLR =
        (1u << MAINBOARD_PWM_HIGH_SIDE_PIN_NUMBER)
        | (1u << MAINBOARD_PWM_LOW_SIDE_PIN_NUMBER);
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_A].PORT_DIRSET =
        (1u << MAINBOARD_PWM_HIGH_SIDE_PIN_NUMBER)
        | (1u << MAINBOARD_PWM_LOW_SIDE_PIN_NUMBER);

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

static void configure_tcc0_waveform_generation_with_swap2(void)
{
    TCC0_REGS->TCC_WAVE =
        TCC_WAVE_WAVEGEN_NPWM |
        TCC_WAVE_SWAP2_Msk;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_WAVE_Msk) != 0u)
    {
        /* Wait for WAVE synchronization. */
    }
}

static void configure_tcc0_dead_time_insertion_on_slices_2_and_3(void)
{
    TCC0_REGS->TCC_WEXCTRL =
        TCC_WEXCTRL_DTIEN2_Msk |
        TCC_WEXCTRL_DTIEN3_Msk |
        TCC_WEXCTRL_OTMX(0u) |
        TCC_WEXCTRL_DTLS(TCC0_DEAD_TIME_LOW_SIDE_IN_GCLK_COUNTS) |
        TCC_WEXCTRL_DTHS(TCC0_DEAD_TIME_HIGH_SIDE_IN_GCLK_COUNTS);
}

static void configure_tcc0_fault_safe_output_levels(void)
{
    TCC0_REGS->TCC_DRVCTRL =
        TCC_DRVCTRL_NRE6_Msk |
        TCC_DRVCTRL_NRE7_Msk;
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
    write_compare_value_to_channels_2_and_3(0u);
}

static void enable_tcc0_pwm_output(void)
{
    TCC0_REGS->TCC_CTRLA |= TCC_CTRLA_ENABLE_Msk;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_ENABLE_Msk) != 0u)
    {
        /* Wait for TCC0 enable. */
    }
}

static uint32_t convert_fractional_duty_cycle_to_compare_value(
    uint16_t duty_cycle_as_fraction_of_65535)
{
    uint32_t compare_value_for_tcc0 =
        ((uint32_t)duty_cycle_as_fraction_of_65535
         * (uint32_t)TCC0_PERIOD_FOR_300KHZ)
        / (uint32_t)DUTY_CYCLE_INPUT_FULL_SCALE;

    if (compare_value_for_tcc0 < BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE)
    {
        compare_value_for_tcc0 = BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE;
    }

    if (compare_value_for_tcc0 > BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE)
    {
        compare_value_for_tcc0 = BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE;
    }

    return compare_value_for_tcc0;
}

static void write_compare_value_to_channels_2_and_3(uint32_t compare_value)
{
    TCC0_REGS->TCC_CC[2] = compare_value;
    TCC0_REGS->TCC_CC[3] = compare_value;
    while ((TCC0_REGS->TCC_SYNCBUSY
            & (TCC_SYNCBUSY_CC2_Msk | TCC_SYNCBUSY_CC3_Msk)) != 0u)
    {
        /* Wait for compare synchronization. */
    }
}

static void write_buffered_compare_value_to_channels_2_and_3(
    uint32_t compare_value)
{
    TCC0_REGS->TCC_CCB[2] = compare_value;
    TCC0_REGS->TCC_CCB[3] = compare_value;
    while ((TCC0_REGS->TCC_SYNCBUSY
            & (TCC_SYNCBUSY_CCB2_Msk | TCC_SYNCBUSY_CCB3_Msk)) != 0u)
    {
        /* Wait for compare-buffer synchronization. */
    }
}

static void force_tcc0_buffered_register_update(void)
{
    TCC0_REGS->TCC_CTRLBSET = TCC_CTRLBSET_CMD_UPDATE;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_CTRLB_Msk) != 0u)
    {
        /* Wait for forced update command synchronization. */
    }
}
