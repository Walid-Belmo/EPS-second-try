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
| [`docs/toolchain_setup_windows.md`](docs/toolchain_setup_windows.md) | Installing compiler, flasher, Make, and serial terminal on Windows |
| [`docs/project_structure.md`](docs/project_structure.md) | File layout, Makefile explained, build targets, DFP files |
| [`docs/flashing.md`](docs/flashing.md) | How OpenOCD works, openocd.cfg, flash commands, common errors |
| [`docs/samd21_architecture.md`](docs/samd21_architecture.md) | Chip overview, memory map, startup sequence, nEDBG, SWD |
| [`docs/samd21_clocks.md`](docs/samd21_clocks.md) | Clock sources, GCLK system, 48MHz configuration, SYNCBUSY |
| [`docs/smoke_test.md`](docs/smoke_test.md) | LED blink — the first program, why it matters, pass criteria |
| [`docs/dma_uart_logging.md`](docs/dma_uart_logging.md) | DMA logging system design, implementation, Windows terminal setup |
| [`docs/mppt_algorithm.md`](docs/mppt_algorithm.md) | P&O vs IncCond, DC/DC dependency, laptop simulation approach |
| [`docs/simulation_of_MPPT.md`](docs/simulation_of_MPPT.md) | Detailed MPPT simulation reference |
| [`docs/eps_state_machine_overview.md`](docs/eps_state_machine_overview.md) | **EPS State Machine** — Complete explanation from first principles with mission doc sources |
| [`docs/eps_simulation.md`](docs/eps_simulation.md) | **EPS Simulation** — Closed-loop physics, battery model, timing, all 8 scenarios |
| [`docs/newlib_and_syscalls.md`](docs/newlib_and_syscalls.md) | Why syscalls_min.c exists, newlib dependency chain, prototype fixes |

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
| Virtual COM port | SERCOM5, PB22 (TX), PB23 (RX) |
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

## Status

Project is in initial setup phase. Toolchain must be installed and verified
before any code is written. See `plan.md` for current phase.
