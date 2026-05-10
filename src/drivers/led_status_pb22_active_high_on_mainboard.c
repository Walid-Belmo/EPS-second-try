// =============================================================================
// led_status_pb22_active_high_on_mainboard.c
//
// Tiny GPIO-only driver for the green status LED (LED2) on the EPS PCU
// testing board V4.1 (mainboard PCB).
//
// BUILD TARGET: mainboard only (PB22 is not wired to an LED on the dev board)
//
// Category: HARDWARE DRIVER
// Peripheral: PORT group B
// Pins: PB22 (push-pull output, drives LED2 active-HIGH through R52 = 750 ohm)
// Clock: PORT block does not require a GCLK; its bus clock is enabled by the
//        chip's reset state, so this driver works immediately at any CPU clock
//        speed without any prior peripheral setup.
// Interrupt: none
//
// Reference: docs/mainboard_pinout_pcu_v4_1.md
// =============================================================================

// Standard library includes first
#include <stdint.h>

// Hardware-specific include — pulls in the chip's register layout. The
// preprocessor flag -D__SAMD21J17D__ in the Makefile selects the correct
// chip header from the SAMD21 device family pack.
#include "samd21j17d.h"

// Project includes third
#include "led_status_pb22_active_high_on_mainboard.h"

// ── Module constants ─────────────────────────────────────────────────────────

// Chip pin number of the LED on PORT group B. From the schematic of the
// EPS PCU testing board V4.1: PB22 -> R52 (750 ohm) -> LED2 (green) -> GND,
// so a logic HIGH on PB22 lights the LED. Source: MCU.kicad_sch, the wire
// segment from (190.5, 40.64) up to (190.5, 38.1) and then right to the
// "LED2" net label, where the chip pin offset (48.26, 20.32) corresponds
// to the bottom-right pin of the chip's top row (PB22).
#define MAINBOARD_STATUS_LED_PIN_NUMBER_ON_PORT_B  22u

// Index of port group B inside PORT_REGS->GROUP[]. Group 0 is port A,
// group 1 is port B. The SAMD21 PORT peripheral defines exactly two groups
// on this part.
#define PORT_GROUP_INDEX_FOR_PORT_B  1u

// Bit mask for PB22 within port group B's 32-bit registers. Pre-computed
// once so register writes below stay readable.
#define MAINBOARD_STATUS_LED_BIT_MASK \
    ((uint32_t)1u << MAINBOARD_STATUS_LED_PIN_NUMBER_ON_PORT_B)

// ── Public functions ─────────────────────────────────────────────────────────

void configure_pb22_as_gpio_output_for_status_led_on_mainboard(void)
{
    // Mark PB22 as an output. PORT_DIRSET is a write-1-to-set register: bits
    // written as 1 set the corresponding pin's direction to "output" and
    // enable its output driver; bits written as 0 leave the pin's direction
    // untouched. This means we cannot accidentally change any other pin's
    // direction with this single register write.
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_B].PORT_DIRSET =
        MAINBOARD_STATUS_LED_BIT_MASK;

    // Drive PB22 LOW so the LED starts OFF on power-on. PORT_OUTCLR is also
    // write-1-to-clear: bits written as 1 force the corresponding output
    // latch to 0; bits written as 0 leave other outputs untouched.
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_B].PORT_OUTCLR =
        MAINBOARD_STATUS_LED_BIT_MASK;
}

void toggle_pb22_status_led_on_mainboard(void)
{
    // PORT_OUTTGL is write-1-to-toggle: every bit written as 1 inverts the
    // corresponding output latch in a single atomic register write. This
    // avoids the read-modify-write race that "OUT ^= mask" would create if
    // an interrupt also touched the same port group.
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_B].PORT_OUTTGL =
        MAINBOARD_STATUS_LED_BIT_MASK;
}

void turn_pb22_status_led_on_mainboard(void)
{
    // PORT_OUTSET is write-1-to-set: bits written as 1 force the output latch
    // HIGH; other bits are unaffected. PB22 driven HIGH lights LED2.
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_B].PORT_OUTSET =
        MAINBOARD_STATUS_LED_BIT_MASK;
}

void turn_pb22_status_led_off_on_mainboard(void)
{
    // PORT_OUTCLR is write-1-to-clear: bits written as 1 force the output
    // latch LOW; other bits are unaffected. PB22 LOW extinguishes LED2.
    PORT_REGS->GROUP[PORT_GROUP_INDEX_FOR_PORT_B].PORT_OUTCLR =
        MAINBOARD_STATUS_LED_BIT_MASK;
}
