# Satellite Firmware — Project Documentation

This repository contains bare-metal C firmware for a satellite power management
system built on the SAMD21G17D microcontroller (Cortex-M0+, 128KB flash, 16KB RAM),
running on the Microchip Curiosity Nano DM320119 development board.

The firmware implements an MPPT (Maximum Power Point Tracking) algorithm for solar
panel power management, with DMA-based non-blocking UART logging for development
debugging.

---

## Document Index

### Conventions and Standards

- [`conventions.md`](conventions.md) — Mandatory coding standards for all firmware.
  Based on NASA Power of 10, JPL D-60411, and MISRA C:2004. Every file, function,
  and variable must comply. Read this before writing any code.

### Plan

- [`plan.md`](plan.md) — High-level development phases and milestones.
  Not prescriptive — a map, not a contract. Updated as the project evolves.

### **START HERE — Build and Flash Reference**

- **[`docs/how_to_build_and_flash.md`](docs/how_to_build_and_flash.md)** —
  Complete, verified reference for compiling and flashing code to the SAMD21G17D.
  Contains exact commands, file layout, every compiler flag explained, the
  correct register API style, startup sequence, common errors and fixes,
  and vendor file download instructions. **Read this first if you need to
  build or flash anything.**

### Technical Documentation (`docs/`)

| File | What it covers |
|---|---|
| [`docs/how_to_build_and_flash.md`](docs/how_to_build_and_flash.md) | **ESSENTIAL** — Complete build and flash reference, verified working |
| [`docs/how_to_recover_from_stalled_debug_port.md`](docs/how_to_recover_from_stalled_debug_port.md) | **EMERGENCY** — If OpenOCD says "stalled AP operation", read this to recover the board using MPLAB X IDE |
| [`docs/toolchain_setup_windows.md`](docs/toolchain_setup_windows.md) | Installing compiler, flasher, Make, and serial terminal on Windows |
| [`docs/project_structure.md`](docs/project_structure.md) | File layout, Makefile explained, build targets, DFP files |
| [`docs/flashing.md`](docs/flashing.md) | How OpenOCD works, openocd.cfg, flash commands, common errors |
| [`docs/samd21_architecture.md`](docs/samd21_architecture.md) | Chip overview, memory map, startup sequence, nEDBG, SWD |
| [`docs/samd21_clocks.md`](docs/samd21_clocks.md) | Clock sources, GCLK system, 48MHz configuration, SYNCBUSY |
| [`docs/smoke_test.md`](docs/smoke_test.md) | LED blink — the first program, why it matters, pass criteria |
| [`docs/dma_uart_logging.md`](docs/dma_uart_logging.md) | DMA logging system design, implementation, Windows terminal setup |
| [`docs/uart_obc_driver.md`](docs/uart_obc_driver.md) | OBC UART driver: pin selection, interrupt architecture, timing analysis, debugging tips |
| [`docs/mppt_algorithm.md`](docs/mppt_algorithm.md) | P&O vs IncCond, DC/DC dependency, laptop simulation approach |
| [`docs/newlib_and_syscalls.md`](docs/newlib_and_syscalls.md) | Why syscalls_min.c exists, newlib dependency chain, prototype fixes |
| [`docs/pwm_buck_converter_driver.md`](docs/pwm_buck_converter_driver.md) | **Phase 5** — TCC0 complementary PWM with dead-time: register config, pin selection, DTI explanation, four-level testing, what was NOT verified |

---

## Hardware Reference

| Item | Value |
|---|---|
| Chip | SAMD21G17D |
| Architecture | ARM Cortex-M0+ |
| Flash | 128 KB at 0x00000000 |
| RAM | 16 KB at 0x20000000 |
| Max CPU clock | 48 MHz (DFLL48M) |
| DMA channels | 12 |
| SERCOM blocks | 6 (SERCOM0–SERCOM5) |
| Board | Curiosity Nano DM320119 |
| Debugger chip | nEDBG (CMSIS-DAP, VID 0x03eb PID 0x2175) |
| Virtual COM port | SERCOM5, PA22 (TX), PB22 (RX) |
| SWD pins | PA30 (SWCLK), PA31 (SWDIO) |
| User LED | PB10 (active low) |
| User button | PB11 |

---

## Key Design Decisions

- **No bootloader.** Code links at 0x00000000. Full 128KB flash available.
- **No RTOS.** Single superloop with DMA and ISRs for background work.
- **No dynamic memory.** All allocation is static at compile time.
- **No HAL.** Direct register access only. Faster, more auditable.
- **DMA UART logging.** Non-blocking. ~1 microsecond CPU cost per log call.
  Logging compiles to zero bytes in flight builds.

---

## Driver Modules (`src/drivers/`)

Hardware driver modules live in `src/drivers/` and are separate from application
code (`src/main.c`). Each driver encapsulates one hardware subsystem and exposes
a small public API through its header file.

### Clock — `clock_configure_48mhz_dfll_open_loop.c/.h`

Switches the CPU clock from the default 1 MHz (OSC8M/8) to 48 MHz using the
DFLL48M oscillator in open-loop mode. The Curiosity Nano has no external crystal,
so we use factory calibration values burned into the chip's OTP fuse area for
~2% frequency accuracy. This must be called before any peripheral that depends
on a specific clock frequency (UART baud rate, timer periods, etc.).

Includes a workaround for Errata 1.2.1 (DS80000760G): the DFLL ONDEMAND bit
must be cleared by writing DFLLCTRL with ENABLE before writing DFLLVAL, or the
device freezes permanently. See `docs/samd21_clocks.md` for full details.

### Debug Logging — `debug_functions.c/.h`

Non-blocking DMA-based debug logging over SERCOM5 UART at 115200 baud. The CPU
writes log messages into a 512-byte circular buffer (~1 microsecond per call),
and DMAC channel 0 drains the buffer in the background with zero CPU involvement.
When the DMA transfer completes, the DMAC_Handler ISR starts a new transfer if
more data has accumulated, or goes idle.

The TX pin is **PA22** (SERCOM5 PAD[0], mux D, TXPO=0), which connects to the
nEDBG UART RX on the DM320119 board and appears as a virtual COM port on the PC.

In flight builds (without `-DDEBUG_LOGGING_ENABLED`), all logging macros compile
to `((void)0)` — zero code, zero RAM, zero CPU cost. See `docs/dma_uart_logging.md`
for the full design and implementation details.

### OBC UART Interface — `uart_obc_sercom0_pa04_pa05.c/.h`

Non-blocking bidirectional UART for communication with the OBC (or ESP32 test
harness). Both transmit and receive are interrupt-driven using 256-byte ring buffers.
The CPU writes bytes to a RAM buffer and returns immediately; the SERCOM0 interrupt
handler (SERCOM0_Handler) moves bytes between the buffers and the hardware in the
background.

- **TX pin:** PA04 (SERCOM0 PAD[0], mux D, TXPO=0)
- **RX pin:** PA05 (SERCOM0 PAD[1], mux D, RXPO=1)
- **Baud:** 115200 (same as debug UART)
- **CPU overhead:** ~0.6 microseconds per byte (~0.7% at max sustained throughput)

The driver does NOT use DMA (to avoid sharing the DMAC_Handler with debug_functions.c).
Instead, the DRE (Data Register Empty) interrupt triggers per transmitted byte, and
the RXC (Receive Complete) interrupt triggers per received byte. Both are handled in
the same SERCOM0_Handler function.

**Critical RX pin detail:** PA05 requires INEN (input enable) in PINCFG. Without it,
the pin's input buffer is disabled and the SERCOM cannot read the voltage level.
This is the most common RX configuration bug on the SAMD21.

See `docs/uart_obc_driver.md` for the complete technical reference including pin
selection rationale, timing analysis, register details, and debugging tips.

### PWM Buck Converter — `driver_For_Generating_PWM_for_Buck_Converter.c/.h`

Complementary PWM at 300 kHz with hardware dead-time insertion on TCC0, for driving
the EPC2152 GaN half-bridge buck converter. Two output pins generate synchronized,
opposite-phase switching signals with a ~42 ns safety gap at every transition.

- **WO[2] pin:** PA18 (TCC0 WO[2], mux F) — high-side FET drive → EPC2152 HSin
- **WO[6] pin:** PA20 (TCC0 WO[6], mux F) — low-side FET drive → EPC2152 LSin
- **Frequency:** 300 kHz (PER = 159, GCLK0 = 48 MHz, no prescaler)
- **Dead time:** 42 ns per transition (DTLS = DTHS = 2 GCLK counts)
- **DTI generator:** DTIEN2 (channel 2 pair: WO[2] + WO[6])
- **Duty cycle input:** 0-65535 (maps to CC[2] 0-159, clamped to 5%-94%)
- **Fault safety:** DRVCTRL forces both outputs LOW on hardware fault

Dead time is enforced in silicon by TCC0's DTI unit — no software bug can cause
shoot-through. Verified with four-level testing: register readback, pin state sampling,
ESP32 frequency measurement (293 kHz), and ESP32 complementary check (0 violations
across 4 million samples).

See `docs/pwm_buck_converter_driver.md` for the complete technical reference including
register details, pin selection rationale, DTI explanation, testing methodology, and
what was NOT verified.

### Assertion Handler — `assertion_handler.h/.c`

Implements the `SATELLITE_ASSERT()` macro required by conventions.md Rule C5. When
an assertion fails:
- **Debug build:** prints file name and line number to debug UART, then freezes
  (watchdog will reset the chip).
- **Flight build:** immediately resets the chip via `NVIC_SystemReset()`.

Assertions remain active in flight builds as an early warning system for impossible
states (memory corruption, hardware behaving outside specification).

---

## Status

Phase 0 (toolchain + smoke test), Phase 1 (48 MHz clock + DMA UART logging),
Phase 3 (ESP32 test harness + bidirectional OBC UART), and Phase 5 (complementary
PWM with dead-time) are complete. Phase 2 (MPPT algorithm) is in progress in a
separate git worktree.

The debug logging system (SERCOM5/PA22) and OBC UART (SERCOM0/PA04-PA05) are both
verified working simultaneously at 115200 baud. The TCC0 complementary PWM (PA18/PA20)
generates 293 kHz switching signals with hardware dead-time insertion, verified by
both SAMD21 self-tests and independent ESP32 measurement.

See `plan.md` for current phase and next steps.
