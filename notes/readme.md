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
| [`docs/mainboard_pinout_pcu_v4_1.md`](docs/mainboard_pinout_pcu_v4_1.md) | **MAINBOARD** — full proof-backed pin map of the PCU testing board V4.1 (the actual EPS PCB). Chip is `ATSAMD21J17D-MUT` (64-pin QFN), not the dev board's `SAMD21G17D`. Read before porting any firmware to the mainboard. |
| [`docs/build_targets_and_file_map.md`](docs/build_targets_and_file_map.md) | **CANONICAL FILE MAP** — every file in the repo classified as devboard-only, mainboard-only, or shared. How to tell which is which, what the Makefile does, what changes when adding a new file. Read this if you are confused about which code applies to your board. |

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

### Mainboard Status LED — `led_status_pb22_active_high_on_mainboard.c/.h`

GPIO-only driver for the green status LED (LED2) on the EPS PCU testing board V4.1
(mainboard PCB). LED2 is wired between PB22 and GND through a 750Ω series resistor,
so driving PB22 HIGH lights the LED, LOW turns it off (active-HIGH). This driver only
exists in the mainboard build.

See [`docs/mainboard_pinout_pcu_v4_1.md`](docs/mainboard_pinout_pcu_v4_1.md) for the
proof-backed pin map of the mainboard.

---

## Build Targets

This project builds firmware for two physically different boards from the
**same source tree**. Each `make` invocation must say which board, **there
is no default** (a default caused confusion in early bring-up and was
removed). The `make clean` target is the one exception — it works without
specifying a board.

| Command | Board | Chip | What gets compiled |
|---|---|---|---|
| `make BOARD=devboard` | Curiosity Nano DM320119 dev board | SAMD21G17D (48-pin TQFP) | `src/main.c` — debugger stress test (LED + button + UART echo) |
| `make BOARD=mainboard` | EPS PCU testing board V4.1 (real EPS PCB) | SAMD21J17D-MUT (64-pin QFN) | `src/main_mainboard_blink_pb22.c` — Firmware A: PB22 LED blink |
| `make clean` | (any) | (any) | Wipes `build/`. No `BOARD` needed. |
| `make BOARD=devboard flash` | Curiosity Nano via on-board nEDBG | SAMD21G17D | builds + flashes the dev board |
| `make BOARD=mainboard flash` | Mainboard via Atmel-ICE on Tag-Connect J1 | SAMD21J17D-MUT | builds + flashes the mainboard |

**Always run `make clean` when switching the `BOARD` variable.** Object files
go into a single `build/` directory; reusing them across chip variants would
silently produce a broken binary.

### How to tell which file is for which board

Three reinforcing layers. If they ever disagree, the Makefile wins.

1. **The Makefile (executable source of truth).** The `ifeq ($(BOARD),...)`
   block lists exactly which `.c` files compile for each board. If a file is
   listed under `devboard`, it goes only into the dev-board binary. If it's
   listed under `mainboard`, it goes only into the mainboard binary. Files
   listed under both are shared.
2. **The file name.** Driver names like `uart_obc_sercom0_pa04_pa05.c` say
   exactly which pins the driver uses. Files ending in `_on_mainboard` are
   mainboard-specific by convention.
3. **The header comment** at the top of every project `.c` and `.h` file
   contains a `BUILD TARGET:` line that says either `devboard only`,
   `mainboard only`, or `shared`. This is a quick visual confirmation when
   you open a file.

For the full file-by-file breakdown — every source file, header, vendor file,
linker script, classified — see
[`docs/build_targets_and_file_map.md`](docs/build_targets_and_file_map.md).

### Why this works without a refactor

The two chips (G17D and J17D) are the same SAMD21 silicon die in different
packages. Same memory map, same peripherals, same registers. The build
differences reduce to (a) the `__SAMD21G17D__` vs `__SAMD21J17D__` chip-name
define, (b) the matching startup file, (c) the matching linker script, and
(d) the application-level files that pick the right pins for the right
board. All driver register code (PORT, SERCOM, TCC, GCLK, NVMCTRL, DMAC) is
identical between the two — see
[`docs/mainboard_pinout_pcu_v4_1.md`](docs/mainboard_pinout_pcu_v4_1.md) and
[`docs/build_targets_and_file_map.md`](docs/build_targets_and_file_map.md)
for the proof.

---

## Status

Phase 0 (toolchain + smoke test), Phase 1 (48 MHz clock + DMA UART logging), and
Phase 3 (ESP32 test harness + bidirectional OBC UART) are complete. Phase 2 (MPPT
algorithm) is in progress in a separate git worktree.

The debug logging system (SERCOM5/PA22) and OBC UART (SERCOM0/PA04-PA05) are both
verified working simultaneously at 115200 baud. The ESP32 test harness sends PING
messages and the SAMD21 echoes them back with zero data loss.

See `plan.md` for current phase and next steps.
