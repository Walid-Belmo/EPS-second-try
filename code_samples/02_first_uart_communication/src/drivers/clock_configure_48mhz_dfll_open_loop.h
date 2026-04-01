/* =============================================================================
 * clock_configure_48mhz_dfll_open_loop.h
 * Configures the CPU clock from default 1 MHz (OSC8M/8) to 48 MHz (DFLL48M)
 * using open-loop mode with factory calibration values from NVM.
 *
 * Call once from main() before initializing any peripherals.
 * =============================================================================
 */

#ifndef CLOCK_CONFIGURE_48MHZ_DFLL_OPEN_LOOP_H
#define CLOCK_CONFIGURE_48MHZ_DFLL_OPEN_LOOP_H

void configure_cpu_clock_to_48mhz_using_dfll_open_loop(void);

#endif /* CLOCK_CONFIGURE_48MHZ_DFLL_OPEN_LOOP_H */
