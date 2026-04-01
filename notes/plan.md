# Development Plan

This is a high-level map of the development phases in order.
It is not a contract. Steps within a phase may shift once we have the hardware spec.
Each phase references the relevant doc file for implementation detail.

---

## Phase 0 — Toolchain, Project Structure, and Smoke Test (Windows)

**Goal:** Go from zero to a blinking LED + button toggle. Prove the entire chain
works: compiler, linker, flasher, startup code, GPIO.

### Status: IN PROGRESS

### What is already done:
- MSYS2, ARM toolchain (arm-none-eabi-gcc 12.2.1), OpenOCD (xPack 0.12.0),
  Make (4.4.1), PuTTY — all installed and on PATH
- All online sources and approaches verified (25 sources checked)

### IMPORTANT — Vendor files must be downloaded fresh:
A previous Claude Code instance may have modified local copies of the DFP and
CMSIS files at `~/.mchp_packs/` and `~/.mcc/harmony/`. These CANNOT be trusted.
All vendor files must be downloaded fresh from their official sources:
- **DFP:** Download `.atpack` from https://packs.download.microchip.com/ (SAMD21 DFP)
- **CMSIS:** Download from https://github.com/ARM-software/CMSIS_5 (raw files)
Delete any existing copies in the project tree before extracting fresh ones.

### What remains:
1. Download and extract fresh DFP files into lib/samd21-dfp/ and startup/
2. Download fresh CMSIS headers into lib/cmsis/
3. Copy linker script to project root (samd21g17d_flash.ld)
4. Create project directory structure (src/, tests/, build/)
5. Write openocd.cfg
6. Write syscalls_min.c (created but not in Makefile SRCS — using nosys.specs)
7. Write Makefile (corrected for actual DFP filenames and register API)
8. Write src/main.c — LED blink (~3s period) + button toggle on PB11
9. `make` produces zero warnings and zero errors
10. `make flash` produces "verified OK" from OpenOCD
11. Observe: LED blinks with ~3s period, button toggles LED

### Critical findings during planning:
- The DFP v3.6.144 uses a **different register API** from the documentation
  examples. Documentation shows `PORT->Group[1].DIRSET.reg` (ASF3 style).
  The actual DFP uses `PORT_REGS->GROUP[1].PORT_DIRSET`. All code must use
  the actual DFP API. (Verified by reading the actual component/port.h header)
- The DFP's `SystemInit()` is a **stub that does nothing**. The CPU runs at
  the default 1 MHz (OSC8M/8), NOT 48 MHz. (Verified by reading
  system_samd21g17d.c — it just sets SystemCoreClock=1000000 and returns)
- The DFP's `Reset_Handler` does NOT call `SystemInit()`. It calls
  `_on_reset()` (weak), `__libc_init_array()`, `_on_bootstrap()` (weak),
  then `main()`. (Verified by reading startup_samd21g17d.c)
- The DFP subdirectory for G17D is `samd21d/`, NOT `samd21a/` as docs state.
- Startup files are named `startup_samd21g17d.c` and `system_samd21g17d.c`,
  NOT `startup_samd21.c` and `system_samd21.c`.
- The `-pedantic` flag in conventions.md will cause errors on DFP headers
  (GCC extensions). Use `-isystem` for vendor include paths to suppress.

### Note on 48 MHz clock:
The CPU starts at 1 MHz (OSC8M/8 default). Getting to 48 MHz requires writing
clock configuration code ourselves (DFLL48M setup). This will be done AFTER
the UART debugger is working so we can verify the clock frequency empirically
by logging timing measurements.

Pass criterion:
1. LED blinks with ~3 second period (distinguishable from previous code on board)
2. Button press on PB11 toggles LED on/off
3. `make` produces zero warnings
4. `make flash` produces "verified OK"

Reference: `docs/toolchain_setup_windows.md`, `docs/project_structure.md`,
`docs/flashing.md`, `docs/smoke_test.md`, `docs/samd21_architecture.md`

---

## Phase 1 — DMA UART Logging (Debugger)

**Goal:** Have a working real-time debug system before writing any application logic.
This is the debugger — once it works, we can verify everything else (clock frequency,
peripheral configuration, algorithm behavior) through UART output.

Steps:
1. Configure 48 MHz clock (DFLL48M) — required for correct UART baud rate
2. Configure SERCOM5 as UART TX on PB22 at 115200 baud
3. Configure DMAC channel 0 with SERCOM5_DMAC_ID_TX as trigger
4. Implement the circular buffer + DMA state machine
5. Call `LOG("BOOT OK\r\n")` in main
6. Open PuTTY on the correct COM port
7. See the message appear
8. Verify clock frequency empirically: log a timestamp before and after
   a known delay, compare to wall-clock time

Pass criterion: Log messages appear in PuTTY in real time. Chip does not stall.
Clock frequency confirmed to be 48 MHz.

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

## Documentation Update (Deferred)

All documentation files (readme.md and everything in docs/) must be updated
to match the actual DFP v3.6.144 behavior discovered during Phase 0 planning.
Key corrections needed:
- Register API style (PORT_REGS->GROUP[] not PORT->Group[])
- SystemInit() is a stub, not a 48 MHz configurator
- Reset_Handler does not call SystemInit()
- Correct DFP filenames and directory structure
- Sources for each correction

This is deferred until after Phase 0 execution. Do not touch conventions.md.

---

## Non-Negotiable Rules Across All Phases

- Every phase has a pass criterion. We do not move forward until it passes.
- We never add two new things at once. If something breaks, one change caused it.
- The main branch always compiles with zero warnings.
- Pure logic functions are tested on the laptop before the chip.
- `conventions.md` applies to every line of every file, always.
