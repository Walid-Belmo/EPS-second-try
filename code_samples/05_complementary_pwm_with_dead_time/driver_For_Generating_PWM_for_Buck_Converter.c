/* =============================================================================
 * driver_For_Generating_PWM_for_Buck_Converter.c
 * Configures TCC0 for complementary PWM at 300 kHz with hardware dead-time
 * insertion (DTI) to drive the EPC2152 GaN half-bridge buck converter.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: TCC0 (Timer/Counter for Control applications, instance 0)
 * Pins: PA18 (TCC0 WO[2], mux F) = high-side drive
 *        PA20 (TCC0 WO[6], mux F) = low-side drive (complementary)
 * Clock: GCLK0 (48 MHz) → TCC0 via GCLK channel 26 (shared with TCC1)
 *
 * Dead-time insertion:
 *   DTIEN2 enables DTI on compare channel 2.
 *   WO[2] = non-inverted (LS), WO[6] = inverted (HS).
 *   DTLS = DTHS = 2 counts = ~42 ns at 48 MHz.
 *   EPC2152 requires minimum ~21 ns. 42 ns provides 2x safety margin.
 *
 * Datasheet references:
 *   SAMD21 DS40001882H Chapter 31 (TCC), pages 616-690
 *   DTI block diagram: Figure 31-34, page 644
 *   DTI timing: Figure 31-35, page 644
 *   WEXCTRL register: page 650 (offset 0x14)
 *   NPWM mode: Section 31.6.2.5.4, page 625
 *   Frequency formula: f = f_GCLK / (N × (PER + 1)), page 626
 * =============================================================================
 */

#include <stdint.h>

#include "samd21g17d.h"

#include "assertion_handler.h"
#include "driver_For_Generating_PWM_for_Buck_Converter.h"
#include "debug_functions.h"

/* ── Compile-time constants ─────────────────────────────────────────────── */

/* PER register value for 300 kHz switching frequency.
 * f_PWM = f_GCLK / (prescaler × (PER + 1))
 * 300,000 = 48,000,000 / (1 × (159 + 1))
 * The counter counts 0, 1, 2, ... 159 then wraps — 160 ticks per cycle. */
#define TCC0_PERIOD_FOR_300KHZ  159u

/* Dead-time values in GCLK cycles (48 MHz → 20.83 ns per count).
 * DTLS = delay on low-side (WO[2]) rising edge.
 * DTHS = delay on high-side (WO[6]) rising edge.
 * 2 counts × 20.83 ns = ~42 ns dead time.
 * EPC2152 engineer recommendation: start at 21 ns, minimum ~10 ns. */
#define TCC0_DEAD_TIME_LOW_SIDE_IN_GCLK_COUNTS    2u
#define TCC0_DEAD_TIME_HIGH_SIDE_IN_GCLK_COUNTS   2u

/* Safe duty cycle operating limits (as CC[2] register values, range 0-159).
 * Below 5%: switching pulses too narrow for clean operation.
 * Above 94%: high-side FET never fully turns off, risks inductor saturation. */
#define BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE    8u    /* ~5% of 160 */
#define BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE  151u    /* ~94% of 160 */

/* Full-scale input value from MPPT algorithm. */
#define DUTY_CYCLE_INPUT_FULL_SCALE  65535u

/* ── Private function prototypes ────────────────────────────────────────── */

static void enable_tcc0_bus_clock_on_apbc(void);
static void connect_48mhz_gclk0_to_tcc0_peripheral_clock(void);
static void configure_pa18_as_tcc0_wo2_and_pa20_as_tcc0_wo6(void);
static void reset_tcc0_to_known_clean_state(void);
static void configure_tcc0_waveform_generation_as_normal_pwm(void);
static void configure_tcc0_dead_time_insertion_on_channel_2(void);
static void configure_tcc0_fault_safe_output_levels(void);
static void set_tcc0_period_for_300khz_switching_frequency(void);
static void set_tcc0_initial_duty_cycle_to_zero(void);
static void enable_tcc0_pwm_output(void);

/* ── Public functions ───────────────────────────────────────────────────── */

void pwm_initialize_tcc0_complementary_300khz_with_dead_time(void)
{
    enable_tcc0_bus_clock_on_apbc();
    connect_48mhz_gclk0_to_tcc0_peripheral_clock();
    configure_pa18_as_tcc0_wo2_and_pa20_as_tcc0_wo6();
    reset_tcc0_to_known_clean_state();
    configure_tcc0_waveform_generation_as_normal_pwm();
    configure_tcc0_dead_time_insertion_on_channel_2();
    configure_tcc0_fault_safe_output_levels();
    set_tcc0_period_for_300khz_switching_frequency();
    set_tcc0_initial_duty_cycle_to_zero();
    enable_tcc0_pwm_output();

    DEBUG_LOG_TEXT("TCC0 PWM: 300kHz complementary, DTI=2/2 (~42ns)");
}

void pwm_set_buck_converter_duty_cycle(
    uint16_t duty_cycle_as_fraction_of_65535)
{
    /* Map from the MPPT output range (0-65535) to the TCC0 compare range
     * (0-159). This is a simple proportion: 32768 → ~80, 65535 → 159.
     * The cast to uint32_t prevents overflow: 65535 × 159 = 10,420,065
     * which fits in 32 bits but not 16 bits. */
    uint32_t compare_value_for_tcc0 =
        ((uint32_t)duty_cycle_as_fraction_of_65535
         * (uint32_t)TCC0_PERIOD_FOR_300KHZ)
        / (uint32_t)DUTY_CYCLE_INPUT_FULL_SCALE;

    /* Clamp to safe operating range, unless fully off.
     * A duty of 0 means "converter off" — no clamping needed.
     * Any non-zero duty is clamped to [MIN..MAX] to protect the hardware. */
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

    /* Postcondition: value is either 0 or within the safe range. */
    SATELLITE_ASSERT(
        (compare_value_for_tcc0 == 0u)
        || ((compare_value_for_tcc0 >= BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE)
            && (compare_value_for_tcc0 <= BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE)));

    /* Write to the BUFFER register (CCB[2]), not the direct register (CC[2]).
     * The hardware copies CCB → CC at the next counter wrap (UPDATE event).
     * This prevents changing the duty cycle mid-PWM-cycle. */
    TCC0_REGS->TCC_CCB[2] = compare_value_for_tcc0;

    /* CCB write crosses a clock domain. Without this wait, a second call
     * before synchronization completes would silently lose data. */
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_CCB2_Msk) != 0u)
    {
        /* Wait for compare buffer write to synchronize */
    }

    DEBUG_LOG_UINT("PWM CC[2] set to", compare_value_for_tcc0);
}

/* ── Private functions ──────────────────────────────────────────────────── */

static void enable_tcc0_bus_clock_on_apbc(void)
{
    /* The SAMD21 keeps peripherals powered off by default to save energy.
     * TCC0 sits on the APB-C bus. Until this bit is set, any write to
     * TCC0's registers is silently ignored — the hardware is unpowered
     * and cannot latch data from the bus. */
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_TCC0_Msk;
}

static void connect_48mhz_gclk0_to_tcc0_peripheral_clock(void)
{
    /* TCC0 needs two clocks to function:
     * 1. The bus clock (enabled above) lets the CPU read/write registers.
     * 2. The peripheral clock drives the counter and dead-time logic.
     *
     * This connects GCLK0 (48 MHz from DFLL48M) to TCC0's peripheral
     * clock input. TCC0 and TCC1 share the same GCLK channel
     * (GCLK_CLKCTRL_ID_TCC0_TCC1, channel 26).
     *
     * Without this, the counter does not increment and no PWM is generated,
     * even though the registers appear to be configured correctly. */
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk         |   /* enable this clock connection */
        GCLK_CLKCTRL_GEN_GCLK0         |   /* source: GCLK0 = 48 MHz */
        GCLK_CLKCTRL_ID_TCC0_TCC1;         /* destination: TCC0 (and TCC1) */

    /* This write crosses a clock domain. Writing TCC0 registers before
     * synchronization completes produces undefined behaviour. */
    while ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for GCLK synchronization to complete */
    }
}

static void configure_pa18_as_tcc0_wo2_and_pa20_as_tcc0_wo6(void)
{
    /* By default, every pin is a GPIO. To connect PA18 and PA20 to
     * TCC0's waveform outputs, we must:
     * 1. Enable the peripheral multiplexer on each pin (PMUXEN)
     * 2. Select mux function F (value 0x5) for TCC0
     *
     * PA18 = even pin → PMUX[9] lower nibble (PMUXE)
     * PA20 = even pin → PMUX[10] lower nibble (PMUXE)
     * Both are PORT GROUP A (GROUP[0]).
     *
     * Verified in lib/samd21-dfp/pio/samd21g17d.h:
     *   PIN_PA18F_TCC0_WO2, MUX_PA18F_TCC0_WO2 = 5 (lines 1066-1069)
     *   PIN_PA20F_TCC0_WO6, MUX_PA20F_TCC0_WO6 = 5 (lines 1116-1119) */

    /* PA18: enable peripheral mux so TCC0 can drive this pin.
     * INEN enables the input buffer so PORT_IN can read the actual pin
     * voltage. This does not affect the output — it just lets the CPU
     * verify the pin state for diagnostics and self-tests. */
    PORT_REGS->GROUP[0].PORT_PINCFG[18] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;

    /* PA18: select mux function F (TCC0 WO[2]).
     * PMUX[9] covers pins 18 (even, lower nibble) and 19 (odd, upper nibble).
     * We preserve the upper nibble (pin 19) and set the lower nibble to F. */
    PORT_REGS->GROUP[0].PORT_PMUX[9] =
        (PORT_REGS->GROUP[0].PORT_PMUX[9] & 0xF0u)
        | PORT_PMUX_PMUXE_F_Val;

    /* PA20: enable peripheral mux so TCC0 can drive this pin.
     * INEN for the same reason as PA18 above. */
    PORT_REGS->GROUP[0].PORT_PINCFG[20] =
        PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;

    /* PA20: select mux function F (TCC0 WO[6]).
     * PMUX[10] covers pins 20 (even, lower nibble) and 21 (odd, upper nibble).
     * We preserve the upper nibble (pin 21) and set the lower nibble to F. */
    PORT_REGS->GROUP[0].PORT_PMUX[10] =
        (PORT_REGS->GROUP[0].PORT_PMUX[10] & 0xF0u)
        | PORT_PMUX_PMUXE_F_Val;
}

static void reset_tcc0_to_known_clean_state(void)
{
    /* Always reset before configuring. Clears any state left by a previous
     * boot, watchdog reset, or the debugger leaving registers dirty. */
    TCC0_REGS->TCC_CTRLA = TCC_CTRLA_SWRST_Msk;

    /* SWRST propagates across clock domains. Writes to TCC0 before this
     * clears are silently discarded — the peripheral is still resetting. */
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_SWRST_Msk) != 0u)
    {
        /* Wait for software reset to complete */
    }
}

static void configure_tcc0_waveform_generation_as_normal_pwm(void)
{
    /* NPWM = Normal Pulse-Width Modulation (single-slope).
     * The counter counts up from 0 to PER, then wraps to 0.
     * Output is HIGH while counter < CC[x], LOW while counter >= CC[x].
     * This gives a single-slope PWM where PER controls frequency and
     * CC[x] controls duty cycle.
     *
     * WAVE register is write-synchronized — must wait before proceeding. */
    TCC0_REGS->TCC_WAVE = TCC_WAVE_WAVEGEN_NPWM;

    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_WAVE_Msk) != 0u)
    {
        /* Wait for WAVE write to synchronize. Writing PER or CC before
         * this clears would cause them to be interpreted under the wrong
         * waveform mode. */
    }
}

static void configure_tcc0_dead_time_insertion_on_channel_2(void)
{
    /* WEXCTRL is "enable-protected" (datasheet p.620): it can only be
     * written while TCC0 is disabled (CTRLA.ENABLE = 0). We have not
     * enabled TCC0 yet, so this write is safe.
     *
     * DTIEN2: enables DTI on compare channel 2. This splits CC[2]'s
     *   output into two complementary signals on WO[2] (non-inverted,
     *   high-side drive) and WO[6] (inverted, low-side drive), with
     *   dead-time gaps at every transition.
     *
     * OTMX(0): default output matrix. CC[2] drives WO[2] and WO[6].
     *   Other OTMX values remap channels to different outputs, which
     *   we do not need.
     *
     * DTLS(2): 2 GCLK cycles of dead time on the WO[2] rising edge.
     *   During a transition, WO[2] stays LOW for 2 × 20.83 ns = ~42 ns
     *   after WO[6] goes LOW, before WO[2] goes HIGH.
     *
     * DTHS(2): 2 GCLK cycles of dead time on the WO[6] rising edge.
     *   During the opposite transition, WO[6] stays LOW for ~42 ns
     *   after WO[2] goes LOW, before WO[6] goes HIGH. */
    TCC0_REGS->TCC_WEXCTRL =
        TCC_WEXCTRL_DTIEN2_Msk                                |
        TCC_WEXCTRL_OTMX(0u)                                  |
        TCC_WEXCTRL_DTLS(TCC0_DEAD_TIME_LOW_SIDE_IN_GCLK_COUNTS)  |
        TCC_WEXCTRL_DTHS(TCC0_DEAD_TIME_HIGH_SIDE_IN_GCLK_COUNTS);
}

static void configure_tcc0_fault_safe_output_levels(void)
{
    /* DRVCTRL is "enable-protected" (datasheet p.620): must be written
     * while TCC0 is disabled.
     *
     * If a non-recoverable fault event occurs, the TCC0 output waveforms
     * are forced to the values in DRVCTRL.NRV. By setting NRE (enable)
     * on our output pins and leaving NRV at 0 (LOW), a fault forces
     * both PA18 and PA20 LOW — which turns both FETs off. This is the
     * safe state: no current flows, no shoot-through possible.
     *
     * NRE2: enable fault override on WO[2] (PA18, high-side drive)
     * NRE6: enable fault override on WO[6] (PA20, low-side drive)
     * NRV2 = NRV6 = 0 (default): fault forces both outputs LOW */
    TCC0_REGS->TCC_DRVCTRL =
        TCC_DRVCTRL_NRE2_Msk  |    /* fault drives WO[2] to NRV2 (LOW) */
        TCC_DRVCTRL_NRE6_Msk;      /* fault drives WO[6] to NRV6 (LOW) */
}

static void set_tcc0_period_for_300khz_switching_frequency(void)
{
    /* PER defines the TOP value of the counter. The counter counts
     * 0, 1, 2, ... PER, then wraps to 0. Total period = PER + 1 ticks.
     *
     * f_PWM = 48,000,000 / (1 × (159 + 1)) = 300,000 Hz exactly.
     *
     * PER is write-synchronized — the new value propagates across a
     * clock domain boundary. */
    TCC0_REGS->TCC_PER = TCC0_PERIOD_FOR_300KHZ;

    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_PER_Msk) != 0u)
    {
        /* Wait for PER write to synchronize. If we enable TCC0 before
         * this completes, the counter uses the reset-default PER value
         * (0xFFFFFF) and runs at the wrong frequency. */
    }
}

static void set_tcc0_initial_duty_cycle_to_zero(void)
{
    /* Start with 0% duty — both outputs LOW, no switching.
     * The MPPT algorithm will set the actual duty cycle once it
     * computes the first operating point.
     *
     * We write CC[2] directly (not CCB[2]) during initialization
     * because the timer is not running yet — there is no risk of
     * a mid-cycle glitch. */
    TCC0_REGS->TCC_CC[2] = 0u;

    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_CC2_Msk) != 0u)
    {
        /* Wait for CC[2] write to synchronize */
    }
}

static void enable_tcc0_pwm_output(void)
{
    /* This is the final step. After this bit is set, the counter starts
     * incrementing and the waveform outputs begin toggling on PA18/PA20.
     *
     * All configuration registers (WEXCTRL, DRVCTRL, WAVE, PER, CC)
     * must be written BEFORE this point, because WEXCTRL and DRVCTRL
     * are enable-protected and cannot be modified while ENABLE = 1. */
    TCC0_REGS->TCC_CTRLA |= TCC_CTRLA_ENABLE_Msk;

    /* ENABLE propagates across clock domains. Using the TCC0 outputs
     * before this clears may give incorrect waveforms for the first
     * few cycles. */
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_ENABLE_Msk) != 0u)
    {
        /* Wait for TCC0 enable to synchronize */
    }
}
