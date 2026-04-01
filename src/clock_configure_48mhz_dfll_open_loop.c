/* =============================================================================
 * clock_configure_48mhz_dfll_open_loop.c
 * Switches the CPU clock from the default 1 MHz (OSC8M / 8) to 48 MHz
 * using the DFLL48M oscillator in open-loop mode.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: NVMCTRL, SYSCTRL, GCLK
 * Clock: transitions GCLK0 from OSC8M/8 (1 MHz) to DFLL48M (48 MHz)
 *
 * Open-loop mode does not require an external crystal. The factory
 * calibration values burned into NVM at manufacturing provide ~2% accuracy,
 * which is well within UART tolerance (3%) and sufficient for all
 * current project requirements.
 *
 * Sources:
 *   - Stargirl Flowers: https://blog.thea.codes/understanding-the-sam-d21-clocks/
 *   - ForceTronics: https://github.com/ForceTronics/SamD21_Clock_Setup
 *   - Microchip DFLL48M init: https://developerhelp.microchip.com/xwiki/bin/view/
 *     products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview/peripherals/
 *     clock-system-overview/clock-system-dfll48m-init/
 *   - SAMD21 datasheet, Section 15 (GCLK), Section 17 (SYSCTRL)
 * =============================================================================
 */

#include <stdint.h>
#include "samd21g17d.h"
#include "clock_configure_48mhz_dfll_open_loop.h"

/* ── Private function prototypes ──────────────────────────────────────────── */

static void set_flash_wait_states_to_1_for_48mhz_operation(void);
static void load_dfll48m_factory_calibration_from_nvm(void);
static void enable_dfll48m_oscillator_and_wait_for_ready(void);
static void switch_gclk0_source_from_osc8m_to_dfll48m(void);

/* ── Public function ──────────────────────────────────────────────────────── */

void configure_cpu_clock_to_48mhz_using_dfll_open_loop(void)
{
    set_flash_wait_states_to_1_for_48mhz_operation();
    load_dfll48m_factory_calibration_from_nvm();
    enable_dfll48m_oscillator_and_wait_for_ready();
    switch_gclk0_source_from_osc8m_to_dfll48m();

    /* Update the global variable so any code that reads it
     * knows the actual CPU frequency. */
    SystemCoreClock = 48000000u;
}

/* ── Private functions ────────────────────────────────────────────────────── */

static void set_flash_wait_states_to_1_for_48mhz_operation(void)
{
    /* At frequencies above 24 MHz, the flash memory cannot respond in one
     * clock cycle. Without this wait state, the CPU reads garbage from flash
     * and crashes silently. This MUST be done BEFORE raising the clock.
     *
     * RWS=1 means "insert 1 wait state per flash read." */
    NVMCTRL_REGS->NVMCTRL_CTRLB |= NVMCTRL_CTRLB_RWS(1);
}

static void load_dfll48m_factory_calibration_from_nvm(void)
{
    /* Microchip measured the exact DFLL48M tuning for this specific chip
     * at the factory and burned the values into the OTP4 fuse area at
     * address 0x00806020. We read the coarse calibration value and load
     * it into the DFLL's tuning register for ~2% frequency accuracy.
     *
     * The coarse calibration is in OTP4 word 1 (offset 0x04 from OTP4 base),
     * bits [31:26] — a 6-bit value. */
    uint32_t otp4_word_1 = *((uint32_t *)(OTP4_ADDR + 4u));
    uint32_t coarse_calibration_value =
        (otp4_word_1 & FUSES_OTP4_WORD_1_DFLL48M_COARSE_CAL_Msk)
        >> FUSES_OTP4_WORD_1_DFLL48M_COARSE_CAL_Pos;

    /* Write the coarse calibration into DFLLVAL. The fine value is left
     * at its reset default (0x200 = midpoint) for open-loop mode. */
    SYSCTRL_REGS->SYSCTRL_DFLLVAL =
        SYSCTRL_DFLLVAL_COARSE(coarse_calibration_value) |
        SYSCTRL_DFLLVAL_FINE(0x200u);
}

static void enable_dfll48m_oscillator_and_wait_for_ready(void)
{
    /* Enable the DFLL48M in open-loop mode (MODE bit = 0).
     * ONDEMAND is cleared so the oscillator runs continuously.
     * In open-loop mode, no reference clock is needed — the output
     * frequency is determined solely by the calibration values. */
    SYSCTRL_REGS->SYSCTRL_DFLLCTRL =
        SYSCTRL_DFLLCTRL_ENABLE_Msk;

    /* Wait for the DFLL48M to stabilize. DFLLRDY in PCLKSR goes high
     * when the oscillator output is stable and usable as a clock source.
     * Without this wait, switching GCLK0 to DFLL48M could produce
     * glitched clock edges that crash the CPU. */
    while ((SYSCTRL_REGS->SYSCTRL_PCLKSR & SYSCTRL_PCLKSR_DFLLRDY_Msk) == 0u)
    {
        /* Wait for DFLL ready */
    }
}

static void switch_gclk0_source_from_osc8m_to_dfll48m(void)
{
    /* GCLK0 is the CPU clock generator. It currently runs from OSC8M/8
     * (1 MHz, the silicon default). We switch its source to DFLL48M.
     *
     * GCLK_GENCTRL is a shared register — writing it affects whichever
     * generator is selected by the ID field. ID=0 selects GCLK0.
     *
     * GENEN enables the generator. SRC selects the clock source.
     * IDC (Improve Duty Cycle) produces a more symmetric 50/50 output. */
    GCLK_REGS->GCLK_GENCTRL =
        GCLK_GENCTRL_ID(0u)            |   /* configure generator 0 (CPU clock) */
        GCLK_GENCTRL_SRC_DFLL48M       |   /* source: DFLL48M (48 MHz) */
        GCLK_GENCTRL_GENEN_Msk         |   /* enable this generator */
        GCLK_GENCTRL_IDC_Msk;              /* improve duty cycle for cleaner edges */

    /* GENCTRL writes cross a clock domain boundary. The CPU must not
     * use GCLK0-dependent resources until synchronization completes.
     * Writing another GCLK register before this clears causes the
     * write to be silently lost. */
    while ((GCLK_REGS->GCLK_STATUS & GCLK_STATUS_SYNCBUSY_Msk) != 0u)
    {
        /* Wait for GCLK synchronization to complete */
    }
}
