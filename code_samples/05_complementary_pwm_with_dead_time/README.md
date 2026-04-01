# Code Sample 05 — Complementary PWM with Dead-Time (Phase 5)

Snapshot of the working Phase 5 implementation: TCC0 complementary PWM at 300 kHz
with hardware dead-time insertion for the EPC2152 GaN half-bridge buck converter.

## What This Code Does

- Configures TCC0 for NPWM mode at 300 kHz (PER = 159, GCLK0 = 48 MHz)
- Enables Dead-Time Insertion on channel 2 (DTIEN2): WO[2] + WO[6] pair
- Dead time: DTLS = DTHS = 2 counts = ~42 ns
- Outputs: PA18 (WO[2], high-side drive), PA20 (WO[6], low-side drive)
- Fault-safe: DRVCTRL forces both outputs LOW on hardware fault
- Self-tests at boot: register readback, pin state sampling (complementary check)
- Sets 50% duty cycle for ESP32 external verification

## Files

| File | Purpose |
|------|---------|
| `driver_For_Generating_PWM_for_Buck_Converter.h` | Public API: init + set duty cycle |
| `driver_For_Generating_PWM_for_Buck_Converter.c` | TCC0 register configuration |
| `assertion_handler.h` | SATELLITE_ASSERT macro |
| `assertion_handler.c` | Assert failure handler (debug: log+halt, flight: reset) |
| `main.c` | Application: PWM init, self-tests, heartbeat loop |
| `02_pwm_verification.ino` | ESP32 test sketch: frequency, complementary, duty cycle |

## Verified Test Results

| Test | Result |
|------|--------|
| Build | Zero warnings with -Wall -Wextra -Werror |
| WEXCTRL readback | 0x02020400 = DTIEN2 + DTLS=2 + DTHS=2 |
| PER readback | 159 |
| WAVE readback | 0x2 = NPWM |
| SAMD21 pin state: PA18 HIGH samples | 4405/10000 (44%) |
| SAMD21 pin state: PA20 HIGH samples | 5357/10000 (54%) |
| SAMD21 pin state: both HIGH violations | 0 |
| ESP32 frequency WO[2] | 292,887 Hz (PASS, within 5% of 300 kHz) |
| ESP32 frequency WO[6] | 292,867 Hz (PASS) |
| ESP32 complementary: violations | 0 out of 4,000,000 samples (two runs) |
| ESP32 complementary: dead time | 0.5-0.6% of samples (both LOW) |
| ESP32 duty cycle WO[2] | 53.6% (expected ~50%, offset due to DTI effect) |

## Wiring for ESP32 Tests

```
SAMD21 PA18 --> ESP32 GPIO 34
SAMD21 PA20 --> ESP32 GPIO 35
SAMD21 GND  --> ESP32 GND
```

## Full Documentation

See `docs/pwm_buck_converter_driver.md` for the complete technical reference.
