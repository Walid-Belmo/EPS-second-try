# How To Merge — Parallel Development Plan for Phases 4, 5, 6

This document is self-contained. A Claude Code instance reading only this file
should have everything needed to merge the three parallel development branches
back into master.

---

## What Happened

We developed three phases in parallel using separate git worktrees, each with its
own Claude Code instance. The goal was to speed up development since Phases 4, 5,
and 6 have NO dependencies on each other — they all branch from the same master
commit (with Phases 0, 1, 3 complete).

```
master (Phases 0,1,3 complete)
├── branch: phase4-chips    (worktree: ../EPS-phase4-chips)
├── branch: phase5-pwm      (worktree: ../EPS-phase5-pwm)
└── branch: phase6-sensors   (worktree: ../EPS-phase6-sensors)
```

---

## What Each Branch Contains

### Branch: phase4-chips — CHIPS Communication Protocol
**New files created:**
- `src/drivers/chips_frame_encode_decode_with_crc16_kermit.c` — frame build/parse, CRC, byte stuffing
- `src/drivers/chips_frame_encode_decode_with_crc16_kermit.h`
- `src/chips_command_dispatch_and_response.c` — command handler dispatching
- `src/chips_command_dispatch_and_response.h`
- `esp32_test_harness/02_chips_protocol_test/02_chips_protocol_test.ino`

**Modified files:**
- `src/main.c` — test code for CHIPS protocol (WILL conflict, expected)
- `Makefile` — added new .c files to SRCS (append only)

**Does NOT touch:** Any existing driver in `src/drivers/` (clock, debug, uart_obc)

### Branch: phase5-pwm — TCC0 Complementary PWM with Dead-Time
**New files created:**
- `src/drivers/pwm_buck_converter_complementary_tcc0_with_dead_time.c`
- `src/drivers/pwm_buck_converter_complementary_tcc0_with_dead_time.h`
- `esp32_test_harness/03_pwm_frequency_and_duty_measurement/` (if created)

**Modified files:**
- `src/main.c` — test code for PWM (WILL conflict, expected)
- `Makefile` — added new .c files to SRCS (append only)

**Does NOT touch:** Any existing driver in `src/drivers/`

### Branch: phase6-sensors — I2C, ADC, SPI Sensor Drivers
**New files created:**
- `src/drivers/i2c_master_read_write_on_sercom3.c/.h`
- `src/drivers/ina226_read_voltage_and_current_via_i2c.c/.h`
- `src/drivers/adc_initialize_and_read_single_channel.c/.h`
- `src/drivers/spi_master_read_write_on_sercom4.c/.h`
- `src/drivers/battery_temperature_read_via_spi.c/.h`
- `esp32_test_harness/04_sensor_simulator/`

**Modified files:**
- `src/main.c` — test code for sensors (WILL conflict, expected)
- `Makefile` — added new .c files to SRCS (append only)

**Does NOT touch:** Any existing driver in `src/drivers/`

---

## Pin Assignments (No Conflicts Between Branches)

Each branch uses different hardware pins. There are ZERO pin overlaps:

| Branch | Peripheral | SERCOM/TCC | Pins |
|---|---|---|---|
| master (Phase 3) | OBC UART | SERCOM0 | PA04 (TX), PA05 (RX) |
| master (Phase 1) | Debug UART | SERCOM5 | PA22 (TX) |
| phase5-pwm | TCC0 PWM | TCC0 DTI ch2 | PA18 (WO[2]), PA20 (WO[6]) |
| phase6-sensors | I2C | SERCOM3 | PA16 (SDA), PA17 (SCL) |
| phase6-sensors | SPI | SERCOM4 | PA12, PA13, PA14, PA15 |
| phase6-sensors | ADC | ADC | Various AIN (not above pins) |

---

## How To Merge — Step by Step

### Prerequisites
- All three branches have been tested and pass their criteria
- You are on the master branch in the main repository

### Step 1: Verify clean master state
```bash
cd c:\Users\iceoc\Documents\EPS-second-try
git status          # should be clean
git branch          # should show * master
```

### Step 2: Merge phase4-chips
```bash
git merge phase4-chips
```
**Expected:** Conflict in `src/main.c` and possibly `Makefile`.

**How to resolve main.c:** Do NOT try to combine the test code. For now, take
phase4-chips's version of main.c (`git checkout phase4-chips -- src/main.c`).
We will write a new combined main.c after all merges.

**How to resolve Makefile:** Keep ALL `SRCS +=` lines from both sides. The order
doesn't matter — just make sure every .c file from both master and phase4-chips
appears in the SRCS list exactly once.

```bash
# After resolving conflicts:
git add src/main.c Makefile
git commit -m "merge phase4-chips into master"
```

### Step 3: Merge phase5-pwm
```bash
git merge phase5-pwm
```
**Expected:** Conflict in `src/main.c` and possibly `Makefile`.

**How to resolve main.c:** Same approach — take phase5-pwm's version for now.
```bash
git checkout phase5-pwm -- src/main.c
```

**How to resolve Makefile:** Keep ALL `SRCS +=` lines from all three sources.

```bash
git add src/main.c Makefile
git commit -m "merge phase5-pwm into master"
```

### Step 4: Merge phase6-sensors
```bash
git merge phase6-sensors
```
**Expected:** Same conflicts.

**How to resolve main.c:** Take phase6-sensors's version for now.
```bash
git checkout phase6-sensors -- src/main.c
```

**How to resolve Makefile:** Keep ALL `SRCS +=` lines.

```bash
git add src/main.c Makefile
git commit -m "merge phase6-sensors into master"
```

### Step 5: Write the combined main.c

After all three merges, the Makefile should have ALL driver .c files in SRCS.
Now write a new `src/main.c` that:

1. Includes all driver headers
2. Initializes the clock (48 MHz)
3. Initializes the debug UART (SERCOM5)
4. Initializes the OBC UART (SERCOM0)
5. Initializes the CHIPS protocol parser
6. Initializes TCC0 PWM (if Phase 5 is complete)
7. Initializes I2C, ADC, SPI (if Phase 6 is complete)
8. Runs a main loop that processes OBC UART data through CHIPS and heartbeats

Read the individual test main.c files from each branch (they're in git history)
to understand what each driver needs for initialization and what test code was used.

### Step 6: Build and verify
```bash
make clean && make    # must produce zero warnings
make flash            # must print "Verified OK"
```

Verify on COM6 that all drivers initialize without errors.

### Step 7: Clean up worktrees
```bash
git worktree remove ../EPS-phase4-chips
git worktree remove ../EPS-phase5-pwm
git worktree remove ../EPS-phase6-sensors
git branch -d phase4-chips
git branch -d phase5-pwm
git branch -d phase6-sensors
```

### Step 8: Update documentation
- Update `notes/plan.md` — mark Phases 4, 5, 6 as COMPLETE
- Update `notes/readme.md` — add new driver descriptions
- Save code sample to `code_samples/`
- Commit and push

---

## What Each Claude Code Instance Was Told

Below are the EXACT prompts given to each instance. This is useful context for
understanding what each branch should contain and what conventions were followed.

### PROMPT FOR WORKTREE A — Phase 4: CHIPS Protocol

```
You are working on the CHESS CubeSat EPS firmware project. Your task is to
implement Phase 4: the CHIPS communication protocol on the SAMD21G17D.

You are in a git worktree on branch "phase4-chips".

═══════════════════════════════════════════════════════════════════════════════
STEP 0 — READ THESE FILES FIRST (in this order, before doing anything else)
═══════════════════════════════════════════════════════════════════════════════

1. CLAUDE.md — project principles, how to build/flash/read serial
2. notes/conventions.md — MANDATORY coding standards (NASA Power of 10 + JPL)
3. notes/plan.md — full development plan (read Phase 4 section carefully)
4. notes/readme.md — project overview, driver descriptions, doc references
5. docs/uart_obc_driver.md — the OBC UART driver you will use (API, pin map,
   timing analysis, debugging tips)
6. research_logs/agent_chips_protocol_and_comms.md — deep research from the
   CHESS mission document with exact page references for the CHIPS protocol
7. src/drivers/uart_obc_sercom0_pa04_pa05.c — read the existing OBC UART
   driver to understand the API: uart_obc_send_bytes(),
   uart_obc_read_one_byte_from_receive_buffer(), etc.
8. src/drivers/debug_functions.h — the DEBUG_LOG_TEXT/UINT/INT macros

═══════════════════════════════════════════════════════════════════════════════
CRITICAL RULES — VIOLATING ANY OF THESE IS UNACCEPTABLE
═══════════════════════════════════════════════════════════════════════════════

A. RESPECT conventions.md IN EVERY FILE, EVERY FUNCTION, EVERY VARIABLE.
   No abbreviations. Names must read like English sentences. Every register
   write gets a comment explaining WHY. Every SYNCBUSY wait is commented.
   Max 60 lines per function. Min 2 assertions per function >10 lines.
   File names describe exactly what the module does.

B. DO NOT flash the SAMD21 without asking me first and waiting for my OK.
   Say "READY TO FLASH — is the board free?" and WAIT.

C. DO NOT flash the ESP32. I will do it myself from Arduino IDE. Give me
   the sketch and tell me what to flash.

D. DO NOT modify any existing driver file. The files in src/drivers/ that
   already exist (clock, debug_functions, uart_obc) are READ-ONLY. You may
   only CREATE new files and modify main.c and Makefile.

E. DO NOT blindly trust the plan. The plan is an INDICATION, not gospel.
   Before implementing anything:
   - Verify register values against datasheets/samd21_datasheet.pdf
   - Search online for how other people have implemented similar things
   - Check the DFP headers in lib/samd21-dfp/ for exact macro names
   - If the plan says something that contradicts the datasheet, the
     datasheet wins. Document the discrepancy.

F. Before writing ANY register configuration code, open the actual datasheet,
   find the specific register, and verify bit positions and field values.
   Cross-reference with the DFP header files in lib/samd21-dfp/component/.

G. START IN PLAN MODE. Read everything, do your research, then present me
   a plan. I will approve it before you write any code.

═══════════════════════════════════════════════════════════════════════════════
YOUR TASK
═══════════════════════════════════════════════════════════════════════════════

Implement the CHIPS protocol (CHESS Internal Protocol over Serial):
- HDLC-style framing with 0x7E sync bytes
- CRC-16/KERMIT checksum
- PPP-style byte stuffing (0x7E → 0x7D 0x5E, 0x7D → 0x7D 0x5D)
- Frame parser: streaming state machine (feed one byte at a time)
- Frame builder: takes command + payload, produces complete framed bytes
- Command handler: dispatches received commands, builds responses

Files to create:
- src/drivers/chips_frame_encode_decode_with_crc16_kermit.c/.h
- src/chips_command_dispatch_and_response.c/.h
- esp32_test_harness/02_chips_protocol_test/02_chips_protocol_test.ino
- A test main.c

The CRC-16/KERMIT and byte stuffing logic are PURE functions (no hardware
access). They can and should be verified against known test vectors before
touching any hardware.

Read plan.md Phase 4 for pass criteria. ALL must be met.
```

### PROMPT FOR WORKTREE B — Phase 5: TCC0 Complementary PWM

```
You are working on the CHESS CubeSat EPS firmware project. Your task is to
implement Phase 5: complementary PWM with hardware dead-time insertion on
TCC0, for driving the EPC2152 GaN half-bridge buck converter.

You are in a git worktree on branch "phase5-pwm".

═══════════════════════════════════════════════════════════════════════════════
STEP 0 — READ THESE FILES FIRST (in this order, before doing anything else)
═══════════════════════════════════════════════════════════════════════════════

1. CLAUDE.md — project principles, how to build/flash/read serial
2. notes/conventions.md — MANDATORY coding standards (NASA Power of 10 + JPL)
3. notes/plan.md — full development plan (read Phase 5 section carefully)
4. notes/readme.md — project overview, driver descriptions, doc references
5. research_logs/agent_eps_sensors_and_dcdc.md — Section 2: DC-DC Buck
   Converter. Contains switching frequency (300 kHz), EPC2152 details,
   complementary PWM requirement, dead-time analysis
6. datasheets/samd21_datasheet.pdf — Chapter 31 (TCC) is your primary
   reference. Read the DTI (Dead-Time Insertion) section carefully.
7. src/drivers/clock_configure_48mhz_dfll_open_loop.c — understand the
   clock setup (GCLK0 = 48 MHz) since TCC0 timing depends on it
8. src/drivers/debug_functions.h — the DEBUG_LOG macros for test output

═══════════════════════════════════════════════════════════════════════════════
CRITICAL RULES — VIOLATING ANY OF THESE IS UNACCEPTABLE
═══════════════════════════════════════════════════════════════════════════════

A. RESPECT conventions.md IN EVERY FILE, EVERY FUNCTION, EVERY VARIABLE.
   No abbreviations. Names must read like English sentences. Every register
   write gets a comment explaining WHY. Every SYNCBUSY wait is commented.
   Max 60 lines per function. Min 2 assertions per function >10 lines.
   File names describe exactly what the module does.

B. DO NOT flash the SAMD21 without asking me first and waiting for my OK.
   Say "READY TO FLASH — is the board free?" and WAIT.

C. DO NOT flash the ESP32. I will do it myself from Arduino IDE.

D. DO NOT modify any existing driver file. They are READ-ONLY.

E. DO NOT blindly trust the plan. Verify EVERY register value against the
   SAMD21 datasheet Chapter 31. Search online for how others configure TCC0
   with dead-time insertion on the SAMD21. If the plan says something that
   contradicts the datasheet, the datasheet wins.

F. Before writing ANY TCC0 register code, open datasheets/samd21_datasheet.pdf,
   find the exact register description, and verify bit positions. Cross-check
   with lib/samd21-dfp/component/tcc.h for the exact DFP macro names.

G. START IN PLAN MODE. Read everything, do research, present a plan. I approve
   before you write code.

═══════════════════════════════════════════════════════════════════════════════
YOUR TASK
═══════════════════════════════════════════════════════════════════════════════

Configure TCC0 for complementary PWM at 300 kHz with hardware dead-time.

Pre-verified pin assignment (from Phase 3 conflict analysis):
- TCC0 WO[2] on PA18 (mux F) — one PWM output
- TCC0 WO[6] on PA20 (mux F) — complementary PWM output
- Uses DTIEN2 (dead-time insertion on channel 2)

BUT: You MUST verify this yourself against the datasheet. Do not blindly
trust these pin assignments. Check Chapter 7 (I/O Multiplexing) Table 7-1
and Chapter 31 (TCC) DTI section.

File to create:
- src/drivers/pwm_buck_converter_complementary_tcc0_with_dead_time.c/.h

Public API should allow:
- Initialize TCC0 at 300 kHz with dead-time
- Set duty cycle (input: 0-65535 from MPPT algorithm, map to TCC0 range)

Read plan.md Phase 5 for pass criteria. ALL must be met.
```

### PROMPT FOR WORKTREE C — Phase 6: Sensor Drivers

```
You are working on the CHESS CubeSat EPS firmware project. Your task is to
implement Phase 6: I2C, ADC, and SPI sensor drivers.

You are in a git worktree on branch "phase6-sensors".

═══════════════════════════════════════════════════════════════════════════════
STEP 0 — READ THESE FILES FIRST (in this order, before doing anything else)
═══════════════════════════════════════════════════════════════════════════════

1. CLAUDE.md — project principles, how to build/flash/read serial
2. notes/conventions.md — MANDATORY coding standards (NASA Power of 10 + JPL)
3. notes/plan.md — full development plan (read Phase 6 section carefully)
4. notes/readme.md — project overview, driver descriptions, doc references
5. research_logs/agent_eps_sensors_and_dcdc.md — ALL sensor details: INA226,
   LT6108, TPS2590, temperature sensor, ADC channels, voltage dividers
6. docs/uart_obc_driver.md — see the pin conflict table to understand which
   pins are already reserved
7. datasheets/samd21_datasheet.pdf — Chapter 28 (I2C), Chapter 33 (ADC),
   Chapter 27 (SPI)
8. The INA226 datasheet (search online: "INA226 datasheet TI SBOS547")
   for register map, I2C protocol, and manufacturer ID verification

═══════════════════════════════════════════════════════════════════════════════
CRITICAL RULES — VIOLATING ANY OF THESE IS UNACCEPTABLE
═══════════════════════════════════════════════════════════════════════════════

A. RESPECT conventions.md IN EVERY FILE, EVERY FUNCTION, EVERY VARIABLE.
   No abbreviations. Names must read like English sentences. Every register
   write gets a comment explaining WHY. Max 60 lines per function.
   File names describe exactly what the module does.

B. DO NOT flash the SAMD21 without asking me first and waiting for my OK.

C. DO NOT flash the ESP32. I will do it myself from Arduino IDE.

D. DO NOT modify any existing driver file. They are READ-ONLY.

E. DO NOT blindly trust the plan. Verify EVERY register value against the
   SAMD21 datasheet. Download the INA226 datasheet from TI's website and
   verify the register addresses and data formats. Search online for how
   others configure SERCOM I2C master on the SAMD21.

F. Before writing ANY register code, verify in the datasheet AND in the
   DFP headers (lib/samd21-dfp/component/).

G. START IN PLAN MODE. Read everything, research online, present a plan.
   I approve before you write code.

H. Implement ONE driver at a time (Principle 3: one change at a time).
   Order: I2C + INA226 first, then ADC, then SPI. Get each working before
   starting the next.

═══════════════════════════════════════════════════════════════════════════════
YOUR TASK
═══════════════════════════════════════════════════════════════════════════════

Implement three sensor interfaces:

1. I2C master + INA226 power monitor
   - SERCOM3 on PA16 (SDA) / PA17 (SCL) — verify against datasheet
   - Read INA226 manufacturer ID (0x5449), bus voltage, shunt voltage
   - Files: src/drivers/i2c_master_read_write_on_sercom3.c/.h
            src/drivers/ina226_read_voltage_and_current_via_i2c.c/.h

2. ADC single-channel reader
   - SAMD21 internal ADC, 12-bit resolution
   - Reads any selected AIN channel (for voltage dividers, LT6108, TPS2590)
   - File: src/drivers/adc_initialize_and_read_single_channel.c/.h

3. SPI master + battery temperature sensor (placeholder — part unknown)
   - SERCOM4 on PA12-PA15 — verify exact PAD mapping against datasheet
   - Generic SPI read/write + placeholder temperature interpretation
   - Files: src/drivers/spi_master_read_write_on_sercom4.c/.h
            src/drivers/battery_temperature_read_via_spi.c/.h

ESP32 test sketch: esp32_test_harness/04_sensor_simulator/
(ESP32 acts as I2C slave simulating INA226, and SPI slave for temp sensor)

Read plan.md Phase 6 for pass criteria. ALL must be met.
```

---

## Why Direct Branch Merge (Not Pull Requests)

- Single-developer project — no team review needed
- PRs add ceremony with no benefit (create, describe, review, click merge)
- Direct `git merge branch-name` is simpler and conflicts are well-documented
- This file (howtomerge.md) is more detailed than any PR description could be
- Each branch's full commit history is preserved in the merge regardless
