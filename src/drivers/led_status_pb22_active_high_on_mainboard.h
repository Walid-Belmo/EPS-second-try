// =============================================================================
// led_status_pb22_active_high_on_mainboard.h
//
// Tiny GPIO-only driver for the green status LED (LED2) on the EPS PCU
// testing board V4.1 (mainboard).
//
// BUILD TARGET: mainboard only (PB22 is not wired to an LED on the dev board)
//
// The mainboard wires PB22 to a 750-ohm series resistor (R52) and then to
// the LED2 anode; the cathode goes to GND. Driving PB22 HIGH lights the LED;
// driving PB22 LOW turns it off (active-HIGH).
//
// This is intentionally the smallest possible driver: it does not depend on
// any clock configuration, does not use SYNCBUSY, does not enable any
// peripheral other than the PORT block (which is bus-clock-enabled by the
// chip's reset state).
//
// Reference: docs/mainboard_pinout_pcu_v4_1.md (LED2 wiring section).
// =============================================================================

#ifndef LED_STATUS_PB22_ACTIVE_HIGH_ON_MAINBOARD_H
#define LED_STATUS_PB22_ACTIVE_HIGH_ON_MAINBOARD_H

// Configures PB22 as a push-pull GPIO output and drives it LOW so the LED
// starts in the OFF state. Must be called once at startup before any of the
// toggle / on / off functions below.
void configure_pb22_as_gpio_output_for_status_led_on_mainboard(void);

// Inverts the current logic level on PB22. Intended use: call this at a
// regular interval to make the LED blink visibly.
void toggle_pb22_status_led_on_mainboard(void);

// Forces PB22 HIGH, lighting the LED.
void turn_pb22_status_led_on_mainboard(void);

// Forces PB22 LOW, extinguishing the LED.
void turn_pb22_status_led_off_on_mainboard(void);

#endif // LED_STATUS_PB22_ACTIVE_HIGH_ON_MAINBOARD_H
