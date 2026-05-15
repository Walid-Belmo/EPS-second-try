/* =============================================================================
 * pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c
 *
 * THE buck-converter PWM driver. Single source of truth for everything
 * the firmware does to TCC0. Sets up the timer at boot and lets the
 * mode runners change duty cycle every iteration.
 *
 * BUILD TARGET: mainboard only.
 *
 * ────────────────────────────────────────────────────────────────────
 * What this driver actually does
 * ────────────────────────────────────────────────────────────────────
 *
 * The synchronous buck converter on the PCB has TWO MOSFETs (high-side
 * and low-side) driven by the EPC2152 GaN gate driver. The MOSFETs
 * MUST NEVER be on at the same time — that's a "shoot-through" event,
 * battery shorts to ground through both MOSFETs, dies in microseconds.
 *
 * So the gate driver's two inputs need a complementary PWM signal pair
 * with a small "both off" gap (the dead time) every time they switch:
 *
 *     PWM_H  ━━━━┓     ┏━━━━━━━━┓     ┏━━━━
 *                 ┃     ┃        ┃     ┃
 *                 ┗━━━━━┛        ┗━━━━━┛       both off here
 *                 ←dt→            ←dt→
 *     PWM_L  ┓     ┏━━━━━━━━┓     ┏━━━━━━━┓
 *            ┃     ┃        ┃     ┃       ┃
 *            ┗━━━━━┛        ┗━━━━━┛       ┗━
 *
 * ────────────────────────────────────────────────────────────────────
 * Why this is harder than it should be: the PCB design error
 * ────────────────────────────────────────────────────────────────────
 *
 * The PCB routes:
 *   PA12 = PWM_H = TCC0 WO[6], peripheral mux F
 *   PA13 = PWM_L = TCC0 WO[7], peripheral mux F
 *
 * The SAMD21 TCC0 dead-time-insertion (DTI) unit operates on PAIRS of
 * waveform outputs. The natural pairs are:
 *
 *     DTI0:  WO[0] + WO[4]
 *     DTI1:  WO[1] + WO[5]
 *     DTI2:  WO[2] + WO[6]      ← we use this one
 *     DTI3:  WO[3] + WO[7]      ← and this one
 *
 * If the PCB had routed PA12 to WO[2] and PA13 to WO[6] (a natural
 * pair), the firmware could have used a single DTI slice in
 * complementary mode and gotten clean dead-time for free. Instead the
 * PCB picked WO[6] and WO[7] — TWO upper-half outputs from TWO
 * DIFFERENT DTI slices, which the natural complementary mode cannot
 * combine.
 *
 * Full back-story in docs/pcb_design_error_for_pwm.md.
 *
 * ────────────────────────────────────────────────────────────────────
 * The workaround
 * ────────────────────────────────────────────────────────────────────
 *
 * Run TWO TCC0 DTI slices simultaneously, both fed the same compare
 * value, with one slice's output SWAPPED so the two slices' upper
 * halves produce a complementary pair when wired to PA12 and PA13:
 *
 *     CC[2] ─→ DTI2 ─→ SWAP2 enabled  ─→ WO[6] = PA12 = PWM_H
 *     CC[3] ─→ DTI3 ─→ SWAP3 disabled ─→ WO[7] = PA13 = PWM_L
 *
 * Both DTI slices share the same TCC0 counter and period, so the two
 * outputs stay phase-locked. Both compare registers are always written
 * with the same value (write_compare_value_to_channels_2_and_3 below).
 * The DTI-inserted dead time is identical on both edges of both
 * outputs because both slices use the same DTLS / DTHS values.
 *
 * The SWAP unit is the right choice (rather than INVEN inversion)
 * because SWAP happens BEFORE the DTI dead-time gap and preserves it.
 * INVEN happens AFTER and would flip the dead-time low interval into a
 * dead-time high interval — i.e. both outputs HIGH at the same time —
 * which is exactly what we cannot have.
 *
 * ────────────────────────────────────────────────────────────────────
 * Frequency choice
 * ────────────────────────────────────────────────────────────────────
 *
 * Mission spec: 300 kHz buck switching frequency. With the GCLK0
 * source at 48 MHz, the TCC0 period register value is:
 *
 *     PER = (48 MHz / 300 kHz) - 1 = 160 - 1 = 159
 *
 * The "-1" is because TCC0 counts 0..PER inclusive, so PER=159 gives
 * 160 counts per cycle.
 *
 * The compare value (CC[2] = CC[3]) is the count at which the output
 * edge happens. Duty cycle = compare / (PER + 1) = compare / 160.
 *
 * Min/max compare clamps (8 / 151) keep the duty in [5%, 94%]. We
 * never run at exactly 0% or 100%: at 0% the high-side gate driver's
 * bootstrap capacitor never gets a chance to recharge; at 100% the
 * low-side gets stuck off and current builds in the inductor with no
 * release path. Both kill the converter.
 *
 * ────────────────────────────────────────────────────────────────────
 * Init-time vs runtime
 * ────────────────────────────────────────────────────────────────────
 *
 *   pwm_buck_converter_initialize_for_demo()
 *     - Called once at boot from setup_pwm_output_starting_off().
 *     - Configures every TCC0 register, but LEAVES PA12/PA13 routed to
 *       GPIO output low. The TCC0 hardware is generating PWM internally
 *       but the pins aren't connected to TCC0 yet.
 *     - This means immediately after init the converter pins are dark
 *       and stay dark until the first non-zero set_duty_cycle call.
 *
 *   pwm_buck_converter_set_duty_cycle(N)
 *     - Called every main-loop iteration from apply_outputs_to_board().
 *     - If N == 0: drive pins LOW via GPIO (don't even try TCC0).
 *     - If N > 0:  set the buffered compare register, force the
 *       buffered value to take effect on the next period boundary,
 *       and (if the pins were still GPIO) route them to TCC0 mux F.
 * =============================================================================
 */

#include <stdint.h>

#include "samd21j17d.h"
#include "assertion_handler.h"
#include "pwm_buck_converter.h"

#define PORT_GROUP_INDEX_FOR_PORT_A                    0u
#define MAINBOARD_PWM_HIGH_SIDE_PIN_NUMBER            12u  /* PA12 */
#define MAINBOARD_PWM_LOW_SIDE_PIN_NUMBER             13u  /* PA13 */

/* PMUX[6] holds the mux for pin pair PA12 (low nibble) + PA13 (high
 * nibble). Set both nibbles to function F (TCC0). */
#define MAINBOARD_PWM_PA12_PA13_PMUX_INDEX             6u
#define PORT_MUX_FUNCTION_F_FOR_TCC0                   5u  /* function F = mux value 5 */

/* TCC0 period: (48 MHz / 300 kHz) - 1 = 159. Counter goes 0..159. */
#define TCC0_PERIOD_FOR_300KHZ                       159u

/* Dead time = 6 GCLK ticks = 6 / 48 MHz = 125 ns on each edge.
 * Picked conservatively for the EPC2152 GaN driver — the chip's
 * datasheet says minimum on-time is ~30 ns and minimum dead time is
 * ~5 ns. 125 ns is well above both, leaving margin for component
 * tolerances. Tightening this would improve efficiency at the cost
 * of a smaller safety margin. */
#define TCC0_DEAD_TIME_LOW_SIDE_IN_GCLK_COUNTS        6u
#define TCC0_DEAD_TIME_HIGH_SIDE_IN_GCLK_COUNTS       6u

/* Duty-cycle clamps — see top-of-file comment for the bootstrap-cap
 * and inductor-current rationale. 8/160 ≈ 5%, 151/160 ≈ 94%. */
#define BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE           8u
#define BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE         151u

/* Public API expresses duty as a fraction of 65535 (uint16_t). The
 * conversion to a TCC0 compare value scales by PER/65535. */
#define DUTY_CYCLE_INPUT_FULL_SCALE               65535u

/* Two driver state flags. Kept as file-scope statics rather than in
 * runtime state because they are private bookkeeping for THIS driver
 * and have no meaning outside it. */
static uint8_t pwm_has_been_initialized;            /* set by init, read by set_duty for sanity */
static uint8_t pwm_outputs_are_routed_to_tcc0;      /* tracks whether pins are GPIO-low or TCC0-driven */

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

/*
 * One-shot bring-up. Called once at boot from
 * setup_pwm_output_starting_off(). Sequence matters:
 *
 *   1. Force PA12/PA13 LOW as GPIO outputs FIRST. Without this the
 *      pins might glitch high during the rest of init while TCC0 is
 *      configured but not yet enabled.
 *   2. Enable bus clock so we can write TCC0 registers at all.
 *   3. Connect functional clock so TCC0 can actually count.
 *   4. Reset TCC0 to a known clean state (in case the debugger or a
 *      previous boot left state behind).
 *   5. Configure WAVE register (sets PWM mode, enables SWAP2).
 *   6. Configure WEXCTRL (enables DTI on slices 2 and 3, sets dead
 *      time).
 *   7. Configure DRVCTRL (defines the safe output level when the
 *      hardware decides to invalidate outputs).
 *   8. Set the period register (defines switching frequency).
 *   9. Set initial compare = 0 (so even if pins were routed to TCC0
 *      right now, no shoot-through could happen).
 *  10. Enable TCC0 — counter starts running, but pins still GPIO.
 *  11. Mark initialised.
 *  12. Force PA12/PA13 LOW as GPIO again, just to be paranoid in case
 *      the enable step somehow reconfigured them.
 *
 * After this returns, TCC0 is generating PWM internally (period and
 * compare are set) but PA12/PA13 are still GPIO outputs at LOW. The
 * first call to set_duty_cycle with a non-zero value is what actually
 * connects the pins to TCC0.
 */
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

/*
 * Update the buck converter's duty cycle. Called every main-loop
 * iteration from apply_outputs_to_board(). The input is a fraction of
 * 65535 (so 32768 = 50%); we convert it to a TCC0 compare value and
 * push it into the BUFFERED compare register so the change happens
 * cleanly on the next period boundary instead of mid-cycle.
 *
 * Special case for duty == 0: drop the pins to GPIO LOW. Doing nothing
 * else (just writing compare = 0) would leave a brief gate-driver
 * transient at every period boundary because the high-side input pulse
 * narrows but never quite goes away. Disconnecting the pins from TCC0
 * entirely is cleaner.
 */
void pwm_buck_converter_set_duty_cycle(
    uint16_t duty_cycle_as_fraction_of_65535)
{
    SATELLITE_ASSERT(pwm_has_been_initialized == 1u);

    if (duty_cycle_as_fraction_of_65535 == 0u)
    {
        /* Off: pins as GPIO LOW, plus also push compare=0 to the
         * buffered register so when the operator next bumps the duty
         * up there's no stale value. */
        force_pa12_and_pa13_low_as_gpio_outputs();
        write_buffered_compare_value_to_channels_2_and_3(0u);
        force_tcc0_buffered_register_update();
        return;
    }

    /* If we're transitioning from "off" (pins GPIO) to "on" (pins
     * TCC0), we need to write the new compare BEFORE routing the
     * pins. Otherwise the pins would briefly carry the previous
     * compare value (likely 0) before the new one took effect. */
    uint8_t outputs_need_to_be_routed_after_compare_update =
        (pwm_outputs_are_routed_to_tcc0 == 0u) ? 1u : 0u;

    uint32_t compare_value_for_tcc0 =
        convert_fractional_duty_cycle_to_compare_value(
            duty_cycle_as_fraction_of_65535);

    /* Defence in depth: the convert function clamps already, but
     * assert here so a future bug in the convert function gets caught
     * loudly instead of silently producing a duty outside the safe
     * envelope. */
    SATELLITE_ASSERT(
        (compare_value_for_tcc0 >= BUCK_MINIMUM_DUTY_CYCLE_AS_CC_VALUE)
        && (compare_value_for_tcc0 <= BUCK_MAXIMUM_DUTY_CYCLE_AS_CC_VALUE));

    /* Buffered compare write — the new value sits in CCB[N] until the
     * next period boundary, at which point TCC0 swaps it into CC[N]
     * atomically. This avoids the mid-cycle glitch that a direct
     * CC[N] write would cause. */
    write_buffered_compare_value_to_channels_2_and_3(compare_value_for_tcc0);

    if (outputs_need_to_be_routed_after_compare_update != 0u)
    {
        /* Force the buffered compare value to swap into the live
         * register NOW (don't wait for the next period boundary).
         * Then route the pins to TCC0. The order means the pins
         * never see a stale compare value. */
        force_tcc0_buffered_register_update();
        route_pa12_as_tcc0_wo6_and_pa13_as_tcc0_wo7();
    }
}

/* ────────────────────────────────────────────────────────────────────
 * Initialisation helpers — register-by-register WHY commentary.
 * ──────────────────────────────────────────────────────────────────── */

/*
 * Power up TCC0's register bank on the APB-C bus. Without this bit
 * set, every TCC0 register read returns 0xFFFFFFFF and writes do
 * nothing. Identical pattern to enable_sercom0_bus_clock above —
 * every SAMD21 peripheral's bus clock is gated by a bit in the PM
 * register block.
 */
static void enable_tcc0_bus_clock_on_apbc(void)
{
    PM_REGS->PM_APBCMASK |= PM_APBCMASK_TCC0_Msk;
}

/*
 * Connect GCLK0 (48 MHz, the CPU clock) to TCC0's functional clock.
 * The bus clock (above) lets the CPU access TCC0 registers; the
 * functional clock lets TCC0 actually count. Without this, the
 * counter never increments and PWM never appears on the pins even
 * though every register is correct.
 *
 * GCLK_CLKCTRL_ID_TCC0_TCC1 selects TCC0's functional-clock channel
 * (TCC0 and TCC1 share one channel). The cross-clock-domain SYNCBUSY
 * wait is required — writing more GCLK registers before this clears
 * silently drops them.
 */
static void connect_48mhz_gclk0_to_tcc0_peripheral_clock(void)
{
    GCLK_REGS->GCLK_CLKCTRL =
        GCLK_CLKCTRL_CLKEN_Msk |
        GCLK_CLKCTRL_GEN_GCLK0 |
        GCLK_CLKCTRL_ID_TCC0_TCC1;

    while ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) != 0u)
    {
    }
}

/*
 * Connect PA12 and PA13 to TCC0's WO[6] / WO[7] outputs via the
 * peripheral mux. After this returns, the TCC0 waveform generator
 * is physically driving the pins; before, the pins are GPIO outputs
 * controlled by the GPIO direction/output registers.
 *
 * Three register writes:
 *   1. PINCFG[12] for PA12: set PMUXEN (use the mux setting) and
 *      INEN (input enable, which TCC0 wants for some output modes).
 *   2. PINCFG[13] for PA13: same.
 *   3. PMUX[6] writes both nibbles in one go: low nibble = PA12,
 *      high nibble = PA13, both to function F (mux value 5 = TCC0).
 *
 * Sets the routed flag so set_duty_cycle knows the next call doesn't
 * need to re-route.
 */
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

/*
 * Disconnect PA12 and PA13 from TCC0 and force them to GPIO LOW.
 * Used at boot (so pins are quiet before TCC0 is configured) and
 * whenever set_duty_cycle is called with duty == 0.
 *
 * Three writes:
 *   1. PINCFG[12] = 0: clears PMUXEN, so the pin is GPIO again.
 *   2. PINCFG[13] = 0: same.
 *   3. PORT_OUTCLR: drives both pins LOW.
 *   4. PORT_DIRSET: makes both pins outputs (DIRSET sets bits to 1
 *      in the DIR register, which means "output").
 *
 * Order matters: drive LOW BEFORE setting direction to output, so the
 * very first sample of the output latch is already 0. If we set
 * direction first then drove low, the pin would briefly carry
 * whatever stale value was in the OUT register.
 */
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

/*
 * Software-reset TCC0 to the post-power-on default. Same pattern as
 * the SERCOM and SystCtrl resets — never trust the leftover state
 * from a previous boot or a debugger session. SWRST propagates across
 * a clock-domain boundary; further config writes before SYNCBUSY
 * clears would be silently dropped.
 */
static void reset_tcc0_to_known_clean_state(void)
{
    TCC0_REGS->TCC_CTRLA = TCC_CTRLA_SWRST_Msk;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_SWRST_Msk) != 0u)
    {
    }
}

/*
 * Configure WAVE register: WAVEGEN = NPWM (Normal PWM, single-slope)
 * and SWAP2 = 1.
 *
 * NPWM means "the output is high while counter < compare, low
 * otherwise" — the standard PWM behaviour. The alternative (DSPWM,
 * dual-slope) gives center-aligned PWM which we don't need.
 *
 * SWAP2 = 1 swaps the two outputs of DTI slice 2: instead of
 * CC[2]→WO[2] and CC[2]'→WO[6] (where ' is the inverted edge), we
 * get CC[2]'→WO[2] and CC[2]→WO[6]. This is the magic that makes
 * WO[6] (PA12, PWM_H) carry the high-side-shaped waveform even
 * though slice 2's natural output for the upper-half pin is the
 * inverted one. Combined with DTI3 unswapped on WO[7] (PA13, PWM_L),
 * the two pins together form a complementary pair with dead time —
 * which is what the gate driver needs.
 */
static void configure_tcc0_waveform_generation_with_swap2(void)
{
    TCC0_REGS->TCC_WAVE =
        TCC_WAVE_WAVEGEN_NPWM |
        TCC_WAVE_SWAP2_Msk;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_WAVE_Msk) != 0u)
    {
    }
}

/*
 * Enable dead-time insertion on slices 2 and 3 (the slices feeding
 * WO[6]/WO[2] and WO[7]/WO[3] respectively). Set the dead-time count
 * for both edges of both slices to the same value (6 GCLK ticks =
 * ~125 ns).
 *
 * OTMX = 0 selects the standard output matrix (no special remapping).
 * The other matrix modes are for fancy multi-phase configurations we
 * don't need here.
 *
 * No SYNCBUSY wait because WEXCTRL doesn't need one — it's a write to
 * a register that doesn't cross clock domains in the same way the
 * waveform-output configuration does.
 */
static void configure_tcc0_dead_time_insertion_on_slices_2_and_3(void)
{
    TCC0_REGS->TCC_WEXCTRL =
        TCC_WEXCTRL_DTIEN2_Msk |
        TCC_WEXCTRL_DTIEN3_Msk |
        TCC_WEXCTRL_OTMX(0u) |
        TCC_WEXCTRL_DTLS(TCC0_DEAD_TIME_LOW_SIDE_IN_GCLK_COUNTS) |
        TCC_WEXCTRL_DTHS(TCC0_DEAD_TIME_HIGH_SIDE_IN_GCLK_COUNTS);
}

/*
 * Configure DRVCTRL for fault-safe output levels. NRE6 / NRE7 enable
 * Non-Recoverable Output settings on WO[6] and WO[7]: when TCC0
 * decides an output is invalid (e.g. during fault recovery), the pin
 * is driven LOW instead of high-Z. LOW is the safe state for the
 * EPC2152 inputs — it means "MOSFET off".
 */
static void configure_tcc0_fault_safe_output_levels(void)
{
    TCC0_REGS->TCC_DRVCTRL =
        TCC_DRVCTRL_NRE6_Msk |
        TCC_DRVCTRL_NRE7_Msk;
}

/*
 * Set the period register that defines the switching frequency.
 * PER = 159 means the counter goes 0..159 (160 counts) per cycle;
 * with the 48 MHz GCLK that's 48 MHz / 160 = 300 kHz, the mission
 * spec switching frequency.
 *
 * SYNCBUSY wait is required for PER — it crosses a clock domain.
 */
static void set_tcc0_period_for_300khz_switching_frequency(void)
{
    TCC0_REGS->TCC_PER = TCC0_PERIOD_FOR_300KHZ;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_PER_Msk) != 0u)
    {
    }
}

/*
 * Initialise both compare channels to 0. Even though the pins are
 * still GPIO at this point, having compare = 0 means that IF something
 * else in the firmware accidentally routed the pins to TCC0 right
 * now, no shoot-through could happen — both outputs would stay LOW
 * (compare=0 means "always below counter" means "always low" in NPWM
 * mode).
 *
 * Uses the immediate-write helper (not buffered) because we want this
 * to take effect right now during init, not on some future period
 * boundary.
 */
static void set_tcc0_initial_duty_cycle_to_zero(void)
{
    write_compare_value_to_channels_2_and_3(0u);
}

/*
 * Final init step: flip ENABLE and wait for the sync. After this,
 * TCC0's counter starts running (counts 0→159→0 every 3.33 µs at
 * 300 kHz). The pins still aren't connected to TCC0 yet — that
 * happens later, when set_duty_cycle is called with a non-zero value.
 */
static void enable_tcc0_pwm_output(void)
{
    TCC0_REGS->TCC_CTRLA |= TCC_CTRLA_ENABLE_Msk;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_ENABLE_Msk) != 0u)
    {
    }
}

/* ────────────────────────────────────────────────────────────────────
 * Per-iteration helpers
 * ──────────────────────────────────────────────────────────────────── */

/*
 * Convert the public-API duty (fraction of 65535) into a TCC0 compare
 * value (0..PER inclusive). Linear scaling, then clamp to the
 * 5%..94% safe range.
 *
 *   compare = duty × PER / 65535
 *
 * The intermediate multiplication needs uint32_t — duty (max 65535)
 * times PER (max 159) = 10.4 million, which would overflow uint16_t.
 * The division then brings it back into the 0..159 range.
 */
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

/*
 * Immediate compare write. Takes effect IMMEDIATELY (potentially
 * mid-cycle, which can produce a glitch on the output). Used only
 * during init (where we don't care because pins aren't routed yet).
 *
 * Both CC[2] and CC[3] always get the same value because both DTI
 * slices need to swap their outputs at the same counter value to
 * maintain the complementary pair on PA12/PA13.
 */
static void write_compare_value_to_channels_2_and_3(uint32_t compare_value)
{
    TCC0_REGS->TCC_CC[2] = compare_value;
    TCC0_REGS->TCC_CC[3] = compare_value;
    while ((TCC0_REGS->TCC_SYNCBUSY
            & (TCC_SYNCBUSY_CC2_Msk | TCC_SYNCBUSY_CC3_Msk)) != 0u)
    {
    }
}

/*
 * Buffered compare write. The new value sits in CCB[N] until the
 * next period boundary, at which point TCC0 atomically swaps it into
 * CC[N]. This is the glitch-free way to update duty cycle while the
 * converter is running — used by every set_duty_cycle call.
 */
static void write_buffered_compare_value_to_channels_2_and_3(
    uint32_t compare_value)
{
    TCC0_REGS->TCC_CCB[2] = compare_value;
    TCC0_REGS->TCC_CCB[3] = compare_value;
    while ((TCC0_REGS->TCC_SYNCBUSY
            & (TCC_SYNCBUSY_CCB2_Msk | TCC_SYNCBUSY_CCB3_Msk)) != 0u)
    {
    }
}

/*
 * Force the buffered compare value to swap into the live CC[N]
 * register IMMEDIATELY instead of waiting for the next period
 * boundary. Used during the GPIO→TCC0 transition in set_duty_cycle so
 * the pins don't carry a stale value when they get routed.
 *
 * CTRLBSET writes 1-to-set; CMD_UPDATE is a one-shot command that
 * the hardware processes and then auto-clears.
 */
static void force_tcc0_buffered_register_update(void)
{
    TCC0_REGS->TCC_CTRLBSET = TCC_CTRLBSET_CMD_UPDATE;
    while ((TCC0_REGS->TCC_SYNCBUSY & TCC_SYNCBUSY_CTRLB_Msk) != 0u)
    {
    }
}
