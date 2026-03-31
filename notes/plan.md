# Development Plan

This is a high-level map of the development phases in order.
It is not a contract. Steps within a phase may shift once we have the hardware spec.
Each phase references the relevant doc file for implementation detail.

---

## Phase 0 — Toolchain and Environment (Windows)

**Goal:** Be able to compile a C file and flash it to the chip.
Nothing else matters until this works.

Steps:
1. Install MSYS2, ARM toolchain, OpenOCD, Make, PuTTY
2. Download Microchip DFP (device support files for SAMD21G17D)
3. Download ARM CMSIS headers
4. Set up project file structure
5. Write Makefile
6. `make` produces a `.bin` with zero warnings and zero errors
7. `make flash` produces "verified OK" from OpenOCD
8. PuTTY opens the COM port without error

Pass criterion: Step 7 and 8 both succeed on a blank main.c that does nothing.

Reference: `docs/toolchain_setup_windows.md`, `docs/project_structure.md`, `docs/flashing.md`

---

## Phase 1 — Smoke Test

**Goal:** Confirm the entire chain works — compiler, linker, flasher, chip, clock.

Steps:
1. Write a minimal `main.c` that blinks the LED on PB10
2. Flash it
3. Observe the LED blinking

Pass criterion: LED blinks at a predictable rate. If it blinks, Reset_Handler ran,
SystemInit ran, your code ran, the chip is alive.

Reference: `docs/smoke_test.md`, `docs/samd21_architecture.md`

---

## Phase 2 — DMA UART Logging

**Goal:** Have a working real-time debug system before writing any application logic.

Steps:
1. Configure SERCOM5 as UART TX on PB22 at 115200 baud
2. Configure DMAC channel 0 with SERCOM5_DMAC_ID_TX as trigger
3. Implement the circular buffer + DMA state machine
4. Call `LOG("BOOT OK\r\n")` in main
5. Open PuTTY on the correct COM port
6. See the message appear

Pass criterion: Log messages appear in PuTTY in real time. Chip does not stall.
CPU cost verified to be negligible (MPPT loop timing unaffected).

Reference: `docs/dma_uart_logging.md`, `docs/samd21_clocks.md`

---

## Phase 3 — MPPT Algorithm on Laptop

**Goal:** Implement and validate the core algorithm in pure C before touching
any hardware peripheral.

Steps:
1. Write `mppt_algorithm.c` with zero hardware dependencies
2. Write a solar panel I-V curve simulator in C
3. Feed simulated ADC values into the algorithm
4. Observe convergence to the maximum power point over iterations
5. Plot the result (CSV output → Excel or Python matplotlib)

Pass criterion: The algorithm converges to within 2% of the theoretical MPP
under multiple irradiance conditions. Algorithm choice (P&O vs IncCond) is
confirmed once DC/DC topology is known.

Reference: `docs/mppt_algorithm.md`

---

## Phase 4 — TBD (Pending Hardware Specification)

This phase depends on the actual satellite hardware spec:
- Which sensors, on which interface (I2C / SPI / analog)
- Which DC/DC converter topology (boost / buck / SEPIC)
- How the switching frequency is controlled (PWM → which timer)
- What the ground communication link looks like (which SERCOM, which radio)

Once the spec is received, we define:
- Phase 4: Hardware drivers (ADC, PWM, sensors)
- Phase 5: Full closed loop (sensor → algorithm → actuator → sensor)
- Phase 6: Telemetry and command
- Phase 7: Sleep and power management
- Phase 8: Integration and endurance testing

---

## Non-Negotiable Rules Across All Phases

- Every phase has a pass criterion. We do not move forward until it passes.
- We never add two new things at once. If something breaks, one change caused it.
- The main branch always compiles with zero warnings.
- Pure logic functions are tested on the laptop before the chip.
- `conventions.md` applies to every line of every file, always.
