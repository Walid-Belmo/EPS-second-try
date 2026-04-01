/* =============================================================================
 * clock_configure_48mhz_dfll_open_loop.c
 * Switches the CPU clock from the default 1 MHz (OSC8M / 8) to 48 MHz
 * using the DFLL48M oscillator in open-loop mode.
 *
 * Category: HARDWARE DRIVER
 * Peripheral: NVMCTRL, SYSCTRL, GCLK
 * Clock: transitions GCLK0 from OSC8M/8 (1 MHz) to DFLL48M (48 MHz)
 *
 * CRITICAL — ERRATA 1.2.1 WORKAROUND:
 * The DFLL ONDEMAND bit is set by default after reset. If ANY DFLL register
 * (DFLLVAL, DFLLMUL, etc.) is written while ONDEMAND is set, the device
 * FREEZES permanently until power-cycled and erased. The workaround is to
 * write DFLLCTRL with ENABLE (which clears ONDEMAND) BEFORE writing any
 * other DFLL register.
 *
 * This sequence is verified against three independent working implementations:
 *   - Stargirl Flowers: https://blog.thea.codes/understanding-the-sam-d21-clocks/
 *   - ForceTronics: https://github.com/ForceTronics/SamD21_Clock_Setup
 *   - Arduino Zero bootloader: https://github.com/arduino/ArduinoCore-samd
 *   - Microchip Errata DS80000760G, Section 1.2.1
 * =============================================================================
 */

#include <stdint.h>
#include "samd21g17d.h"
#include "clock_configure_48mhz_dfll_open_loop.h"

/* ── Private function prototypes ──────────────────────────────────────────── */

static void set_flash_wait_states_to_1_for_48mhz_operation(void);
static void enable_dfll48m_and_clear_ondemand_bit_errata_workaround(void);
static void load_dfll48m_factory_calibration_from_nvm(void);
static void enable_dfll48m_with_calibration_values_loaded(void);
static void switch_gclk0_source_from_osc8m_to_dfll48m(void);

/* ── Public function ──────────────────────────────────────────────────────── */

void configure_cpu_clock_to_48mhz_using_dfll_open_loop(void)
{
    set_flash_wait_states_to_1_for_48mhz_operation();
    enable_dfll48m_and_clear_ondemand_bit_errata_workaround();
    load_dfll48m_factory_calibration_from_nvm();
    enable_dfll48m_with_calibration_values_loaded();
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

static void enable_dfll48m_and_clear_ondemand_bit_errata_workaround(void)
{
    /* ERRATA 1.2.1 WORKAROUND (DS80000760G):
     *
     * After reset, DFLLCTRL.ONDEMAND is set (1). In this state, writing to
     * ANY other DFLL register (DFLLVAL, DFLLMUL, etc.) FREEZES THE DEVICE.
     * The freeze is unrecoverable without a chip erase via MPLAB X IDE.
     *
     * The workaround: write DFLLCTRL with ENABLE bit set. This simultaneously
     * enables the DFLL and clears the ONDEMAND bit (because we are writing
     * the entire register and not setting ONDEMAND). After this write, it is
     * safe to access DFLLVAL and other DFLL registers.
     *
     * We must wait for DFLLRDY before AND after this write. DFLLRDY indicates
     * register synchronization is complete, not that the clock is stable. */
    while ((SYSCTRL_REGS->SYSCTRL_PCLKSR & SYSCTRL_PCLKSR_DFLLRDY_Msk) == 0u)
    {
        /* Wait for DFLL registers to be ready for writing */
    }

    SYSCTRL_REGS->SYSCTRL_DFLLCTRL = SYSCTRL_DFLLCTRL_ENABLE_Msk;

    while ((SYSCTRL_REGS->SYSCTRL_PCLKSR & SYSCTRL_PCLKSR_DFLLRDY_Msk) == 0u)
    {
        /* Wait for the ENABLE write to synchronize. Writing to DFLLVAL
         * before this clears would freeze the device. */
    }
}

static void load_dfll48m_factory_calibration_from_nvm(void)
{
    /* Microchip measured the exact DFLL48M tuning for this specific chip
     * at the factory and burned the values into the OTP4 fuse area.
     *
     * Coarse calibration: OTP4 Word 1 (address 0x00806024), bits [31:26]
     * Fine calibration:   OTP4 Word 2 (address 0x00806028), bits [9:0]
     *
     * These values give ~2% frequency accuracy without an external crystal.
     * UART at 115200 baud tolerates up to 3%, so this is sufficient. */
    uint32_t otp4_word_1 = *((uint32_t *)(OTP4_ADDR + 4u));
    uint32_t coarse_calibration_value =
        (otp4_word_1 & FUSES_OTP4_WORD_1_DFLL48M_COARSE_CAL_Msk)
        >> FUSES_OTP4_WORD_1_DFLL48M_COARSE_CAL_Pos;

    uint32_t otp4_word_2 = *((uint32_t *)(OTP4_ADDR + 8u));
    uint32_t fine_calibration_value =
        (otp4_word_2 & FUSES_OTP4_WORD_2_DFLL48M_FINE_CAL_Msk)
        >> FUSES_OTP4_WORD_2_DFLL48M_FINE_CAL_Pos;

    /* Write both calibration values to DFLLVAL.
     * This is safe because the errata workaround (above) already cleared
     * the ONDEMAND bit by writing DFLLCTRL with ENABLE. */
    SYSCTRL_REGS->SYSCTRL_DFLLVAL =
        SYSCTRL_DFLLVAL_COARSE(coarse_calibration_value) |
        SYSCTRL_DFLLVAL_FINE(fine_calibration_value);

    while ((SYSCTRL_REGS->SYSCTRL_PCLKSR & SYSCTRL_PCLKSR_DFLLRDY_Msk) == 0u)
    {
        /* Wait for DFLLVAL write to synchronize before proceeding */
    }
}

static void enable_dfll48m_with_calibration_values_loaded(void)
{
    /* Re-enable the DFLL now that calibration values are loaded.
     * The DFLL was already enabled by the errata workaround, but
     * re-writing ENABLE ensures the calibration values take effect. */
    SYSCTRL_REGS->SYSCTRL_DFLLCTRL = SYSCTRL_DFLLCTRL_ENABLE_Msk;

    while ((SYSCTRL_REGS->SYSCTRL_PCLKSR & SYSCTRL_PCLKSR_DFLLRDY_Msk) == 0u)
    {
        /* Wait for DFLL to be ready with new calibration values */
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
