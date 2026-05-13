# Source PDS Implementation Plan

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

## Current Blocking Items

- `src-pds` is not yet wired into the Makefile.
- The ESP32 text command parser is not yet written in this folder.
- The board command handlers are not implemented yet.
- The MPPT curve simulator is not implemented yet.
- The state-transition demo is not connected to `eps_state_machine` yet.
- Status field names and payload format still need to be finalized.
