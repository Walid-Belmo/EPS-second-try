# Defects To Correct — Source PDS

Living defect log for the `src-pds` firmware and its ESP32 bridge / web tooling.
Findings are recorded as they are discovered during testing and fixed later.
Every entry is backed by an observed fact (what was sent, what came back).

- **Test rig:** laptop → COM3 (115200) → ESP32 bridge → SAMD21 PCU board V4.1
  (`BOARD=mainboard-pds`).
- **Bridge:** `tail -f cmds.txt | plink -serial COM3 > com3.log` (manual,
  one command at a time, raw replies read back).
- **Status:** open defects are not yet fixed unless an entry explicitly says
  it has been fixed in the working tree and still needs reflash/regression.

---

## Severity legend

- **HIGH** — wrong/unsafe behavior or data the operator would trust and act on.
- **MED** — misleading behavior or a broken documented contract.
- **LOW** — cosmetic, robustness nit, or tooling gap.

---

## Fixed in working tree, pending reflash verification

### F7 - ADC reference constant assumed 3.300 V, but bench rail measured 2.86 V - HIGH

During Test B ADC calibration on `TP19` / `OUTV1`, the operator injected a
DMM-measured `0.175 V` on the ADC-side node. Firmware reported stable
`sensor_panel_v divider` values around `5.99 V`, while the physical divider
math with the measured pin voltage predicts about `5.04 V`.

- Root cause: `mainboard_adc_reader.c` configured the ADC reference as
  `INTVCC1` / VDDANA but converted raw ADC counts using a hardcoded
  `ADC_REFERENCE_MILLIVOLTS = 3300u`.
- Evidence: `TP5` measured `2.86 V` during the test. The observed error is
  consistent with `3300 / 2860 = 1.15x`.
- Fix applied in working tree on 2026-05-16: changed
  `ADC_REFERENCE_MILLIVOLTS` from `3300u` to `2860u`.
- Verification after reflash: repeat the `TP19 = 0.175 V` point. Expected
  `sensor_panel_v divider` should move from about `5.9-6.0 V` down to about
  `5.0 V`; then continue the TP19/TP20 calibration sweep.

---

## Open defects

### F1 — INA226 telemetry is failing garbage, reported as valid — HIGH

The two INA226 readings are non-repeatable noise under identical, static
conditions. Observed `sensor_*_i ina226` across successive polls with no
input change: `5 → 0 → 2 → 5 → 7 → 2 → -2 → 5 → 0`. Voltages stay `0`.
The firmware passes these through as normal telemetry with **no
stale/invalid flag**, so any consumer (state machine on real-sensor source,
MPPT on board sensors, operator) would trust dead data.

- Matches the documented wrong-I²C-address bug: firmware uses `0x40/0x41`,
  the chips answer at `0x45/0x46` (A0/A1 strap to V+).
- Evidence: every `get_values` poll in Tests 1–5 and the manual-mode steps.
- Fix direction: correct the INA226 addresses **and** make a failed I²C
  read mark the reading invalid instead of returning a stale/zero value.

### F2 — Stale telemetry survives an explicit `off` — MED

After `off` (and even at boot before anything ran), the board reports
`pcu=MPPT_CHARGE`, `state_duty=32768` (50 %), `loads=0x1F` (all loads on),
and injected default `inputs` as if live. `off` zeroes PWM and MPPT fields
but does **not** sanitize the state-machine / load snapshot fields.

- An operator polling a freshly-booted or safed board is shown phantom
  "50 % duty / MPPT charging / all loads enabled".
- Evidence: Test 1 (boot), Test 2 (after explicit `off` — values persisted).
- Fix direction: `handle_off_command` (and boot defaults) should also reset
  `pcu_mode`, `state_duty`, `load_mask` to honest "not running" values.

### F3 — `fields=` mask is cosmetic; full 125-byte packet always sent — MED

`get_values fields=X` / `stream_values ... fields=X` never shrinks the
packet. The firmware always transmits the full 125-byte status structure
(fixed layout); the mask only filters what the ESP32 *prints*. Manual block
and the entire sensor-reads block are sent every frame regardless.

- Contradicts the command-contract intent ("ask for smaller packets
  without changing commands"). Pure bandwidth waste when streaming.
- Evidence: Test 3 (`fields=mode,pwm,manual,commands` → `requested_fields=0x83`,
  manual/sensor blocks still present), Test 4 (every 200 ms frame = 125 bytes).

### F4 — ESP32 text parser silently ignores unknown field names — LOW

`parse_field_mask` on the ESP32 only knows
`mode,pwm,state,loads,faults,mppt,inputs,commands`. Names `manual` and
`sensor_reads` are silently dropped (no error to the operator).

- Evidence: Test 3, `fields=...,manual,...` produced mask `0x83` (manual
  bit `0x100` absent).
- Fix direction: add the missing names, or reject unknown field names.

### F5 — Stray frame counted "valid" but never executed at startup — LOW

First post-boot poll showed `valid_frames=2 executed=1` after exactly one
command. Steady-state increments are correct (+1/+1 per command). A stray
frame (line noise / ESP32-reset DTR glitch) was CRC-framed and counted as
a valid frame without being a real command.

- Evidence: Test 1 vs Test 2 counter deltas.
- Fix direction: only count a frame "valid" once it is accepted for
  dispatch, or expose a separate "framed-but-rejected" counter.

### F6 — Telemetry lags one loop iteration for ALL mirrored fields — MED (systemic)

Every status reply is built by the command handler **before** the same
main-loop iteration runs the mode runner that writes the snapshot mirror
fields (PWM requested/applied/enabled, `status_led_is_on`,
`panel_efuse_is_enabled`, eFuse status). So any "set X then read X"
sequence reports the **old** value. Worse: when two commands arrive in the
same UART drain (e.g. a `set_*` immediately followed by `get_values`),
*both* replies are built before the runner executes, so even the explicit
follow-up poll is stale by one iteration.

- Confirmed on three independent outputs:
  - `set_manual_pwm duty=13107` reply → `applied=0`; next poll → `13107`.
  - `set_manual_pwm duty=32768` reply → `applied=13107`; next poll → `32768`.
  - `set_manual_led off` reply **and** the batched follow-up poll → both
    `led_is_on=1`; the LED was **physically off** (actuation correct).
- Impact: the operator cannot trust status during exactly the moment they
  are verifying a command worked. A working board looks broken.
- Root cause: replies are emitted from inside
  `read_and_execute_commands_from_esp32()`, which runs before the mode
  runner / safety pass / `apply_outputs_to_board()` in the main loop.
- Fix direction (deferred by decision — see bench log): defer reply
  emission until after `apply_outputs_to_board()` in the same iteration,
  and support more than one pending reply per iteration (batched commands).
  Needs rebuild + reflash + re-run of Tests 1–5 regression.

---

## Observations to verify later (not yet classified as defects)

- **eFuse PGOOD reads HIGH with eFuses disabled.** In manual mode with
  `pv_req=0 bat_req=0` and only 3.3 V on J5, the real PA18/PA19 reads gave
  `pv_pgood=1 bat_pgood=1`. Could be open-drain pull-up behavior with no
  load, or inverted/incorrect read logic. Revisit during Test C (eFuse
  switching) with the rails actually powered.

---

## Confirmed working (adversarially tested, held up)

- Board runs its main loop reliably (monotonic uptime, consistent replies
  across all commands).
- Manual-mode gate: `set_manual_pwm` without `enter_manual` → rejected
  (`command_not_available`), **zero side effects**.
- Input validation: stream period under-min / over-max, out-of-range
  state-demo inputs → all rejected **before** any state change.
- CHIPS link reliability: 188-frame / ~38 s stream soak at 200 ms,
  **0 CRC errors, 0 framing errors**, perfect cadence; `stream off`
  halts cleanly.
- `off` electrically safes the converter (`requested=applied=enabled=0`).

---

## Bench-test log (Phase 2 — hardware, supervised)

| Test | Setup | Command(s) | Result |
|---|---|---|---|
| A (20 %) | 3.3 V on J5 only; buck input cold; scope on PA12/PA13 | `enter_manual` → `set_manual_pwm duty=13107` | **PASS** — operator confirms clean complementary signals on PWM_H/PWM_L; firmware `applied=13107 enabled=1`. |
| A (50 %) | same | `set_manual_pwm duty=32768` | **PASS** — operator confirms ~50 % duty looks correct; firmware `applied=32768 enabled=1`. Frequency / dead-time accepted as already proven in a prior session (not re-measured this run). |
| B1 (LED) | same | `set_manual_led on` → `off` | **PASS (actuation)** — operator confirms green LED2 solid ON (no heartbeat blink-fight; manual override owns PB22), then OFF. Telemetry showed F6 staleness (`led_is_on` lagged) but the pin was physically correct. |

**Decision (logged):** F6 fix deferred until after the wired bench tests
(operator choice) to avoid conflating a firmware change with hardware
behavior mid-session and to keep the scoped setup intact.

*(Append further bench results here as they are collected.)*
