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
| [`docs/chips_protocol_framing_and_crc.md`](docs/chips_protocol_framing_and_crc.md) | CHIPS protocol: frame format, byte stuffing, CRC-16/KERMIT, parser state machine, verification tests |
| [`docs/chips_command_interface.md`](docs/chips_command_interface.md) | EPS command table, telemetry struct, status codes, idempotency, OBC timeout — share with OBC team |
| [`docs/millisecond_tick_timer_using_arm_systick.md`](docs/millisecond_tick_timer_using_arm_systick.md) | Millisecond tick timer: what ARM SysTick is, why we need it, register config, ISR, API |

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

### Millisecond Tick Timer — `millisecond_tick_timer_using_arm_systick.c/.h`

Hardware driver for the ARM Cortex-M0+ SysTick core peripheral (built into every
ARM core, not a SAMD21-specific peripheral). Configures SysTick to fire an
interrupt every 1 millisecond at 48 MHz, incrementing a `volatile uint32_t`
counter. Provides accurate elapsed time measurement without blocking the CPU.

Used for: OBC communication timeout detection (120 seconds), non-blocking LED
heartbeat toggling (500ms). Replaces the blocking busy-wait delay from Phase 3.

See `docs/millisecond_tick_timer_using_arm_systick.md` for details.

### CHIPS Frame Codec — `chips_protocol_encode_decode_frames_with_crc16_kermit.c/.h`

**PURE LOGIC** — no hardware access, testable on a laptop without a microcontroller.

Implements the CHIPS (CHESS Internal Protocol over Serial) framing layer:
- **CRC-16/KERMIT** with 256-entry lookup table (512 bytes flash)
- **PPP-style byte stuffing** (0x7E → 0x7D 0x5E, 0x7D → 0x7D 0x5D)
- **Streaming frame parser**: state machine, processes one byte at a time from
  the UART receive buffer, handles back-to-back sync bytes (RFC 1662)
- **Frame builder**: takes command fields + payload, produces complete wire-ready
  frame with CRC and byte stuffing

Includes boot self-tests: CRC test vector ("123456789" → 0x2189) and frame
build-then-parse round-trip verification.

See `docs/chips_protocol_framing_and_crc.md` for the complete protocol reference.

### CHIPS Command Dispatch — `chips_protocol_dispatch_commands_and_build_responses.c/.h`

Application-level command handler that sits above the frame codec. Located in
`src/` (not `src/drivers/`) because it is application logic, not a hardware driver.

- Receives parsed CHIPS frames from the frame parser
- Dispatches to per-command handler functions (GET_TELEMETRY, SET_PARAMETER, etc.)
- Builds response frames with status byte + payload
- Tracks **idempotency**: caches the last response and re-sends it if a duplicate
  sequence number arrives, without re-executing the command
- Contains the **telemetry struct** (219 bytes, placeholder zeros until Phase 6)

See `docs/chips_command_interface.md` for the command table and telemetry structure.

### Assertion Handler — `assertion_handler.c/.h`

Utility module providing the `SATELLITE_ASSERT` macro per conventions.md (NASA
Power of 10 Rule C5). Located in `src/` (not `src/drivers/`).

- Debug build: logs file name and line number to debug UART, then freezes
- Flight build: triggers `NVIC_SystemReset()` for immediate recovery

Every function longer than 10 lines must have at least two assertions (one
precondition, one postcondition). Assertions remain active in flight builds.

---

## Status

Phase 0 (toolchain + smoke test), Phase 1 (48 MHz clock + DMA UART logging),
Phase 3 (ESP32 test harness + bidirectional OBC UART), and Phase 4 (CHIPS protocol)
are complete. Phase 2 (MPPT algorithm) is in progress in a separate git worktree.

Phase 4 added the CHIPS communication protocol: HDLC-style framing with
CRC-16/KERMIT checksums, PPP byte stuffing, a streaming frame parser, 6 command
handlers with idempotency tracking, and a 120-second OBC autonomy timeout.
Telemetry data is placeholder zeros — real sensor values come in Phase 6.

An ESP32 test harness (`esp32_test_harness/02_chips_protocol_test/`) implements
the OBC side and runs 9 automated test cases covering all protocol features.

See `plan.md` for current phase and next steps.
