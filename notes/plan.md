# Development Plan

This is a high-level map of the development phases in order.
It is not a contract. Steps within a phase may shift once we have the hardware spec.
Each phase references the relevant doc file for implementation detail.

---

## Phase 0 — Toolchain, Project Structure, and Smoke Test (Windows)

**Goal:** Go from zero to a blinking LED + button toggle. Prove the entire chain
works: compiler, linker, flasher, startup code, GPIO.

### Status: COMPLETE

### What was done:
- Installed MSYS2, ARM toolchain (arm-none-eabi-gcc 12.2.1), OpenOCD (xPack 0.12.0),
  Make (4.4.1), PuTTY
- Downloaded fresh DFP v3.6.144 and CMSIS 5.9.1 files
- Created project structure: src/, startup/, lib/, docs/, notes/, code_samples/
- Wrote Makefile, openocd.cfg, syscalls_min.c, linker script
- Wrote LED blink + button toggle code
- Build: zero warnings, zero errors with -Wall -Wextra -Werror
- Flash: "Verified OK" from OpenOCD
- LED blinks at ~3s period, button toggles LED on/off

### Critical findings:
- DFP v3.6.144 register API uses `PORT_REGS->GROUP[1].PORT_DIRSET` (NOT the
  ASF3 `PORT->Group[1].DIRSET.reg` style shown in some online examples)
- `SystemInit()` in the DFP is a stub — sets SystemCoreClock=1000000 and returns.
  CPU runs at default 1 MHz (OSC8M/8), NOT 48 MHz
- Reset_Handler does NOT call SystemInit(). It calls `_on_reset()` (weak),
  `__libc_init_array()`, `_on_bootstrap()` (weak), then `main()`
- `-isystem` required for vendor include paths to suppress warnings from DFP headers

### Saved code: `code_samples/01_blink_and_button/`

Reference: `docs/toolchain_setup_windows.md`, `docs/project_structure.md`,
`docs/flashing.md`, `docs/smoke_test.md`, `docs/samd21_architecture.md`

---

## Phase 1 — 48 MHz Clock + DMA UART Logging (Debugger)

**Goal:** Have a working real-time debug system before writing any application logic.
This is the debugger — once it works, we can verify everything else (clock frequency,
peripheral configuration, algorithm behavior) through UART output.

### Status: COMPLETE

### What was done:
1. Configured 48 MHz clock (DFLL48M open-loop with NVM factory calibration)
2. Configured SERCOM5 as UART TX on **PA22** at 115200 baud
3. Configured DMAC channel 0 with SERCOM5_DMAC_ID_TX (12) as trigger
4. Implemented circular buffer (512 bytes) + DMA state machine + DMAC_Handler ISR
5. Verified: "BOOT OK" and "blink" messages appear on COM6 at 115200 baud
6. LED continues blinking (confirms DMA logging does not stall CPU)

### Bugs encountered and fixed:

**Bug 1 — AP stall from bad clock code (2026-04-01)**
Our first clock configuration attempt wrote to DFLLVAL while the DFLL ONDEMAND
bit was set. This triggered Errata 1.2.1 (DS80000760G) and froze the CPU. The
debug access port stalled, and OpenOCD could not connect. Recovery required
MPLAB X IDE → Production → Erase Device Memory Main Project. The fix: write
DFLLCTRL with ENABLE bit first (which clears ONDEMAND) before writing DFLLVAL.
Documented in `docs/how_to_recover_from_stalled_debug_port.md`.

**Bug 2 — Wrong TX pin: PB22 vs PA22**
Our documentation and initial code used PB22 as UART TX (SERCOM5 PAD[2], TXPO=1).
The UART appeared to work — DRE flag set correctly, code ran, LED blinked — but
zero bytes arrived on COM6. After extensive debugging and web research, we
discovered the DM320119 board actually wires:
- **PA22** → nEDBG UART RX (this is MCU TX, SERCOM5 PAD[0], mux D, TXPO=0)
- **PB22** → nEDBG UART TX (this is MCU RX, SERCOM5 PAD[2], mux D)
- **PB23 is NOT involved** in the CDC UART at all

The original PB22-as-TX assumption came from early documentation that was
incorrect. Sources confirming the fix: DM320119 User Guide DS70005409D,
Microchip AN3563, mircobytes UART tutorial for DM320119.

### Project structure change:
Driver modules moved from `src/` to `src/drivers/` to separate stable hardware
drivers from application code that changes frequently.

### Saved code: `code_samples/02_first_uart_communication/`

Reference: `docs/dma_uart_logging.md`, `docs/samd21_clocks.md`

---

## Phase 2 — MPPT Algorithm on Laptop

**Status: IN PROGRESS** — being implemented in a separate git worktree.
Do not touch this work from the main branch.

**Goal:** Implement and validate the core algorithm in pure C before touching
any hardware peripheral.

Steps:
1. Write `mppt_algorithm.c` with zero hardware dependencies
2. Write a solar panel I-V curve simulator in C
3. Feed simulated ADC values into the algorithm
4. Observe convergence to the maximum power point over iterations
5. Plot the result (CSV output → Excel or Python matplotlib)

Pass criterion: The algorithm converges to within 2% of the theoretical MPP
under multiple irradiance conditions.

Algorithm: **Incremental Conductance** (specified by CHESS mission document,
page 97/101). DC/DC topology is a **buck converter** (EPC2152 GaN half-bridge,
300 kHz, output 8V to battery bus). Buck relationship: V_out = D x V_in,
so higher duty cycle = lower panel voltage = more current drawn from panel.

Reference: `docs/mppt_algorithm.md`, CHESS satellite main doc (on Desktop)

---

## Phase 3 — TBD (Pending Hardware Specification)

This phase depends on the actual satellite hardware spec:
- Which sensors, on which interface (I2C / SPI / analog)
- How the switching frequency is controlled (PWM → which timer)
- What the ground communication link looks like (which SERCOM, which radio)

Once the spec is received, we define:
- Phase 3: Hardware drivers (ADC, PWM, sensors)
- Phase 4: Full closed loop (sensor → algorithm → actuator → sensor)
- Phase 5: Telemetry and command
- Phase 6: Sleep and power management
- Phase 7: Integration and endurance testing

---

## Non-Negotiable Rules Across All Phases

- Every phase has a pass criterion. We do not move forward until it passes.
- We never add two new things at once. If something breaks, one change caused it.
- The main branch always compiles with zero warnings.
- Pure logic functions are tested on the laptop before the chip.
- `conventions.md` applies to every line of every file, always.
