# Source PDS Implementation Plan

> **⚠️ STATUS DISCLAIMER (added 2026-05-16) — read before trusting this file.**
>
> This is a **historical design record, not a live plan**. The build phase
> it describes is finished; the project is now in hardware testing.
>
> - **Trust** the design sections: "Rule For Every Coding Step", "Main
>   Reference Files In `src`", and "Big Implementation Steps 1–8". They
>   accurately explain *why* `src-pds` is structured the way it is and how
>   each module maps back to `src/`.
> - **Do NOT trust** any status / "Current Blocking Items" / page-status
>   line at face value — some are stale (corrected inline below with
>   dated `[UPDATE]` notes; others may still be wrong).
> - **The Step-1 command list is the *initial* small set, not the shipped
>   contract.** The firmware actually ships 14 commands (incl. the manual-
>   control set `0x38–0x3D` and `READ_MPPT_INPUT_SAMPLE 0x37`). The
>   authoritative list is
>   `src-pds/board_command_contract/board_command_ids_and_payload_layouts.h`.
>   Manual-control mode (a whole firmware mode + 5 commands + a web page)
>   was delivered but is **not** among the eight "Big Implementation Steps".
> - For the **real current state** use, in this order:
>   `src-pds/context.md`, `src-pds/defects_to_correct.md`,
>   `src-pds/What's TP for what test.md`. Unlike `notes/plan.md` (pure
>   historical noise — disregard entirely), this file still has design
>   value, which is why it is kept rather than deleted.

This file is the working plan for `src-pds`.
The goal is to rebuild the EPS firmware with clearer names, clearer files, and
the same proven behavior as the working code in `src`.

## Rule For Every Coding Step

Before changing `src-pds`, do this:

1. Read the matching working code in `src`.
2. Write the small to-do list for the current step.
3. Code only that step.
4. Reread the code that was just written.
5. Check whether each function name can be understood as plain English.
6. Check whether each comment explains why the code exists.
7. Remove needless complexity before moving on.
8. Build or compile-check when the step reaches real C code.

The working code is the reference for behavior.
The new code is allowed to rename and reorganize things, but it must not invent
different behavior by accident.

## Main Reference Files In `src`

- `src/main_mainboard_chips_injection_demo.c`
  - Reference for board startup, UART command reading, CHIPS parsing, and the
    current main loop timing.
- `src/eps_demo_chips_command_dispatch.c`
  - Reference for command handling, command replies, telemetry, injected
    sensor values, MPPT demo execution, and state-machine demo execution.
- `src/eps_state_machine.c`
  - Reference for PCU state transitions, safe mode checks, load shedding, and
    actuator decisions.
- `src/eps_state_machine.h`
  - Reference for the state-machine inputs and outputs.
- `src/mppt_algorithm.c`
  - Reference for the incremental conductance MPPT algorithm.
- `src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.c`
  - Reference for CHIPS packet reading and writing.
- `src/drivers/uart_obc_sercom0_pa10_pa11_on_mainboard.c`
  - Reference for the UART link between ESP32 and the board.
- `src/drivers/pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c`
  - Reference for the real PWM hardware output.

## Big Implementation Steps

### 1. Board Command Contract

Goal: define exactly what CHIPS commands the ESP32 can send to the board.

To do:

- Define command names and numeric command values.
- Define each payload byte layout.
- Keep the names aligned with `command_architecture.md`.
- Keep the command list small:
  - `BOARD_OFF`
  - `BOARD_RUN_PWM`
  - `BOARD_START_MPPT_DEMO`
  - `BOARD_START_STATE_DEMO`
  - `BOARD_INJECT_STATE_INPUTS`
  - `BOARD_GET_VALUES`
  - `BOARD_STREAM_VALUES`

Reference first:

- `src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.h`
- `src/eps_demo_chips_command_dispatch.c`

### 2. Command-Controlled RAM Values

Goal: create one clear place for values changed by commands.

To do:

- Store the requested mode.
- Store the requested fixed PWM duty cycle.
- Store injected state-transition inputs.
- Store MPPT demo curve settings.
- Store telemetry streaming settings.
- Store the last command status for debugging.

Reference first:

- `src/eps_demo_chips_command_dispatch.c`

### 3. CHIPS Command Handling

Goal: make `read_and_execute_commands_from_esp32()` actually execute commands.

To do:

- Decode received command ID.
- Validate payload length.
- Validate parameter ranges.
- Update RAM only after validation passes.
- Send a clear success or error reply to ESP32.
- Count broken packets without changing mode or outputs.

Reference first:

- `src/main_mainboard_chips_injection_demo.c`
- `src/eps_demo_chips_command_dispatch.c`
- `src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.c`

### 4. Fixed PWM Test

Goal: allow real board testing by setting one PWM value manually.

To do:

- `BOARD_RUN_PWM` sets requested mode to fixed PWM test.
- Store the requested duty cycle.
- Apply that duty cycle in `run_fixed_pwm_test_only()`.
- Keep `BOARD_OFF` able to force PWM back to zero.

Reference first:

- `src/eps_demo_chips_command_dispatch.c`
- `src/drivers/pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c`

### 5. MPPT Demo

Goal: run the real MPPT algorithm on simulated panel values.

To do:

- Store a quadratic curve: `I = A*V^2 + B*V + C`.
- Use PWM duty cycle and battery voltage to estimate panel voltage.
- Use the curve to compute simulated panel current.
- Feed those simulated raw values into `mppt_algorithm_run_one_iteration()`.
- Store duty cycle, voltage, current, and power for display.

Reference first:

- `src/mppt_algorithm.c`
- `src/eps_demo_chips_command_dispatch.c`

### 6. State Transition Demo

Goal: run the real state-machine logic using injected values.

To do:

- Store injected values:
  - battery voltage
  - battery current
  - panel voltage
  - panel current
  - charging rail voltage
  - temperature
  - heartbeat
  - OBC mode
  - safe substate
  - faults
- Convert those values into `struct eps_sensor_readings_this_iteration`.
- Run `eps_state_machine_run_one_iteration()`.
- Store the chosen PCU mode, safe mode state, load states, heater state, and PWM.

Reference first:

- `src/eps_state_machine.c`
- `src/eps_state_machine.h`
- `src/eps_demo_chips_command_dispatch.c`

### 7. Status And Streaming

Goal: make the computer UI able to observe what the board is doing.

To do:

- Define the exact field names used by `BOARD_GET_VALUES`.
- Define the exact field mask or field list used by `BOARD_STREAM_VALUES`.
- Send current mode, PWM, PCU state, injected values, MPPT values, load states,
  fault state, and last command result.

Reference first:

- `src/eps_demo_chips_command_dispatch.c`

### 8. Hardware Startup And Output Application

Goal: make the top-level functions in `main.c` connect to real board drivers.

To do:

- Clock setup calls the existing 48 MHz clock driver.
- UART setup calls the existing SERCOM0 UART driver.
- Sensor setup calls the existing ADC driver.
- PWM setup calls the existing PWM driver and starts at zero duty.
- Output application writes final allowed PWM and output states to hardware.

Reference first:

- `src/main_mainboard_chips_injection_demo.c`
- `src/drivers/clock_configure_48mhz_dfll_open_loop.c`
- `src/drivers/uart_obc_sercom0_pa10_pa11_on_mainboard.c`
- `src/drivers/mainboard_adc_reader.c`
- `src/drivers/pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c`

## Local Web Demo Pages

Goal: create local pages that make the important Source PDS behaviors visible
to someone who is not already inside the project.

All pages live in one unified Python web app:

```text
src-pds/local_web_app/
```

Run it with:

```bash
python src-pds/local_web_app/run_local_web_app.py
```

One Python process owns the ESP32 USB serial link and routes each page by
URL path. Adding a new page is a new actions module + a new `static/<page>/`
folder, no new process.

See `src-pds/local_web_app/README.md` for the route table and architecture.

### 1. MPPT Convergence Page — `/mppt`

Lets the user choose a simulated solar-panel I-V curve, plots the I-V and
P-V curves, sends `start_mppt_demo`, and tracks the board-reported voltage,
current, power, and duty cycle while the MPPT algorithm runs.

Status: working.

### 2. State Transition Page — `/state`

Runs 12 preset scenarios that exercise the PCU state machine (all four
normal PCU modes, all three safe-mode entry reasons, all four safe-mode
sub-state load masks). Each scenario injects sensor values, waits for
steady-state, and compares the firmware's observed snapshot against
expected outputs.

Status: working. Backed by the pure-dispatcher firmware in
`src-pds/state_machine_pure_logic/`. The flight-grade target lives in
`src-pds/state-transitions.md`.

### 3. Manual Control Page — `/manual`

Lets the operator drive each of the four user-facing board outputs
independently — PWM duty, PV-side eFuse switch, battery-side eFuse switch,
and the green status LED — through a new firmware mode
(`PDS_REQUESTED_MODE_MANUAL = 5`). Off-first convention is enforced in the
UI; the four `set_manual_*` commands are rejected by the firmware unless
the chip is already in manual mode.

Status: working on `BOARD=devboard-pds` (PWM physically moves; PV / BAT /
LED commands round-trip and the readbacks mirror the requested values
because the corresponding pins are not wired on the dev board). Not yet
verified on the real PCU board V4.1. See `src-pds/manual_control_mode.md`
for full details.

> **[UPDATE 2026-05-16]** Now PARTIALLY verified on the real PCU board
> V4.1: `enter_manual` works, `set_manual_pwm` drives real PA12/PA13
> (scoped at ~20 % and ~50 %), `set_manual_led` drives PB22 (green LED2
> confirmed on/off, no heartbeat blink-fight). Still unverified: the
> PV/BAT eFuse *rails* (the rails were unpowered — a bench-setup gap,
> not a firmware gap). See `src-pds/defects_to_correct.md` (bench log)
> and `src-pds/What's TP for what test.md`.

### 4. Communication And Status Page — `/comm`

Demonstrates the computer → ESP32 → SAMD21 command path. Shows
`get_values` and `stream_values`, packet counters, last command, last
command status, and visible serial traffic.

Status: not implemented yet.

## Current Blocking Items

- The communication/status page (page 4) is not implemented yet. The
  unified server architecture is in place so adding it is a new actions
  module plus a new static folder.
- Manual control mode has not yet been verified on the real PCU board
  V4.1 — only on the Curiosity Nano dev board, where the PV / BAT / LED
  pins are not physically present (`src-pds/manual_control_mode.md`
  explains the mirror behaviour that keeps the demo meaningful anyway).
  > **[UPDATE 2026-05-16] — partially resolved.** Manual mode IS now
  > verified on the real PCU V4.1 for PWM (PA12/PA13, scoped) and the
  > status LED (PB22). Only the PV/BAT eFuse rails remain unverified
  > (rails unpowered — bench-setup gap, not firmware). New testing-phase
  > blockers (not build-phase) are tracked in `defects_to_correct.md`:
  > F1 INA226 telemetry garbage, F6 telemetry one-iteration lag, and the
  > wrong TP assignments in `how_to_test.md` (corrected in
  > `What's TP for what test.md`).
- The old root-level `server.py` should not be used as the reference for
  the new Source PDS web app.
