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

**Status: IMPLEMENTED IN SEPARATE WORKTREE, NOT MERGED INTO MASTER.**
The `mppt-algorithm` worktree contains the pure Incremental Conductance
algorithm and simulation support. The code is intended to be merged into
the main firmware after the board-specific UART/PWM/injection path exists.

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

## Requirements Traceability (from CHESS Mission Document Research)

Every requirement below has a verified source. See `research_logs/` for detailed findings.

| ID | Requirement | Source | Phase |
|---|---|---|---|
| R1 | CHIPS protocol: HDLC framing, CRC-16/KERMIT, byte stuffing | PDF p.125-127 | 4 |
| R2 | EPS telemetry: 10 fields, ~210 bytes | PDF p.35-36 | 4 |
| R3 | Complementary PWM with dead-time on TCC0 for EPC2152 at 300kHz | PDF p.96, SAMD21 DS p.616+ | 5 |
| R4 | INA226 I2C driver for voltage/current sensing | PDF p.95,108 | 6 |
| R5 | ADC driver for analog sensors (LT6108, voltage dividers) | PDF p.95-98 | 6 |
| R6 | MPPT IncCond algorithm runs on SAMD21 with closed-loop feedback | PDF p.97/101 | 7 |
| R7 | State machine: Charging, Measurement, Communication, Safe (4 sub-states) | PDF p.20-25 | 8 |
| R8 | Load shedding via eFuse enable pins (GPIO) | PDF p.103-104 | 8 |
| R9 | Heater control (GPIO/PWM to MOSFET) | PDF p.95 | 8 |
| R10 | Battery temp monitoring (SPI — part unknown) | PDF p.95 | 6 |
| R11 | Watchdog timer for radiation mitigation | PDF p.106-107 | 8 |
| R12 | 120s autonomy timeout — EPS acts alone if OBC silent | PDF p.127 | 8 |
| R13 | EPS boots first, manages power before OBC is up | PDF p.14-15,26-27 | 8 |
| R14 | LEOP: battery-only operation, solar panels stowed 30+ min | PDF p.15,26-28 | 8 |
| R15 | Antenna deployment burn wire: 5V 250mA with retry | PDF p.218-219 | 8 |
| R16 | Runtime parameter updates via UART (thresholds etc.) | PDF p.34, Table 2.1.1 | 4 |
| R17 | OBC-EPS interface changed from I2C to UART | Team decision | 3 |
| R18 | Second SERCOM needed for OBC UART (SERCOM5 = debug) | Architecture constraint | 3 |

---

## Phase 3 — ESP32 Test Harness + Second UART (OBC Interface)

**Goal:** Establish bidirectional UART between the SAMD21 and an ESP32 dev board.
The ESP32 will act as our universal test tool for all subsequent phases: it will
simulate the OBC, sensors, and physics models. Nothing else can be tested until
this works.

**Status: COMPLETE**

### What was done

1. Selected **SERCOM0** on **PA04** (TX, PAD[0], mux D) and **PA05** (RX, PAD[1], mux D)
2. Created `src/drivers/uart_obc_sercom0_pa04_pa05.c` and `.h`
3. Implemented **fully non-blocking interrupt-driven TX and RX** (no DMA, no blocking)
4. Created ESP32 Arduino test sketch: `esp32_test_harness/01_uart_echo_test/`
5. Verified bidirectional PING/echo between SAMD21 and ESP32
6. Both debug UART (SERCOM5/PA22) and OBC UART (SERCOM0/PA04-PA05) work simultaneously

### SERCOM and pin selection rationale

**SERCOM0 on PA04 (TX) / PA05 (RX)** was chosen after verifying:
- DM320119 User Guide Table 4-4 (p.13): PA04/PA05 have NO debugger connections
- DM320119 User Guide Section 4.1.1 (p.11): both pins on edge headers
- SAMD21 Datasheet Table 7-1 (p.29): PA04=SERCOM0/PAD[0] mux D, PA05=SERCOM0/PAD[1] mux D
- No conflict with SERCOM5 (debug), TCC0 (PWM Phase 5 can use PA18/PA20 for DTI ch2),
  SERCOM3 (I2C Phase 6 on PA16/PA17), or SERCOM4 (SPI Phase 6 on PA12-PA15)

### Deviations from the original plan

1. **File names changed.** Plan said `uart_obc_interface.c/.h`. Actual files are
   `uart_obc_sercom0_pa04_pa05.c/.h` — per conventions.md, file names must describe
   exactly what hardware they use.

2. **TX is non-blocking (interrupt-driven), not blocking.** The original plan said
   `uart_obc_send_bytes()` would be blocking (poll DRE flag per byte). Analysis showed
   blocking TX would waste 18 ms of CPU time for a 210-byte CHIPS telemetry response.
   Instead, both TX and RX use interrupt-driven ring buffers. The CPU writes to a RAM
   buffer and returns immediately; SERCOM0_Handler drains TX and fills RX in the background.
   CPU overhead: 0.7% at max sustained throughput.

3. **No DMA used.** The plan suggested following the debug_functions.c DMA pattern. However,
   the SAMD21 has a single shared DMAC_Handler for all 12 DMA channels. The existing
   DMAC_Handler lives inside debug_functions.c wrapped in `#ifdef DEBUG_LOGGING_ENABLED`.
   Adding a second DMA channel would require splitting the ISR into a shared module —
   too much structural change for Phase 3. Interrupt-driven TX is sufficient.

4. **No assertion_handler.h yet.** conventions.md describes SATELLITE_ASSERT but the header
   doesn't exist in the project. The driver uses simple guard checks instead. The
   assertion handler should be created as a separate task before Phase 4.

5. **PING interval is 2 seconds, not 1 second.** The ESP32 sketch sends PING every 2s
   (not every 1s as the plan stated) to avoid flooding the debug log.

6. **Wiring was initially reversed.** During testing, ESP32 TX was accidentally connected
   to SAMD21 TX (and RX to RX). Symptoms: ESP32 received garbled data, SAMD21 received
   nothing. Swapping the two data wires fixed it. The correct wiring:
   ESP32 TX2 (GPIO17) → SAMD21 PA05 (RX); ESP32 RX2 (GPIO16) → SAMD21 PA04 (TX).

### Pass criterion — all verified

1. `make clean && make` — zero warnings ✓
2. `make flash` — Verified OK ✓
3. COM6 shows "OBC UART initialized on SERCOM0 PA04/PA05" ✓
4. ESP32 sends "PING" → COM6 shows "OBC RX: PING" ✓
5. SAMD21 echoes PING back → ESP32 Serial Monitor shows "Received: PING" ✓
6. Both UARTs work simultaneously without interference ✓
7. Heartbeat LED continues blinking at steady rate ✓

### Saved code: `code_samples/04_esp32_uart_echo/`

Reference: `docs/uart_obc_driver.md`

---

## Phase 4 — CHIPS Protocol Implementation

**Goal:** Implement the CHESS Internal Protocol over Serial (CHIPS) so the SAMD21
can communicate with the OBC (or ESP32 pretending to be the OBC) using proper
framing, CRC, and byte stuffing.

**Status: COMPLETE IN `phase4-chips` WORKTREE, NOT MERGED INTO MASTER.**
The CHIPS frame codec, command dispatch layer, ESP32 test sketch, and
self-tests exist in the separate `phase4-chips` worktree. They still need
to be merged and adapted into the two-board build structure.

### Why this is priority 2

The CHIPS protocol is the language the EPS speaks. All telemetry, telecommands,
parameter updates, and state transitions flow through it. We need it before we
can demonstrate any OBC-EPS interaction.

### Requirements satisfied

- R1: CHIPS protocol — HDLC framing, CRC-16/KERMIT, byte stuffing (PDF p.125-127)
- R2: EPS telemetry — 10 fields, ~210 bytes (PDF p.35-36)
- R16: Runtime parameter updates via UART (PDF p.34, Table 2.1.1)

### Source: CHIPS protocol specification (PDF pages 125-128)

The protocol is described in Section 3.5.2 of the CHESS mission document.
Full details are in `research_logs/agent_chips_protocol_and_comms.md`.

**Frame format (verified from PDF p.125-127):**
```
| 0x7E | SEQ(8) | RSP(1)+CMD(7) | PAYLOAD(0-1024) | CRC-16(2) | 0x7E |
  sync   seq num  response flag    variable data    Kermit CRC   sync
         + cmd ID
```

**Byte stuffing (PPP-style, RFC 1662):**
- 0x7E in payload → 0x7D 0x5E
- 0x7D in payload → 0x7D 0x5D
- Stuffing applies to everything between the two sync bytes

**CRC-16/KERMIT:**
- Polynomial: 0x1021
- Initial value: 0x0000
- Reflected input: YES
- Reflected output: YES
- Final XOR: 0x0000
- Also known as CRC-CCITT with reflected I/O
- Computed over: SEQ + RSP/CMD + PAYLOAD (before byte stuffing)

**Master/slave model:**
- OBC is always master — it initiates every exchange
- EPS must respond within 1 second
- If EPS doesn't respond, OBC retransmits after 1 second
- Commands are idempotent: execute once, reply every time same SEQ is seen
- 120-second autonomy timeout: if no OBC message for 120s, EPS may act alone

**CRITICAL GAP: Command IDs are NOT defined in the PDF.** We must define our own.
Proposed starting set:

| CMD ID | Name | Direction | Payload |
|---|---|---|---|
| 0x01 | GET_TELEMETRY | OBC→EPS | none |
| 0x02 | SET_PARAMETER | OBC→EPS | param_id(1) + value(4) |
| 0x03 | GET_STATE | OBC→EPS | none |
| 0x04 | SET_MODE | OBC→EPS | mode_id(1) |
| 0x05 | SHED_LOAD | OBC→EPS | load_mask(1) |
| 0x06 | DEPLOY_ANTENNA | OBC→EPS | none |
| 0x10 | TELEMETRY_RESPONSE | EPS→OBC | 210 bytes telemetry |
| 0x11 | STATE_RESPONSE | EPS→OBC | state_id(1) + sub_state(1) |
| 0x7F | ERROR/NACK | EPS→OBC | error_code(1) |

These IDs are preliminary. Adjust based on actual needs during implementation.

### What to build

**SAMD21 files:**
- `src/drivers/chips_protocol.c` — framing layer:
  - `chips_build_frame()` — takes cmd+payload, produces framed bytes with CRC
  - `chips_parse_byte()` — state machine, feed one byte at a time, returns
    CHIPS_INCOMPLETE / CHIPS_FRAME_READY / CHIPS_ERROR
  - `chips_compute_crc16_kermit()` — CRC calculation
  - Internal: byte stuffing/unstuffing
- `src/drivers/chips_protocol.h` — public API + frame struct definition
- `src/chips_command_handler.c` — EPS-specific command dispatch:
  - Receives parsed frames from chips_protocol
  - Dispatches to handler functions based on CMD ID
  - Builds and sends response frames
- `src/chips_command_handler.h` — command handler API

**ESP32 test sketch:**
- `esp32_test_harness/02_chips_protocol_test/02_chips_protocol_test.ino`
- Implements the OBC side of CHIPS:
  - Builds valid CHIPS frames with correct CRC
  - Sends GET_TELEMETRY command
  - Parses EPS response, validates CRC
  - Prints telemetry fields to Serial Monitor
  - Tests: valid frame, corrupted CRC, timeout behavior

### Telemetry packet structure (from PDF p.35-36)

The 10 telemetry fields the EPS must report:

| Field | Size | Description | Source |
|---|---|---|---|
| Bus voltages | 16 bytes | Voltage on each power rail | INA226 / ADC |
| Bus currents | 16 bytes | Current on each power rail | INA226 / LT6108 |
| Battery info | 40 bytes | Voltage, current, SOC, cycles | INA226 + ADC |
| Solar panel info | 8 bytes | Panel voltage, current per string | ADC |
| HDRM status | 1 byte | Hold-down release mechanism state | GPIO |
| MCU temperature | 4 bytes | Internal temp sensor | SAMD21 ADC |
| Battery temperature | 16 bytes | Battery pack temp sensors | SPI sensor |
| Solar panel temperature | 8 bytes | Panel temp sensors | Thermistor ADC |
| Command log | 100 bytes | Last N commands received | RAM buffer |
| Alert log | 10 bytes | Active fault flags | Comparator GPIO |

Total: ~210-219 bytes. For initial implementation, fill with placeholder values.
Replace with real sensor readings as drivers are built in Phase 6.

### Pass criterion

1. `make clean && make` — zero warnings
2. ESP32 sends GET_TELEMETRY (CMD 0x01) as valid CHIPS frame
3. SAMD21 debug log: "CHIPS frame received, cmd=0x01, seq=N, payload_len=0"
4. SAMD21 responds with TELEMETRY_RESPONSE (CMD 0x10) containing 210 bytes
5. ESP32 validates CRC of response — prints "CRC OK"
6. ESP32 prints parsed telemetry fields to Serial Monitor
7. ESP32 sends frame with bad CRC → SAMD21 debug log: "CHIPS CRC error"
   → SAMD21 responds with ERROR frame (CMD 0x7F)
8. ESP32 stops sending for >120 seconds → SAMD21 debug log: "OBC timeout,
   entering autonomous mode"

### Estimated time: 1.5-2 hours

---

## Phase 5 — Complementary PWM with Dead-Time (TCC0)

**Goal:** Configure TCC0 to output complementary PWM at 300 kHz with hardware
dead-time insertion (DTI). This is the signal that will drive the EPC2152 GaN
half-bridge buck converter.

**Status: COMPLETE FOR DEV BOARD IN `phase5-pwm` WORKTREE; MAINBOARD DRIVER NOT YET WRITTEN.**
The dev-board PWM driver targets PA18/PA20 as a natural TCC0 DTI pair. The
mainboard needs a sibling driver for PA12/PA13 using the dual-channel
TCC0 scheme documented in `research_logs/agent_F_tcc0_dual_channel_complementary_pwm.md`.

### Why this matters

The EPC2152 buck converter has two GaN FETs (high-side and low-side). They need
complementary drive signals — when one is ON, the other must be OFF. If both are
ON simultaneously even for nanoseconds ("shoot-through"), the FETs are destroyed.
Dead-time insertion guarantees both are OFF briefly during transitions.

### Requirements satisfied

- R3: Complementary PWM with dead-time on TCC0 for EPC2152 at 300 kHz
  (PDF p.96, SAMD21 datasheet Chapter 31 p.616+)

### Source: SAMD21 datasheet, Chapter 31 (TCC)

The SAMD21 TCC (Timer/Counter for Control applications) peripheral supports:
- Complementary waveform output on WO[x] and WO[x+N] pairs
- Dead-Time Insertion (DTI) via WEXCTRL register
- Only TCC0 and TCC3 support DTI (SAMD21 datasheet Table 7-7, p.34)
- TCC0 has 4 compare channels (CC[0]-CC[3]) and 8 waveform outputs (WO[0]-WO[7])

**DTI registers (from SAMD21 datasheet p.637-638):**
- WEXCTRL.DTIEN0: enable DTI for output pair 0 (WO[0] and WO[4])
- WEXCTRL.DTLS: dead-time low-side, 8-bit, in GCLK counts
- WEXCTRL.DTHS: dead-time high-side, 8-bit, in GCLK counts

**Calculation:**
- GCLK0 = 48 MHz → period = 20.83 ns per count
- Target frequency: 300 kHz → PER register = 48000000 / 300000 = 160 counts
- 50% duty → CC[0] = 80
- Dead-time 100ns → DTLS = DTHS = ceil(100ns / 20.83ns) = 5 counts
- 100ns is conservative; EPC2152 datasheet may specify different value

**Pin selection:**
- TCC0 WO[0] and WO[4] are the complementary pair when DTIEN0 is enabled
- Must find which GPIO pins these map to on the DM320119 Curiosity Nano headers
- Check `lib/samd21-dfp/pio/samd21g17d.h` for TCC0_WO0 and TCC0_WO4 pin options
- Check that chosen pins don't conflict with SERCOMs we need

### What to build

**SAMD21 files:**
- `src/drivers/pwm_complementary_tcc0.c`
- `src/drivers/pwm_complementary_tcc0.h`
- Public API:
  ```c
  void pwm_tcc0_initialize_complementary_300khz(void);
  void pwm_tcc0_set_duty_cycle(uint16_t duty_as_fraction_of_65535);
  ```
  The duty cycle input matches the MPPT algorithm output format (0-65535).
  Internally, map it to the TCC0 CC register range (0-160 for 300 kHz).

**ESP32 test sketch:**
- `esp32_test_harness/03_pwm_measurement/03_pwm_measurement.ino`
- ESP32 uses `pulseIn()` on both PWM pins to measure:
  - Frequency of each signal
  - Duty cycle of each signal
  - That they are complementary (inverted)
- ESP32 prints measurements to Serial Monitor
- Note: `pulseIn()` may not be accurate at 300 kHz (~3.3 µs period). If so, use
  ESP32 hardware timer capture or a simple interrupt-based frequency counter.
  Alternative: just use the SAMD21 debug log to confirm register values, and
  verify visually if an oscilloscope is available.

### Pass criterion

1. `make clean && make` — zero warnings
2. SAMD21 debug log: "TCC0 PWM initialized: 300kHz, DTI=5/5 counts"
3. Calling `pwm_tcc0_set_duty_cycle(32768)` → 50% duty on WO[0], 50% inverted on WO[4]
4. Calling `pwm_tcc0_set_duty_cycle(MPPT_MINIMUM_DUTY_CYCLE)` → 5% duty
5. Calling `pwm_tcc0_set_duty_cycle(MPPT_MAXIMUM_DUTY_CYCLE)` → 95% duty
6. ESP32 or oscilloscope confirms complementary signals with dead-time gap
7. Dynamic duty cycle changes from UART command work smoothly

### Estimated time: 45 minutes

---

## Phase 6 — Sensor Drivers (Simulated via ESP32)

**Goal:** Build the I2C, ADC, and SPI drivers that will read real sensors in the
final hardware, but test them using the ESP32 as a sensor simulator.

**Status: PAUSED / DEFERRED UNTIL AFTER THE SEMESTER DEMO.**
The semester-project demo does not need the physical sensor drivers first.
Sensor values will be injected over the OBC UART so the state machine, MPPT
logic, CHIPS communication, and PWM path can be tested as a coherent system.
Real ADC/I2C/SPI sensor validation remains important, but it follows the
hardware-in-the-loop demo.

### Why this matters

The EPS must read voltage, current, and temperature from multiple sensors to:
- Feed the MPPT algorithm (solar panel V and I)
- Report telemetry to the OBC
- Trigger safety actions (low battery, over-temperature)

We don't have the physical sensor ICs, so the ESP32 pretends to be each sensor.

### Requirements satisfied

- R4: INA226 I2C driver (PDF p.95, 108 — research_logs/agent_eps_sensors_and_dcdc.md)
- R5: ADC driver for analog sensors (PDF p.95-98)
- R10: Battery temp monitoring via SPI (PDF p.95 — part number unknown)

### Sub-step 6a: I2C Master Driver + INA226

**Source:** INA226 datasheet (Texas Instruments SBOS547A). The research agent
noted a typo in the PDF calling it "ina246" — it's actually INA226AIDGSR
(PDF p.108, component table).

**INA226 key registers (from TI datasheet):**
- 0x00: Configuration (16-bit, sets averaging, conversion time)
- 0x01: Shunt Voltage (16-bit, 2.5 µV/LSB)
- 0x02: Bus Voltage (16-bit, 1.25 mV/LSB)
- 0x03: Power (16-bit, derived)
- 0x04: Current (16-bit, derived from calibration)
- 0x05: Calibration (16-bit, sets current LSB)
- 0xFE: Manufacturer ID (should read 0x5449 = "TI")
- 0xFF: Die ID (should read 0x2260 for INA226)

**Default I2C address:** 0x40 (A0=GND, A1=GND). PDF does not specify the actual
addresses used on the EPS board — this is a gap. Use 0x40 for testing.

**I2C on SAMD21:**
- SERCOM in I2C master mode (MODE = 0x5 in CTRLA)
- Needs SDA and SCL pins — check which SERCOM/pins are available
- 100 kHz or 400 kHz I2C clock
- BAUD register calculation: BAUD = (f_GCLK / (2 * f_SCL)) - 1

**Files to create:**
- `src/drivers/i2c_master_sercom.c/.h` — generic I2C master (init, write, read)
- `src/drivers/ina226_voltage_current_sensor.c/.h` — INA226-specific (read bus
  voltage, read shunt voltage, read manufacturer ID for verification)

**ESP32 as I2C slave (sensor simulator):**
- `esp32_test_harness/04_i2c_sensor_simulator/04_i2c_sensor_simulator.ino`
- ESP32 uses Wire library in slave mode: `Wire.begin(0x40)`
- On register read request, ESP32 returns fake values:
  - Register 0x02 (bus voltage): varies from 3000 to 4200 (simulating 3.75V-5.25V)
  - Register 0xFE: returns 0x5449 (manufacturer ID)
- ESP32 prints what was requested to Serial Monitor for debugging
- Wiring: ESP32 SDA ↔ SAMD21 SDA, ESP32 SCL ↔ SAMD21 SCL, common GND
  IMPORTANT: I2C needs pull-up resistors (4.7kΩ to 3.3V) on SDA and SCL

**Pass criterion for 6a:**
1. SAMD21 reads INA226 manufacturer ID = 0x5449 → logs "INA226 found at 0x40"
2. SAMD21 reads bus voltage register → logs changing values matching ESP32 output
3. I2C errors (NACK, timeout) are detected and logged

### Sub-step 6b: ADC Driver

**Source:** SAMD21 datasheet Chapter 33 (ADC), p.683+

**SAMD21 ADC key facts:**
- One ADC module with multiplexed inputs (up to 20 channels)
- 12-bit resolution (0-4095)
- Needs GCLK connection + reference voltage selection
- Pin mux function B for analog inputs
- Can average multiple samples in hardware (AVGCTRL register)

**Files to create:**
- `src/drivers/adc_single_channel_read.c/.h`
- Public API:
  ```c
  void adc_initialize(void);
  uint16_t adc_read_channel(uint8_t channel_number);  /* returns 12-bit value */
  ```

**ESP32 for testing:**
- ESP32 DAC outputs (GPIO25 or GPIO26) generate a known voltage
- Connect ESP32 DAC → voltage divider (if needed for 3.3V range) → SAMD21 ADC pin
- SAMD21 reads ADC, logs value. ESP32 varies DAC output, SAMD21 tracks it.
- Alternative simpler test: connect SAMD21 ADC pin to a potentiometer (3.3V-GND)
  and manually turn it while watching debug log. No ESP32 needed for basic ADC test.

**Pass criterion for 6b:**
1. SAMD21 reads ADC channel → logs raw value
2. Value changes when input voltage changes (ESP32 DAC or potentiometer)
3. Value is within expected range (0-4095 for 0-3.3V)

### Sub-step 6c: SPI Master Driver (Battery Temperature)

**Source:** PDF p.95 mentions SPI for the battery temperature sensor. The exact
sensor part number is NOT specified — this is a documented gap
(research_logs/agent_eps_sensors_and_dcdc.md and agent_thermal_safety_loadshed.md).

Build a generic SPI master driver and a placeholder sensor driver. When the real
part number is known, we'll add the specific register map.

**Files to create:**
- `src/drivers/spi_master_sercom.c/.h` — generic SPI master
- `src/drivers/battery_temperature_sensor.c/.h` — placeholder with:
  ```c
  void battery_temp_sensor_initialize(void);
  int16_t battery_temp_sensor_read_temperature_decicelsius(void);  /* e.g. 253 = 25.3°C */
  ```

**ESP32 as SPI slave:**
- `esp32_test_harness/04_i2c_sensor_simulator/` — extend this sketch to also
  handle SPI slave mode, OR create a separate sketch
- ESP32 sends fake temperature values (e.g., ramp from -10°C to +50°C)

**Pass criterion for 6c:**
1. SAMD21 reads SPI → logs temperature value
2. Temperature changes match what ESP32 sends

### Estimated time: 1-1.5 hours total for all three sub-steps

---

## Phase 7 — MPPT Running on SAMD21 with ESP32 Closed-Loop Simulation

**Goal:** The MPPT algorithm runs on the SAMD21 in a real-time closed loop with
the ESP32 providing the physics simulation (solar panel model + buck converter
model). This proves the algorithm works on actual hardware with realistic feedback.

**Status: NOT INTEGRATED INTO MAINBOARD FIRMWARE YET.**
The MPPT algorithm itself exists in the `mppt-algorithm` worktree. The next
useful MPPT test should run it on the real SAMD21 using injected voltage and
current values over UART before relying on physical sensors.

### Why this is a key demo

This is the most impressive demo for the meeting: the SAMD21 is genuinely running
the MPPT algorithm, making real-time duty cycle decisions, and converging to the
maximum power point — all with realistic physics simulated by the ESP32.

### Requirements satisfied

- R6: MPPT IncCond algorithm runs on SAMD21 with closed-loop feedback
  (PDF p.97/101, EPS-mppt-algorithm worktree)

### What exists already (in the `EPS-mppt-algorithm` worktree)

The worktree at `C:\Users\iceoc\Documents\EPS-mppt-algorithm` contains:

- `src/mppt_algorithm.c/.h` — pure integer IncCond algorithm. Takes raw 12-bit
  ADC readings for voltage and current, returns duty cycle as uint16_t (0-65535).
  Has moving average filter (8 samples), noise threshold, duty cycle clamping.
  **This file runs directly on Cortex-M0+ with no changes needed.**

- `tests/solar_panel_simulator.c/.h` — five-parameter single-diode model of the
  Azur Space 3G30C solar panel array. Uses float math. **Runs on ESP32 only.**

- `tests/buck_converter_model.c/.h` — models V_panel = V_battery / D, converts
  to ADC readings with configurable noise. Uses float math. **Runs on ESP32 only.**

- `tests/mppt_simulation_runner.c` — runs complete simulation scenarios with
  varying irradiance. Has 6 test scenarios producing CSV output. This is the
  reference for what the closed loop should look like.

### What to build

**SAMD21 side:**
1. Copy `mppt_algorithm.c/.h` from worktree into `src/drivers/`
2. Create new `src/main.c` that runs the MPPT loop:
   ```
   while (1):
     1. Request sensor values from ESP32 over OBC UART
     2. Receive voltage_adc and current_adc (2x uint16_t)
     3. Feed to mppt_algorithm_run_one_iteration()
     4. Get new duty_cycle back
     5. Update TCC0 PWM with new duty cycle
     6. Send duty_cycle to ESP32 over OBC UART
     7. Log: voltage, current, power, duty_cycle via debug UART
     8. Wait for next sample period (~10ms for 100 Hz)
   ```

**ESP32 side (Arduino sketch):**
- `esp32_test_harness/05_mppt_physics_simulation/05_mppt_physics_simulation.ino`
- Port `solar_panel_simulator.c` and `buck_converter_model.c` to Arduino (these
  use float math — ESP32 has hardware FPU, works fine)
- ESP32 runs this loop:
  ```
  while (1):
    1. Receive duty_cycle (uint16_t) from SAMD21 over Serial2
    2. Compute panel voltage: V_panel = V_battery / (duty_cycle / 65535.0)
    3. Compute panel current from solar_panel_compute_current_at_voltage()
    4. Convert to 12-bit ADC readings with 2% noise
    5. Send voltage_adc + current_adc (2x uint16_t) back to SAMD21
    6. Print to Serial: iteration, V_panel, I_panel, power, duty_cycle
    7. Optionally: change irradiance at specific iteration counts to show
       algorithm tracking a moving MPP
  ```

**Simple binary protocol for this step (between SAMD21 and ESP32):**
- SAMD21 → ESP32: 2 bytes (duty_cycle, big-endian uint16_t)
- ESP32 → SAMD21: 4 bytes (voltage_adc uint16_t + current_adc uint16_t, big-endian)
- OR: use CHIPS protocol if Phase 4 is complete (better demo but more complex)

### Irradiance change scenarios (from worktree test scenarios)

The worktree has 6 scenarios in `mppt_simulation_runner.c`:
1. Constant full sun (1.0) — basic convergence test
2. Step from 1.0 to 0.5 at iteration 200 — cloud passing
3. Gradual ramp 0.3 to 1.0 — orbit dawn
4. Step from 1.0 to 0.0 to 1.0 — eclipse entry/exit
5. Rapid fluctuations — partial shading
6. Very low irradiance (0.1) — worst case

For the demo, scenarios 1 and 2 are the most visual. Scenario 4 (eclipse) is
the most dramatic.

### Pass criterion

1. SAMD21 debug log shows voltage/current/power converging over ~50-100 iterations
2. Converged power is within 2% of theoretical MPP (same as PC simulation criterion)
3. When ESP32 changes irradiance mid-run, SAMD21 tracks to new MPP within 100 iterations
4. TCC0 PWM duty cycle changes are logged and match algorithm output
5. ESP32 Serial Monitor shows matching convergence from simulation side
6. No buffer overflows, no hangs, no crashes over 1000+ iterations

### Estimated time: 1 hour

---

## Phase 8 — EPS State Machine + Safety Logic

**Goal:** Implement the central state machine that governs all EPS behavior:
mode transitions, load shedding, heater control, watchdog, autonomy timeout.

**Status: PURE LOGIC IMPLEMENTED IN `mppt-algorithm` WORKTREE, NOT MERGED INTO MASTER.**
The state-machine logic exists as host-testable C in the MPPT worktree. It
still needs to be integrated into the SAMD21 firmware and connected to a
sensor-input abstraction, CHIPS commands, telemetry, and actuator outputs.

### Why this matters

The state machine is the brain of the EPS. Without it, the firmware is a
collection of drivers with no coordination. The state machine decides:
- When to run MPPT vs when to shed loads
- When to activate heaters
- When to enter safe mode
- When to act autonomously (OBC timeout)

### Requirements satisfied

- R7: State machine with modes: BOOT → LEOP → CHARGING → MEASUREMENT → COMMUNICATION → SAFE
  (PDF p.20-25, Figure 1.7.1 on p.24)
- R8: Load shedding via eFuse enable pins (PDF p.103-104)
- R9: Heater control via GPIO/PWM (PDF p.95)
- R11: Watchdog timer (PDF p.106-107)
- R12: 120s OBC autonomy timeout (PDF p.127)
- R13: EPS boots first (PDF p.14-15, 26-27)
- R14: LEOP battery-only 30+ min (PDF p.15, 26-28)
- R15: Antenna deployment burn wire (PDF p.218-219)

### State machine design

**Architecture principle:** The state machine is PURE LOGIC. It receives inputs
(sensor readings, OBC commands, timer values) and returns ACTIONS (set PWM,
shed load X, activate heater, send telemetry). It does NOT directly call
hardware functions. This makes it fully testable.

```c
struct eps_state_machine_inputs {
    uint16_t battery_voltage_adc;
    int16_t  battery_temperature_decicelsius;
    uint16_t solar_panel_voltage_adc;
    uint16_t solar_panel_current_adc;
    uint8_t  obc_command_received;      /* 0 = none, or CMD ID */
    uint32_t seconds_since_last_obc_message;
    uint32_t seconds_since_boot;
    uint8_t  fault_flags;               /* from comparator GPIOs */
};

struct eps_state_machine_outputs {
    uint8_t  current_state;             /* BOOT, LEOP, CHARGING, etc. */
    uint8_t  current_sub_state;         /* for SAFE mode: DETUMBLE, CHARGE, COM, REBOOT */
    uint8_t  mppt_enabled;              /* 1 = run MPPT, 0 = stop */
    uint8_t  heater_enabled;            /* 1 = turn on heater */
    uint8_t  load_shed_mask;            /* bit per load: 1 = shed */
    uint8_t  deploy_antenna;            /* 1 = trigger burn wire */
    uint8_t  send_telemetry_now;        /* 1 = pack and send via CHIPS */
    uint8_t  reboot_requested;          /* 1 = self-reboot (SAFE_REBOOT escalation) */
};
```

**States (from PDF p.20-25, Figure 1.7.1):**
- BOOT: hardware init, self-test
- LEOP: battery-only, solar panels stowed, wait for deployment timer
- CHARGING: default mode, MPPT active, sun-pointing
- MEASUREMENT: payload active, 4W peak budget
- COMMUNICATION: UHF active, ground pass
- SAFE: emergency — 4 sub-states (DETUMBLING, CHARGING, COMMUNICATION, REBOOT)

**Safe mode entry triggers (from PDF p.23):**
1. Tumbling rate > TumbMax
2. Battery < Bmin
3. No ground contact > TimeMax
4. Telemetry failure
5. EPS failure
6. Antenna deployment failure
7. Temperature outside [TempMin, TempMax]
8. Flight software failure
9. Manual ground command

**Configurable thresholds (updatable via CHIPS SET_PARAMETER command):**
- Bmin: minimum battery voltage (trigger safe mode)
- BminCrit: critical battery (trigger SAFE_CHARGING)
- TempMin: heater activation threshold
- TempMax: over-temperature load shedding threshold
- TimeMax: OBC communication timeout (seconds)
- TimeMax2: escalation to SAFE_REBOOT (days)

### What to build

**SAMD21 files:**
- `src/eps_state_machine.c/.h` — pure logic state machine
- `src/drivers/load_shedding_efuse_control.c/.h` — GPIO for eFuse enable pins
- `src/drivers/heater_control.c/.h` — GPIO for heater MOSFET
- `src/drivers/watchdog_timer.c/.h` — SAMD21 WDT configuration

**ESP32 test sketch:**
- `esp32_test_harness/06_state_machine_scenarios/06_state_machine_scenarios.ino`
- ESP32 sends time-series of simulated sensor values + OBC commands via CHIPS
- Pre-programmed scenarios:
  - Scenario A: Normal boot → LEOP (30min simulated) → CHARGING → MEASUREMENT
  - Scenario B: Battery drops below Bmin → SAFE → SAFE_CHARGING → recovery
  - Scenario C: Temperature < TempMin → heater ON → temp rises → heater OFF
  - Scenario D: No OBC message for 120s → autonomous mode → OBC resumes
  - Scenario E: No ground contact > TimeMax → SAFE_REBOOT escalation
  - Scenario F: Load shedding sequence under low power
- ESP32 compresses time: 1 second of real time = configurable simulated time
- ESP32 reads SAMD21 debug output (or CHIPS responses) to verify state transitions

### Pass criterion

1. Each scenario produces the expected state transitions (verified from debug log)
2. Heater GPIO goes HIGH when temperature < TempMin, LOW when recovered
3. Load shedding GPIOs change at correct transitions
4. OBC timeout at 120s triggers autonomous behavior
5. Watchdog doesn't fire during normal operation
6. Watchdog DOES fire and recover when main loop is deliberately stalled
7. SET_PARAMETER command changes thresholds and affects behavior

### Estimated time: 2 hours

---

## Dependency Graph

```
Phase 3 (ESP32 UART)
  ├──→ Phase 4 (CHIPS protocol) ──→ Phase 8 (State machine)
  ├──→ Phase 5 (Complementary PWM) ──→ Phase 7 (MPPT closed loop)
  └──→ Phase 6 (Sensor drivers)  ──┤
                                     └──→ Phase 7 (MPPT closed loop)
```

Phase 5 (PWM) only needs Phase 3 for the ESP32 verification, but it can be done
with just debug UART if the ESP32 isn't ready yet.

## Historical Implementation Order (Optimized for Tomorrow's Demo)

| Order | Phase | Time | What to show |
|---|---|---|---|
| 1 | Phase 3 — ESP32 UART | 30-45 min | Two boards talking |
| 2 | Phase 5 — Complementary PWM | 45 min | PWM signals on scope/ESP32 |
| 3 | Phase 4 — CHIPS protocol | 1.5-2 hrs | OBC↔EPS framed communication |
| 4 | Phase 6 — Sensor drivers | 1-1.5 hrs | Simulated sensor readings |
| 5 | Phase 7 — MPPT closed loop | 1 hr | Algorithm converging on real MCU |
| 6 | Phase 8 — State machine | 2 hrs | Mode transitions + safety logic |

Total: ~7-8 hours. If time is tight, prioritize Phases 3→5→7 (UART + PWM + MPPT)
as these are the most visual demos.

**2026-05-10 correction:** this table is historical. It was useful when the
goal was a quick dev-board demo. For the semester-project presentation, the
priority is now a full mainboard hardware-in-the-loop EPS demo with injected
values over UART. That demo moves Phase 6 physical sensor drivers after the
communication, state-machine, MPPT, and PWM integration work.

---

## Non-Negotiable Rules Across All Phases

- Every phase has a pass criterion. We do not move forward until it passes.
- We never add two new things at once. If something breaks, one change caused it.
- The main branch always compiles with zero warnings.
- Pure logic functions are tested on the laptop before the chip.
- `conventions.md` applies to every line of every file, always.
- Every test is run through the debug UART or ESP32 — no "trust me it works."
- Before using any register, verify it against the datasheet (CLAUDE.md Principle 1).
- SERCOM and pin allocation must be documented and checked for conflicts.

---

## Mainboard Bring-Up — Day 1 (2026-04-26)

This section is APPENDED. Earlier sections describe development against the
Curiosity Nano DM320119 dev board only. From this date onwards the same
repo also targets the actual EPS PCU testing board V4.1 (the real PCB
that will eventually fly), and the build system carries both.

### What changed: the real board exists now

| | Dev board (older work) | Mainboard (added today) |
|---|---|---|
| Physical board | Curiosity Nano DM320119 | EPS PCU testing board V4.1, repo `CHESS-mission/eps_pcu_eng` (private) |
| Chip on board | ATSAMD21G17D, 48-pin TQFP | ATSAMD21J17D-MUT, 64-pin QFN |
| Programmer | On-board nEDBG (USB-CMSIS-DAP) | External Atmel-ICE via Tag-Connect TC2050-IDC-NL footprint at `J1` |
| User LED | PB10, active-LOW | PB22, active-HIGH (different polarity) |
| OBC UART pins | PA04 (TX) / PA05 (RX), mux D | PA10 (TX) / PA11 (RX), mux C |
| Debug UART | PA22, mux D, SERCOM5 | not exposed (PA22 on the mainboard is I²C SDA) |
| Buck-converter PWM | PA18 + PA20 (TCC0/WO[2]+WO[6], natural DTI pair) | PA12 + PA13 (TCC0/WO[6]+WO[7], dual-channel scheme) |

The two chips are the **same SAMD21 silicon die in different packages** —
verified against the family datasheet, against a fresh diff of the DFP
v3.6.144 atpack, and against the chip-id table in OpenOCD source. This
means every driver register-write written for the dev board ports
verbatim to the mainboard; the only changes are the chip-name macro,
the pin numbers (when applicable), the startup file, and the linker
script. See `docs/build_targets_and_file_map.md` and
`research_logs/agent_A_samd21_g17d_vs_j17d_silicon.md` for the proof.

### What we built today

- Five vendor files dropped into the project, additive (existing G17D
  files untouched): `lib/samd21-dfp/samd21j17d.h`,
  `lib/samd21-dfp/pio/samd21j17d.h`, `startup/startup_samd21j17d.c`,
  `startup/system_samd21j17d.c`, `samd21j17d_flash.ld`.
- New driver `src/drivers/led_status_pb22_active_high_on_mainboard.c/.h`
  — GPIO-only, smallest possible LED driver for PB22 active-HIGH.
- New application `src/main_mainboard_blink_pb22.c` — Firmware A, the
  smallest "is the chip alive?" test: 48 MHz clock + PB22 toggle at ~1 Hz.
- `Makefile` reorganised so it can produce binaries for either board.
  `BOARD` must be set explicitly on every invocation (no default), the
  one exception being `make clean`. Wrong invocation prints a clear error.
- Existing dev-board build verified byte-identical (4332/4/5704). Mainboard
  build produces 1012/4/4124, well under the 128 KB / 16 KB chip limits.

### Bring-up status as of 2026-04-26 evening

- Programmer hardware identified: Microchip Atmel-ICE.
- Cable identified: Tag-Connect TC2050-IDC-NL (0.1″ IDC end). Ribbon
  marked with a red wire on pin 1.
- Wiring path resolved: Atmel-ICE SAM port → numbered squid cable →
  5 male-male jumpers → IDC plug → Tag-Connect head → board J1 pads.
  Five active wires (numbers 1, 2, 3, 4, 10), the rest dangling.
- Multimeter continuity check on the IDC plug orientation was **skipped**
  (logged as a known failure point: if the first flash refuses to connect,
  cable orientation is the first thing to suspect).
- Firmware A binary built, sitting in `build/satellite_firmware.bin`.
- First flash attempt scheduled for the same evening.

### How firmware files are now organised — the "which board does this code apply to?" question

This is the discipline the project now follows so that no future Claude
Code instance (or human) confuses the two builds:

**Three reinforcing layers, in order of authority:**

1. **The Makefile** is the executable source of truth. Its
   `ifeq ($(BOARD),devboard) … else ifeq ($(BOARD),mainboard)` block lists
   exactly which `.c` files compile for each build. If a file isn't listed,
   it doesn't compile.
2. **The file name** is a sentence (per `notes/conventions.md`). Files
   ending in `_on_mainboard.c/.h` are mainboard-specific. Files whose name
   names specific dev-board pins (e.g. `..._sercom0_pa04_pa05.c`) are
   dev-board specific. No suffix → shared.
3. **The `BUILD TARGET:` line in every file's header comment** says
   explicitly `devboard only`, `mainboard only`, or `shared`. This is the
   fastest way to confirm classification when you open a file.

If the three layers ever disagree, the Makefile wins. They were aligned on
2026-04-26 across every project source file.

**The three categories every file falls into:**

- `devboard only` — used only by `make BOARD=devboard`. Examples: `src/main.c`,
  `src/drivers/debug_functions.*`, `src/drivers/uart_obc_sercom0_pa04_pa05.*`,
  the G17D vendor files.
- `mainboard only` — used only by `make BOARD=mainboard`. Examples:
  `src/main_mainboard_blink_pb22.c`, `src/drivers/led_status_pb22_active_high_on_mainboard.*`,
  the J17D vendor files.
- `shared` — used by both. Examples:
  `src/drivers/clock_configure_48mhz_dfll_open_loop.*`, `syscalls_min.c`,
  the family-wide DFP component / instance / cmsis headers.

For the full file-by-file map (every source, every header, every vendor
file, every linker script), see
[`docs/build_targets_and_file_map.md`](../docs/build_targets_and_file_map.md).

### Existing dev-board drivers — do they change?

No. None of the dev-board drivers are edited. They are SHARED in spirit
(same chip silicon underneath) but for the moment they are tagged
`devboard only` because they reference dev-board pin numbers. When the
mainboard needs equivalent functionality (e.g. UART for Firmware B), a
sibling file is created (e.g. `uart_obc_sercom0_pa10_pa11_on_mainboard.c`)
rather than editing the existing dev-board driver. That keeps the
dev-board build alive and lets us flash the Curiosity Nano any time we
need it for comparison.

### What's queued after Firmware A flashes successfully

1. Firmware B (UART echo on PA10/PA11 via SERCOM0, mux C, TXPO=1, RXPO=3).
   New driver `uart_obc_sercom0_pa10_pa11_on_mainboard.c/.h` to be written.
2. Firmware C (300 kHz complementary PWM on PA12 + PA13, scope-verifiable).
   Will use the dual-channel TCC0 scheme verified by research agent F.
3. Once all three pass on the mainboard: integrate the MPPT algorithm and
   EPS state machine from the `mppt-algorithm` worktree, which are pure-logic
   modules and port without any change.

---

## Next Step: Mainboard MPPT Test 1 MVP

This section remains valid for later bench power-stage bring-up. It is no
longer the immediate semester-demo priority. The next semester-project target
is the hardware-in-the-loop full EPS demo appended below.

The test firmware should:

1. Generate buck PWM on the real mainboard pins `PWM_H`/`PWM_L`
   (`PA12`/`PA13`).
2. Read the analog measurement pins that the PCB already provides:
   `OUTV1`, `OUTV2`, `OUTA1`, `OUTA2`, and optionally `PV_IMON`/`BAT_IMON`.
3. Convert raw ADC readings into engineering values using the PCB scaling
   constants, once those constants are verified from the schematic and/or
   bench measurements.
4. Log duty cycle, raw ADC readings, converted voltage/current estimates, and
   computed power so Alexander can verify the hardware behavior.
5. Start with fixed-duty and voltage-regulation tests before connecting the
   existing Incremental Conductance MPPT algorithm to the real ADC and PWM
   peripherals.

This step is documented in
[`docs/mpptest1.md`](../docs/mpptest1.md). That document is the working
agreement for the first useful code we intend to send for bench testing.

---

## Semester Project Target: Full EPS Hardware-In-The-Loop Demo With Injected Values

**Date added:** 2026-05-10

This is now the highest-priority integration path for the semester project.
The goal is to demonstrate a complete and coherent EPS firmware running on
real hardware without being blocked by unfinished physical sensor bring-up.

The demo should prove that the real EPS mainboard can:

1. Communicate with an OBC-like external controller over the board UART.
2. Accept injected voltage, current, temperature, mode, and fault values over
   that communication link.
3. Run the EPS state machine on the SAMD21 using those injected inputs.
4. Run the Incremental Conductance MPPT algorithm on the SAMD21 using injected
   panel voltage/current values.
5. Produce real PWM duty-cycle outputs on the mainboard buck-converter pins.
6. Return telemetry, state, commanded duty cycle, and alert information back
   to the host so the behavior can be plotted and presented.

This is not a fake software-only simulation. The control firmware runs on the
real SAMD21. The only simulated part is the sensor environment.

### Physical Test Setup

The intended final setup is:

```text
Computer simulation / control UI
    |
    | USB serial
    v
ESP32 bridge
    |
    | 3.3 V UART
    v
EPS PCU mainboard J3 UART header
    |
    | SERCOM0 on PA10/PA11
    v
SAMD21J17D firmware
```

The first setup to build is the same communication architecture on the dev
board:

```text
Computer simulation / control UI
    |
    | USB serial
    v
ESP32 bridge acting as the OBC
    |
    | 3.3 V UART
    v
SAMD21 dev board UART pins
    |
    | SERCOM0, preferably PA10/PA11 to match the mainboard SERCOM0 pad layout
    v
SAMD21G17D firmware
```

This dev-board setup is the first blocker to remove. Before integrating more
MPPT, state-machine, or PWM behavior, the firmware must have a reliable way to
receive commands and send back proof of what it is doing.

Verified board assumptions:

- J3 pin 1 is `AUX_3V3`.
- J3 pin 2 is `UART_TX`.
- J3 pin 3 is `UART_RX`.
- J3 pin 4 is `GND`.
- The MCU UART nets are `PA10` / `PA11`.
- `PA10` is SERCOM0 PAD[2], mux C.
- `PA11` is SERCOM0 PAD[3], mux C.
- UART configuration for the mainboard is therefore SERCOM0, mux C,
  `TXPO=1`, `RXPO=3`, 115200 baud initially.
- Mainboard PWM pins are `PA12` / `PA13`, TCC0 `WO[6]` / `WO[7]`, mux F.
- The mainboard PWM driver must use the dual-channel TCC0 scheme:
  `CC[2] == CC[3]`, `DTIEN2 | DTIEN3`, and `INVEN7`.

References:

- `docs/mainboard_pinout_pcu_v4_1.md`
- `docs/build_targets_and_file_map.md`
- `docs/mpptest1.md`
- `research_logs/agent_F_tcc0_dual_channel_complementary_pwm.md`
- `research_logs/agent_H_j3_uart_header_pinout_and_power_input.md`

### Why This Is The Right Semester Demo

Physical sensor drivers are useful but they are not the critical path for a
coherent EPS demonstration. A sensor-first plan risks spending most of the
project on peripheral bring-up without showing the full EPS behavior.

The hardware-in-the-loop injection plan demonstrates the actual architecture:

- OBC communication works.
- The EPS receives commands and runtime data.
- The state machine transitions under controlled scenarios.
- MPPT runs on the actual MCU.
- PWM commands are applied to the actual timer peripheral.
- Telemetry comes back through the same OBC-facing communication path.

After this demo works, the physical sensor drivers become a clean next step:
replace injected values with real ADC/I2C/SPI readings behind the same sensor
input interface.

### Firmware Architecture

The key architectural rule is to separate **where sensor values come from**
from **what the EPS does with them**.

The second rule is that the demo uses **one bidirectional CHIPS UART channel**
for both control and visibility. The host/ESP32 sends commands on that channel,
and the EPS returns telemetry, state, debug snapshots, command acknowledgements,
and optional periodic debug frames on the same channel.

Do not mix raw text prints into the CHIPS UART stream. If a debug value needs
to be visible, send it as a CHIPS response or CHIPS debug/telemetry frame. The
dev-board DMA logger on SERCOM5/PA22 may still be used as a development aid,
but the semester demo must not depend on it because the mainboard does not
expose that same debug UART.

The main loop should use this structure:

```text
1. Receive CHIPS frames from UART.
2. Update injected sensor/input state if an injection command arrived.
3. Build the current EPS input struct from the active source:
      - injected UART values, or
      - real ADC/I2C/SPI values later.
4. Run the EPS state machine.
5. Run MPPT when the state machine enables MPPT.
6. Apply actuator outputs:
      - PWM duty cycle,
      - load enable/disable pins later,
      - heater control later.
7. Send telemetry/state/duty/alerts back over CHIPS.
```

This means the state machine and MPPT code do not know whether values came
from physical sensors or the injected UART test harness.

### Communication / Injection Protocol

Use CHIPS as the preferred protocol rather than inventing a one-off binary
format, because CHIPS is already the mission communication layer and exists
in the `phase4-chips` worktree.

Add or reserve commands for the demo:

| Command | Direction | Purpose |
|---|---|---|
| `GET_TELEMETRY` | OBC/PC -> EPS | Request current EPS telemetry snapshot |
| `SET_INJECTED_SENSOR_FRAME` | OBC/PC -> EPS | Inject voltage, current, temperature, mode, and fault inputs |
| `SET_INPUT_SOURCE` | OBC/PC -> EPS | Select injected inputs or real sensor inputs |
| `SET_CONTROL_MODE` | OBC/PC -> EPS | Select off, fixed-duty, voltage regulation, MPPT, or full state-machine control |
| `SET_PARAMETER` | OBC/PC -> EPS | Change thresholds, safe limits, target voltage, duty clamps |
| `GET_STATE` | OBC/PC -> EPS | Read current state-machine mode, safe substate, alerts |
| `SET_FIXED_DUTY` | OBC/PC -> EPS | Directly command a duty cycle for PWM verification |
| `GET_DEBUG_SNAPSHOT` | OBC/PC -> EPS | Request one detailed debug frame for presentation/diagnosis |
| `SET_TELEMETRY_STREAM` | OBC/PC -> EPS | Enable/disable periodic telemetry/debug frames and set their period |

The value-forcing commands should be implemented at the beginning, before the
full algorithms are integrated. At first they only need to store the injected
values and echo them back in telemetry. Later, the state machine, MPPT, and PWM
code will consume the same stored input struct. This keeps the PC/ESP32 protocol
stable while the firmware grows.

The injected sensor frame should include at least:

```text
panel_voltage_raw_adc
panel_current_raw_adc
battery_voltage_mv
battery_current_ma
charging_rail_voltage_mv
battery_temperature_decicelsius
obc_heartbeat_present
satellite_mode_commanded_by_obc
safe_mode_sub_state_commanded_by_obc
fault_flags
```

For the MPPT-only demo, the critical fields are:

```text
panel_voltage_raw_adc = simulated OUTV1
panel_current_raw_adc = simulated OUTA1
```

### Implementation Order For The Semester Demo

1. **Build the dev-board CHIPS UART visibility path first.**
   - Create or adapt the dev-board UART driver for SERCOM0 on PA10/PA11 if
     those pins are practical on the dev-board headers.
   - If PA10/PA11 are blocked mechanically, temporarily use PA04/PA05, but keep
     the UART API identical so the application code does not change later.
   - Build the ESP32 bridge/test firmware that forwards PC serial traffic to
     the SAMD21 UART and prints returned CHIPS frames on the PC.
   - Build the SAMD21 dev-board application that receives CHIPS commands,
     returns acknowledgements, and returns telemetry over the same UART.
   - Implement `SET_INJECTED_SENSOR_FRAME`, `GET_TELEMETRY`,
     `GET_DEBUG_SNAPSHOT`, and `SET_TELEMETRY_STREAM` immediately, even if they
     initially only store and echo values.
   - Pass condition: from the PC, inject values through the ESP32, then see the
     same values, firmware state, command result, and heartbeat returned from
     the SAMD21.

2. **Fix build hygiene and preserve both board targets.**
   - Make normal Windows builds reliable.
   - Keep dev-board and mainboard builds separate.
   - Do not let generated `build/` artifacts confuse source status.

3. **Add the mainboard UART driver.**
   - Create `uart_obc_sercom0_pa10_pa11_on_mainboard.c/.h`.
   - Use SERCOM0, PA10/PA11, mux C, `TXPO=1`, `RXPO=3`.
   - First test: UART echo over J3 through the ESP32 bridge.
   - Keep the CHIPS protocol and application command handling identical to the
     dev-board version.

4. **Merge or copy the CHIPS protocol modules into the two-board build.**
   - Reuse the pure frame codec and command dispatch from `phase4-chips`.
   - Keep the frame codec shared.
   - Keep board-specific UART drivers separate.
   - Confirm host/ESP32 can send a command and receive a valid CRC response.

5. **Add the injection input model.**
   - Create a shared struct for EPS input values.
   - Add injected-value storage updated by CHIPS commands.
   - Add an input-source selector: injected values first, real sensors later.

6. **Integrate the pure MPPT algorithm.**
   - Merge `mppt_algorithm.c/.h` from `EPS-mppt-algorithm`.
   - Feed it injected panel voltage/current raw ADC values.
   - Return the duty cycle in telemetry before enabling real PWM output.

7. **Add the mainboard PWM driver.**
   - Create a mainboard-specific PA12/PA13 TCC0 driver.
   - Start in a safe disabled state.
   - Support fixed-duty mode first.
   - Then let MPPT/state-machine output drive the duty cycle.

8. **Integrate the EPS state machine.**
   - Merge `eps_state_machine.c/.h` and `eps_configuration_parameters.h`.
   - Feed it the injected input struct.
   - Use its actuator outputs to decide MPPT enable, duty command, and alerts.

9. **Build the PC + ESP32 demo harness.**
   - PC runs simulation scenarios and sends CHIPS injection frames.
   - ESP32 bridges USB serial to mainboard UART, or implements the OBC side
     directly if that is simpler during the demo.
   - PC logs returned telemetry and plots state, voltage/current, power,
     duty cycle, and safe-mode transitions.

10. **Run presentation scenarios.**
   - Normal charging.
   - MPPT convergence under constant illumination.
   - Irradiance step change and MPPT recovery.
   - Battery low -> safe/charging behavior.
   - Temperature low -> heater request.
   - OBC heartbeat timeout -> autonomous behavior.
   - Fixed-duty PWM verification.

11. **After the demo works, resume physical sensor drivers.**
    - ADC on `PB04..PB09` for `PV_IMON`, `BAT_IMON`, `OUTA1`, `OUTA2`,
      `OUTV1`, `OUTV2`.
    - I2C sensors only if populated and confirmed.
    - SPI battery temperature once the part and register map are known.

### Pass Criteria

The semester demo is successful when:

1. `make BOARD=devboard` and `make BOARD=mainboard` both compile with zero warnings.
2. The mainboard receives CHIPS commands over J3 UART through the ESP32 bridge.
3. The host can inject a complete sensor/input frame.
4. The mainboard reports telemetry/state back over the same link.
5. Injected scenarios produce expected state-machine transitions.
6. Injected panel voltage/current values cause MPPT duty-cycle movement.
7. The computed duty cycle is applied to the real TCC0 PWM peripheral.
8. The PWM signal can be observed on `PA12`/`PA13` or verified through register
   telemetry before any risky power-stage test.
9. The demo can be run repeatedly without reflashing for every scenario.
10. Real sensor support can be added later by changing only the input source,
    not the state machine or MPPT logic.

### What Is Deferred

The following work remains important but is not the next blocker:

- INA226 I2C support, unless the board population is confirmed.
- Full ADC calibration and engineering-unit conversion.
- SPI battery temperature sensor, because the exact part remains uncertain.
- Load-shedding/heater GPIO actuation on real hardware, until the downstream
  circuitry is reviewed for safe bench use.
- Closed-loop power-stage testing with actual PV/battery inputs.

The firmware should still be designed so all of these can slot in cleanly.
