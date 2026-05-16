# testing_history.md — append-only test record

## THE RULE

**Every time a test is run against the firmware or hardware, it MUST be
appended here as a new entry, before moving on.** No exceptions. This
file is the single chronological record of what was tested, on what
firmware, and what was learned. A test that is not written here did not
happen (for the purposes of the report and of any future instance trying
to understand the system).

This file is **append-only**: never rewrite or delete past entries. If a
later test contradicts an earlier result, add a *new* entry that
references the old one — do not edit history.

Entries and addenda must be readable prose, not just terse checklists. A
future reader should understand the full bench setup, what was powered,
what was disconnected, exactly where probes/leads were placed, what command
state the firmware was in, what was observed, how the conclusion was
calculated, and any relevant caveats or safety limits without needing the
live chat transcript.

### Entry schema (copy this for every new test)

```
### <SESSION-DATE> · Test <ID> — <short name>

- **Firmware under test:** <branch / commit or "working tree pre-commit">,
  `BOARD=<target>`, status payload v<N>, build text size <bytes> if known.
- **Hardware / setup:** <board, what is powered, what is wired, scope/DMM>.
- **Goal / what it probes:** <one line — and is it adversarial?>
- **Command(s) sent:** <exact text command(s) over the COM3 bridge>
- **Reply / observation:** <what came back / what the scope/meter showed>
- **Verdict:** PASS / FAIL / PARTIAL / INCONCLUSIVE
- **Defects produced or confirmed:** <F-numbers in defects_to_correct.md, or none>
- **Notes / caveats:** <F6 read-back caveat, methodology, follow-ups>
```

`PARTIAL` = the firmware/logic path was confirmed but the physical proof
was deferred. `INCONCLUSIVE` = the test could not be trusted (rig issue).

---

## Firmware identity for the 2026-05-16 session

- **Source:** `src-pds` working tree, branch `projet-de-semestre-version`,
  **pre-commit (uncommitted changes)** at test time.
- **Build:** `make BOARD=mainboard-pds` (SAMD21J17D, real
  `pwm_buck_converter_tcc0_pa12_pa13_on_mainboard` driver), clean build,
  zero warnings, ELF text = 23956 bytes. Status payload **version 4**.
- **Flash caveat hit this session:** the Makefile emits no `.hex`; a
  stale `build/satellite_firmware.hex` was being flashed. Regenerated via
  `arm-none-eabi-objcopy -O ihex`. **Root cause of the early "nothing
  works": the board was not reset after flashing (reset pin not
  connected) — it kept running old firmware.**
- **Bridge:** `tail -f cmds.txt | plink -serial COM3 -sercfg
  115200,8,n,1,N > com3.log`. Raw transcripts are in the scratch
  `com3.log` (not committed).

---

## Session 2026-05-16 — Phase 1: zero-actuation adversarial protocol tests

Goal of the phase: attack the command/CHIPS/telemetry layer to find
defects (not to confirm success). Board in default state, no actuation.

### 2026-05-16 · Test 1 — Liveness / `get_values`

- **Firmware:** mainboard-pds, payload v4 (as above).
- **Setup:** board powered, ESP32 bridge, no actuation.
- **Goal:** is the board running its main loop and answering CHIPS at all?
- **Command(s) sent:** `get_values fields=all`
- **Reply / observation:** 125-byte status, `status=success version=4`,
  `timestamp_ms=458037` (uptime climbing), counters `valid=2 executed=1`,
  `mode=off`, `pwm 0/0/0`, INA226 currents = constant garbage.
- **Verdict:** PASS (board alive, main loop servicing CHIPS).
- **Defects produced or confirmed:** F1 (INA226 garbage), F2 (stale
  `pcu=MPPT_CHARGE`/`state_duty=32768`/`loads=0x1F` in OFF), F3/F4
  (field-mask cosmetic + unknown field names dropped), F5 (startup
  `valid=2 executed=1` stray frame).
- **Notes:** byte math verified (84+10+31 = 125), framing sound.

### 2026-05-16 · Test 2 — `off` safety + counter behavior

- **Setup:** same.
- **Goal:** does `off` force the converter electrically safe; do counters
  increment correctly?
- **Command(s) sent:** `off` → then `get_values fields=all`
- **Reply / observation:** `off` → ACK `success`. Poll: `valid 2→4
  executed 1→3` (steady **+1/+1 per command** confirmed); `pwm
  requested=applied=enabled=0`; but `pcu=MPPT_CHARGE`, `state_duty=32768`,
  `loads=0x1F` persisted; INA226 panel_i flipped 5→0.
- **Verdict:** PASS (PWM safed) / defect confirmed.
- **Defects:** F2 confirmed (stale telemetry survives an explicit `off`),
  F1 confirmed (non-repeatable INA226), F5 (the anomaly is startup-only).

### 2026-05-16 · Test 3 — Manual-mode gate (adversarial)

- **Goal:** can PWM be set without entering manual mode? (must be rejected)
- **Command(s) sent:** `set_manual_pwm duty=50000` (mode still `off`) →
  `get_values fields=mode,pwm,manual,commands`
- **Reply / observation:** `status=command_not_available (4)`. Poll:
  `mode=off`, `manual pwm=0`, `last_command=0x39
  last_status=command_not_available` — **zero side effects**.
  `requested_fields=0x83` (manual bit 0x100 dropped).
- **Verdict:** PASS (gate solid, no leakage).
- **Defects:** F4 (ESP32 silently drops `manual` field name), F3, F1.

### 2026-05-16 · Test 4 — Stream bounds + 38 s soak

- **Goal:** reject illegal stream periods; is the CHIPS link reliable
  under sustained streaming?
- **Command(s) sent:** `stream_values on period=10` → `... period=60000`
  → `stream_values on period=200 fields=mode,pwm,commands` → (soak) →
  `stream_values off`
- **Reply / observation:** period 10 and 60000 → `value_out_of_range (3)`.
  period 200 → enabled; **188 frames over ~38 s, exact 200 ms cadence, 0
  CRC errors, 0 framing errors**; `stream_values off` halted it cleanly
  (log frozen).
- **Verdict:** PASS (validation solid; link robust — partial evidence for
  the mission "CHIPS stress" requirement).
- **Defects:** F3 reconfirmed (every frame full 125 B regardless of
  `fields=`).
- **Notes:** I initially suspected a bridge-starvation bug; **retracted**
  — the delay was my `tail -f` ~1 s rig latency, not firmware. Recorded so
  it isn't re-investigated.

### 2026-05-16 · Test 5 — State-demo input validation (adversarial)

- **Goal:** does `start_state_demo` reject out-of-range inputs *before*
  changing mode (no actuation leak)?
- **Command(s) sent:** `start_state_demo battery_voltage=65000 ...
  obc_mode=200 ...` → `get_values fields=mode,commands`
- **Reply / observation:** `value_out_of_range (3)`. Poll: `mode=off`
  unchanged, `last_command=0x33 last_status=value_out_of_range`, zero
  side effects.
- **Verdict:** PASS (validator rejects pre-mode-change).

---

## Session 2026-05-16 — Phase 2: supervised hardware bench tests

Setup unless noted: only **3.3 V on J5**, buck input cold, no PV/battery;
oscilloscope on **PA12 (PWM_H) / PA13 (PWM_L)**; manual mode.

### 2026-05-16 · Test A(20%) — PWM waveform @ ~20 %

- **Goal:** does firmware physically drive the buck gate pins correctly?
- **Command(s) sent:** `enter_manual` → `set_manual_pwm duty=13107`
- **Reply / observation:** `mode=manual`; immediate reply `applied=0`
  (F6 race); separate poll → `requested=13107 applied=13107 enabled=1`.
  **Operator: clean complementary signals on PWM_H/PWM_L.**
- **Verdict:** PASS.
- **Defects:** F6 (telemetry one-iteration lag) first observed here.

### 2026-05-16 · Test A(50%) — PWM waveform @ ~50 %

- **Command(s) sent:** `set_manual_pwm duty=32768`
- **Reply / observation:** separate poll → `applied=32768 enabled=1`.
  **Operator: ~50 % duty looks correct.** Frequency / dead-time accepted
  as proven in a prior session (not re-measured this run).
- **Verdict:** PASS.
- **Defects:** F6 reconfirmed.

### 2026-05-16 · Test B1 — Status LED (PB22)

- **Command(s) sent:** `set_manual_led on` → `set_manual_led off`
- **Reply / observation:** ON: poll `led_req=1 led_is_on=1`; **operator:
  green LED2 solid ON, no heartbeat blink-fight**. OFF: batched poll
  still showed `led_is_on=1` (F6), but **operator confirmed LED
  physically OFF**.
- **Verdict:** PASS (actuation correct).
- **Defects:** F6 generalized — telemetry lag affects *all* mirrored
  fields (LED/PWM/eFuse), not just `set_manual_pwm`.

### 2026-05-16 · Test B2 — PV eFuse enable (PA16), logic only

- **Setup:** rails unpowered (logic-only check; no PV source).
- **Command(s) sent:** `set_manual_pv on` → (separate) `get_values
  fields=all`
- **Reply / observation:** `pv_req=1`; separated poll (F6-free) →
  `outputs panel_efuse=1`. Physical PA16 pin **not probed** — no
  accessible pad on the QFN; this triggered the full TP-mapping research
  (`What's TP for what test.md`).
- **Verdict:** PARTIAL — firmware/telemetry round-trip confirmed;
  physical eFuse proof **deferred** to a powered test probing **TP10**
  (`/+PV`, post-eFuse) per `What's TP for what test.md`.
- **Notes:** Demonstrated the F6 workaround: send `set_*` and the poll as
  *separate* commands (different loop iterations) → telemetry is fresh.

### 2026-05-16 · Test B2 setup check — PV source through 100 ohm resistor

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** bench supply set to 12.2 V with 200 mA current
  limit, supply positive routed through a 100 ohm series resistor to
  `J4/PV_RAW`, supply negative on board GND (`J5` GND), DMM probing PV input.
- **Goal / what it probes:** verify the PV source actually reaches
  `PV_RAW`/TP8 above the PV eFuse UVLO threshold before running the powered
  PA16 eFuse proof.
- **Command(s) sent:** none.
- **Reply / observation:** operator reported supply current 0.114 A, only
  about 1 V at `PV_RAW`, and about 12 V across the 100 ohm resistor. The
  resistor was dropping almost the full supply, so IC4 would remain below
  its 9.52 V UVLO threshold.
- **Verdict:** INCONCLUSIVE; powered eFuse toggle not run.
- **Defects produced or confirmed:** none.
- **Notes / caveats:** the 100 ohm resistor is too large for this setup
  because the PV input path is not high impedance when powered. Continue
  only after rewiring with the supply current limit as the protection and
  confirming TP8/PV_RAW is about 12 V.

### 2026-05-16 · Test B2 setup check — PV source direct, current limit clamps

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** 100 ohm resistor removed, bench supply connected
  directly to `J4/PV_RAW` and board GND with current limit set around
  0.112 A.
- **Goal / what it probes:** verify direct PV source wiring before running
  the powered PA16 eFuse proof.
- **Command(s) sent:** none.
- **Reply / observation:** operator reported the supply entered current
  limit and its output voltage collapsed to about 1.6 V.
- **Verdict:** INCONCLUSIVE; powered eFuse toggle not run.
- **Defects produced or confirmed:** none.
- **Notes / caveats:** do not increase current or actuate firmware until
  the wiring is verified. Next check is power-off resistance/continuity
  from each J4 screw terminal to board GND to confirm the PV_RAW terminal
  is not being confused with J4 GND or shorted.

### 2026-05-16 · Test B2 setup check — PV source direct, 200 mA still clamps

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** bench supply connected directly to `J4/PV_RAW`
  and board GND, current limit raised to 0.2 A.
- **Goal / what it probes:** determine whether the previous 0.112 A
  current-limit collapse was just an undersized limit or evidence of a
  low-resistance PV input path.
- **Command(s) sent:** none.
- **Reply / observation:** operator reported that with a 0.2 A current
  limit the supply still clamped, reaching only about 1.8 V.
- **Verdict:** INCONCLUSIVE; powered eFuse toggle not run.
- **Defects produced or confirmed:** none.
- **Notes / caveats:** this behaves like an effective input resistance of
  roughly 9 ohm while current-limited. Do not raise the current further or
  add a parallel resistor; next step is an unpowered ohms/continuity check
  from `PV_RAW` to board GND and confirmation of the exact J4 screw pinout.

### 2026-05-16 · Test B2 logic attempt — PV eFuse TP9 toggle

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** PV supply disconnected/off; logic-only eFuse check
  intended with DMM on `TP9` (`/CTRL/BAT_OC_F`, PV eFuse enable/fault-gate
  net) to board GND.
- **Goal / what it probes:** safely prove firmware command path to the PV
  eFuse logic node without powering `PV_RAW`.
- **Command(s) sent:** `enter_manual` → `set_manual_pv on` →
  `get_values fields=all`.
- **Reply / observation:** ESP32 bridge forwarded commands
  `0x38`, `0x3A`, and `0x35`, but no new `[BOARD] reply` lines appeared.
  Operator reported `TP9` remained at 0 V after the command sequence.
- **Verdict:** INCONCLUSIVE.
- **Defects produced or confirmed:** none yet.
- **Notes / caveats:** cannot interpret TP9 as a firmware-control failure
  while the SAMD21 is not replying. Also, TP9 is a wired/fault-gated logic
  net; the CTRL schematic notes `CTRL_EN_OUT` is low if any fault flag is
  low, otherwise high-Z, so it may be held low by hardware even when PA16 is
  requested high. Next check: confirm `TP5` logic power and restore board
  replies before repeating or test the BAT eFuse logic point `TP15`.

### 2026-05-16 · Test B2 diagnostic — TP5 logic rail underpowered

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** after the inconclusive TP9 logic attempt, DMM on
  `TP5` (`/AUX/3V3`, MCU logic rail) to board GND.
- **Goal / what it probes:** determine why the SAMD21 stopped replying to
  COM3 bridge commands during the logic-only eFuse test.
- **Command(s) sent:** none.
- **Reply / observation:** operator measured `TP5 = 1.9 V`; expected
  logic rail is about 3.0-3.3 V.
- **Verdict:** FAIL for test rig readiness; firmware/eFuse result remains
  INCONCLUSIVE.
- **Defects produced or confirmed:** none.
- **Notes / caveats:** with TP5 at 1.9 V, the SAMD21 is likely in brownout
  or unstable reset, so missing command replies and a static TP9 are not
  meaningful firmware evidence. Restore the J5 3.3 V supply path before any
  further command-based tests.

### 2026-05-16 · Test B2 logic retry — PV eFuse request asserted

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** PV supply disconnected/off; J5 logic ground
  reconnected; direct COM3 serial session used instead of stale `cmds.txt`
  bridge; operator measuring `TP9` to board GND.
- **Goal / what it probes:** safely verify that the firmware command path
  can assert the PV eFuse request in manual mode before interpreting the
  physical TP9 logic node.
- **Command(s) sent:** `get_values fields=all` liveness checks, then
  `enter_manual` → `set_manual_pv on` → `get_values fields=all`.
- **Reply / observation:** liveness restored (`status=success version=4`).
  `enter_manual` succeeded (`mode=manual`). `set_manual_pv on` succeeded
  with `manual pv_req=1`. Separate poll reported `mode=manual`,
  `outputs panel_efuse=1`, `manual pv_req=1`, `pv_pgood=1`, `pv_flt=0`.
- **Verdict:** PARTIAL; firmware request path confirmed, physical TP9
  voltage still awaiting operator reading.
- **Defects produced or confirmed:** none yet.
- **Notes / caveats:** TP9 is a fault-gated/wired logic node, so physical
  interpretation depends on the measured transition and may not match the
  raw `panel_efuse` software request one-to-one.

### 2026-05-16 · Test B2 logic proof — PV eFuse TP9 toggles with firmware

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** PV supply disconnected/off; board logic powered
  from J5; DMM black on `J5 GND`, red on `TP9`
  (`/CTRL/BAT_OC_F`, PV eFuse enable/fault-gate net). `TP5` logic rail
  measured about 2.8 V during the test.
- **Goal / what it probes:** safely prove that the firmware manual PV eFuse
  command physically reaches the PV eFuse logic path without powering the
  PV rail.
- **Command(s) sent:** prior state `enter_manual` and `set_manual_pv on`;
  then `set_manual_pv off` → `get_values fields=all`.
- **Reply / observation:** with `set_manual_pv on`, separate poll reported
  `mode=manual`, `manual pv_req=1`, `outputs panel_efuse=1`; operator
  measured `TP9 = 2.8 V` and `TP5 = 2.8 V`. After `set_manual_pv off`,
  separate poll reported `manual pv_req=0`, `outputs panel_efuse=0`;
  operator measured `TP9` back at `0 V`.
- **Verdict:** PASS for PV eFuse logic control.
- **Defects produced or confirmed:** none.
- **Notes / caveats:** this proves the firmware controls the PV eFuse
  logic/enable path (`PA16` through the board logic to TP9). It does not
  prove IC4 passes PV power at `TP10`; that powered test remains deferred
  until the `PV_RAW` input-current behavior is understood and TP8 can be
  held above the IC4 UVLO threshold.

### 2026-05-16 · BAT eFuse logic proof — TP15 toggles with firmware

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** battery power path not energized; board logic
  powered from J5; DMM black on board GND, red on `TP15`
  (`/CTRL/BAT_UV_F`, BAT eFuse enable/fault-gated net). `TP5` logic rail
  was about 2.8 V during the related PV logic proof.
- **Goal / what it probes:** safely verify that manual BAT eFuse commands
  physically move the BAT eFuse logic node without powering the battery
  rail.
- **Command(s) sent:** `set_manual_bat on` -> `get_values fields=all` ->
  `set_manual_bat off` -> `get_values fields=all`.
- **Reply / observation:** `set_manual_bat on` succeeded and a separate
  poll reported `mode=manual`, `manual bat_req=1`. Operator measured
  `TP15 = 1.7 V`. After `set_manual_bat off`, the board replied
  `manual bat_req=0`; operator measured `TP15` back at `0 V`.
- **Verdict:** PASS for BAT eFuse logic control, with voltage-level caveat.
- **Defects produced or confirmed:** none.
- **Notes / caveats:** TP15 is not a raw MCU GPIO test pad; it is a
  shared/fault-gated node connected to the BAT eFuse enable path and
  comparator network. The 1.7 V asserted level is therefore not a clean
  3.3 V logic-high proof, but the observed `0 V -> 1.7 V -> 0 V`
  transition demonstrates firmware control of the physical BAT eFuse logic
  node. This does not prove IC7 passes battery power at `TP17`; that powered
  test remains separate.

### 2026-05-16 - Test F smoke transfer curve - isolated buck output tracks PWM duty

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4.
- **Hardware / setup:** assembled board treated as having R49 and R50 not
  populated, per operator report, so the buck stage was tested isolated from
  the PV and battery eFuse paths.
- **Wiring:**
  - Existing board logic supply remained connected through `J5 PDU_3V3` and
    `J5 GND`.
  - 12 V auxiliary bias supply positive to `J5 PDU_12V`, negative to
    `J5 GND`; current limit set to 100 mA. Operator measured `TP7 = 11.8 V`.
  - Buck input supply positive to `J6 BUCK_IN`, negative to `J6 GND`; set to
    5.0 V with 200 mA current limit. Operator measured `TP12 = 5.0 V`.
  - Load resistor: 100 ohm between `J7 BUCK_OUT` and `J7 GND`.
  - DMM red lead on `TP13` (`/BUCK/BUCK_OUT`), black lead on board GND.
- **Baseline before PWM:** firmware commanded to `off`, then `enter_manual`,
  then `set_manual_pwm duty=0`; separate poll reported `mode=manual`,
  `requested=0 applied=0 enabled=0`. Operator measured `TP12 = 5.0 V`,
  `TP13 = 0 V`, `TP7 = 11.8 V`.
- **Goal / what it probes:** verify that firmware-applied PWM physically
  drives the isolated buck converter and that `BUCK_OUT` follows commanded
  duty cycle.
- **Command(s) sent:** successive `set_manual_pwm duty=N` commands, each
  followed by a separate `get_values fields=all` poll to avoid the known F6
  stale-telemetry lag. Final command was `set_manual_pwm duty=0`.
- **Reply / observation:** for all nonzero duties, separate polls reported
  `mode=manual`, `requested=applied=N`, `enabled=1`. Final poll after
  `set_manual_pwm duty=0` reported `requested=0 applied=0 enabled=0`.

  | PWM duty | Duty fraction | Ideal TP13 @ 5.0 V in | Measured TP13 |
  |---:|---:|---:|---:|
  | 5000 | 7.63 % | 0.381 V | 0.250 V |
  | 10000 | 15.26 % | 0.763 V | 0.642 V |
  | 20000 | 30.52 % | 1.526 V | 1.410 V |
  | 25000 | 38.15 % | 1.907 V | 1.800 V |
  | 30000 | 45.78 % | 2.289 V | 2.195 V |
  | 40000 | 61.04 % | 3.052 V | 2.990 V |
  | 50000 | 76.30 % | 3.815 V | 3.692 V |

- **Verdict:** PASS for isolated buck output response / smoke transfer
  curve.
- **Defects produced or confirmed:** none.
- **Notes / caveats:** this is a low-voltage isolated smoke test, not a full
  12-18 V MPPT power test. The low-duty points are below the ideal line,
  plausibly because of light load, discontinuous conduction, driver timing,
  and DMM averaging. The monotonic response and near-linear mid/high-duty
  points are sufficient evidence that firmware PWM controls the physical
  buck output. `AUX_12V` on `TP7` is required for IC6/EPC2152 to switch;
  `BUCK_IN` alone is not enough.

### 2026-05-16 - Test B ADC calibration, partial - TP19 / OUTV1 direct injection

- **Firmware under test:** `src-pds` working tree, `BOARD=mainboard-pds`,
  status payload v4, real sensor source selected.
- **Hardware / setup:** board logic powered from `J5 PDU_3V3`/GND. Buck,
  PV, and BAT power paths left off for this ADC-only test. Bench supply
  current limit set to 10 mA. Supply positive connected to `TP19`
  (`/OUTV1`, ADC-side panel-voltage divider node), supply negative to
  board GND. DMM red lead on `TP19`, black lead on board GND. `TP5`
  (`/AUX/3V3`, practical ADC-reference rail check) measured `2.86 V`.
- **Goal / what it probes:** verify that injecting a known small voltage at
  the `OUTV1` ADC-side node changes the firmware's reported panel-voltage
  divider telemetry, and quantify the reference-voltage scaling error.
- **Command(s) sent:** `off`; `set_sensor_source real`; repeated
  `get_values fields=all` polls.
- **Reply / observation:** after the operator set the DMM-measured `TP19`
  voltage to `0.175 V`, five firmware polls reported
  `sensor_panel_v divider` values of `6648`, `6130`, `5899`, `5986`, and
  `5986 mV`. Other divider fields remained near zero.
- **Expected math:** firmware treats `TP19` as the `OUTV1` ADC pin voltage,
  converts the ADC count using a hardcoded `3300 mV` reference, then undoes
  the `100000/3600 ohm` divider ratio. With `TP5 = 2.86 V` as the practical
  reference check, `0.175 V * ((100000 + 3600) / 3600) * (3.300 / 2.860)`
  predicts about `5.81 V` reported panel-divider voltage. The stable
  observed values around `5.99 V` are coherent with that firmware scaling
  and measurement tolerance.
- **Verdict:** PASS so far for TP19 mapping to `sensor_panel_v divider`;
  PARTIAL for full ADC calibration because more TP19 points and TP20 remain.
- **Defects produced or confirmed:** ADC reference/scaling mismatch
  confirmed by measurement: firmware assumes `3300 mV`, while the board's
  analog/reference rail check at `TP5` was `2.86 V`, causing ADC-derived
  voltages to report high by roughly `3300/2860 = 1.15x`.
- **Notes / caveats:** this is direct ADC-side injection, not high-voltage
  rail injection. Keep direct `TP19` injection well below `3.3 V`; for this
  calibration sweep, stay below about `0.7 V` unless a wider range is
  explicitly needed. The exact ADC reference is VDDANA downstream of the
  analog supply filtering; `TP5` is the practical nearby rail measurement
  used during this bench run.

### 2026-05-16 - Post-reflash ADC calibration setup check

- **Firmware under test:** `src-pds` working tree after F7 ADC-reference fix
  (`ADC_REFERENCE_MILLIVOLTS = 2860u`), rebuilt as `BOARD=mainboard-pds`,
  flashed by operator, status payload v4.
- **Hardware / setup:** same bench session; ADC injection wiring intended
  for `TP19`/GND, but DMM voltage at `TP19` not yet reconfirmed after the
  reflash.
- **Goal / what it probes:** verify the reflashed board is alive, safe/off,
  and using real board sensor reads before repeating the `TP19 = 0.175 V`
  calibration point.
- **Command(s) sent:** `get_values fields=all`; `off`;
  `set_sensor_source real`; `get_values fields=all`.
- **Reply / observation:** COM3 link alive. Initial poll showed
  `sensor source=injected`. After `set_sensor_source real`, a separate
  poll showed `mode=off`, `sensor source=real`, `pwm requested=0 applied=0
  enabled=0`, and `sensor_panel_v divider=0`.
- **Verdict:** PASS for post-reflash liveness and safe real-sensor setup;
  calibration verification not started because the firmware did not yet see
  a nonzero `TP19` injection.
- **Defects produced or confirmed:** none.
- **Notes / caveats:** before verifying F7, reconfirm the actual DMM voltage
  from `TP19` to board GND. Expected fixed-firmware reading at
  `TP19 = 0.175 V` is about `5.0 V`.

### 2026-05-16 - Post-F7 ADC verification attempt - TP19 nonzero after reflash

- **Firmware under test:** `src-pds` working tree after F7 ADC-reference fix
  (`ADC_REFERENCE_MILLIVOLTS = 2860u`), rebuilt as `BOARD=mainboard-pds`,
  flashed by operator, status payload v4.
- **Hardware / setup:** ADC injection still intended on `TP19`/GND from the
  previous calibration setup. Operator asked to retry; DMM voltage at `TP19`
  was not reported in this step.
- **Goal / what it probes:** verify whether the corrected firmware reports
  a lower `OUTV1`/panel-divider equivalent voltage for the same TP19
  injection.
- **Command(s) sent:** `off`; `set_sensor_source real`; five separate
  `get_values fields=all` polls.
- **Reply / observation:** board replied normally, remained `mode=off`,
  `sensor source=real`, and `pwm requested=0 applied=0 enabled=0`. Firmware
  reported `sensor_panel_v divider` values of `4259`, `4029`, `3971`,
  `4058`, `3971`, and `3799 mV` across the `set_sensor_source` reply plus
  five polls.
- **Expected math:** after the F7 fix, if `TP19` were still exactly
  `0.175 V`, the expected panel-divider equivalent would be
  `0.175 V * ((100000 + 3600) / 3600) = 5.04 V`. The observed values around
  `3.8-4.3 V` imply the actual TP19 voltage during this retry was closer to
  `0.13-0.15 V`.
- **Verdict:** PARTIAL. The reflash took effect well enough that TP19 now
  reads in the corrected lower range, but F7 is not fully verified until the
  operator reports the simultaneous DMM voltage at TP19.
- **Defects produced or confirmed:** none new.
- **Notes / caveats:** repeat with DMM red on `TP19`, black on board GND,
  and record the actual TP19 voltage at the same time as the firmware poll.

### 2026-05-16 - Post-F7 ADC verification attempt - TP19 DMM 0.138 V but firmware zero

- **Firmware under test:** `src-pds` working tree after F7 ADC-reference fix
  (`ADC_REFERENCE_MILLIVOLTS = 2860u`), rebuilt as `BOARD=mainboard-pds`,
  flashed by operator, status payload v4.
- **Hardware / setup:** operator reported DMM reading `TP19 = 0.138 V`
  relative to board GND during the retry. Board commanded safe/off with real
  sensor source.
- **Goal / what it probes:** correlate simultaneous DMM voltage at `TP19`
  with the fixed firmware's `sensor_panel_v divider` reading.
- **Command(s) sent:** three `get_values fields=all` polls; then
  `set_sensor_source real` followed by two more `get_values fields=all`
  polls.
- **Reply / observation:** all five `get_values` replies reported
  `mode=off`, `sensor source=real`, and `sensor_panel_v divider=0`. The
  forced `set_sensor_source real` reply also reported
  `sensor_panel_v divider=0`.
- **Expected math:** if the DMM voltage is on the actual `OUTV1` ADC-side
  node, the corrected firmware should report
  `0.138 V * ((100000 + 3600) / 3600) = 3.97 V`.
- **Verdict:** INCONCLUSIVE for F7 verification. The earlier post-reflash
  values (`3.8-4.3 V`) match `TP19` around `0.138 V`, but the later
  simultaneous retry returned zero, which points to a physical contact /
  probe-point / ground-reference issue rather than a scaling-code issue.
- **Defects produced or confirmed:** none new.
- **Notes / caveats:** do not treat the zero readings as an ADC calibration
  failure until the operator reconfirms the injection lead is on the actual
  `TP19` pad, DMM red is on the same pad, and DMM/supply negative are tied
  to board GND.

### 2026-05-16 - Correction - Post-F7 TP19 verification passed at 0.138 V

- **Firmware under test:** `src-pds` working tree after F7 ADC-reference fix
  (`ADC_REFERENCE_MILLIVOLTS = 2860u`), rebuilt as `BOARD=mainboard-pds`,
  flashed by operator, status payload v4.
- **Correction to previous entry:** the later zero readings occurred after
  the operator had stopped injecting voltage on `TP19`; they are therefore
  the correct baseline and do not contradict the calibration result.
- **Valid measurement point:** operator reported the DMM was at
  `TP19 = 0.138 V` for the nonzero post-reflash readings.
- **Expected math:** `OUTV1` divider ratio is
  `(100000 + 3600) / 3600 = 28.7778`, so `0.138 V` at the ADC-side node
  corresponds to `0.138 * 28.7778 = 3.971 V` reported panel-divider
  equivalent.
- **Firmware observation:** post-F7 firmware reported
  `sensor_panel_v divider` values of `4259`, `4029`, `3971`, `4058`,
  `3971`, and `3799 mV` during the active injection window. Two samples
  landed exactly at `3971 mV`; the central four samples average
  `4007 mV`.
- **Error calculation:** using the central four samples, error is
  `(4007 - 3971) / 3971 = +0.9 %`. Using the exact matching samples,
  error is effectively `0 %` at the displayed millivolt precision.
- **Verdict:** PASS. The F7 firmware calibration fix worked for the TP19 /
  `OUTV1` voltage-divider path at the tested point.
- **Defects produced or confirmed:** F7 is verified fixed at this calibration
  point. Continue with more TP19 points and TP20 before considering the ADC
  calibration fully complete across both divider paths.

### 2026-05-16 - Setup addendum - Post-F7 TP19 ADC verification wiring

- **Purpose of addendum:** make the physical setup of the successful
  `TP19 = 0.138 V` verification explicit.
- **Board power:** board logic powered from the normal `J5 PDU_3V3` and
  `J5 GND` setup. No PV power-path or battery power-path test was being run.
  Buck output testing was not active.
- **Firmware state:** board commanded `off`, then `set_sensor_source real`.
  Polls confirmed `mode=off`, `sensor source=real`, and PWM
  `requested=0 applied=0 enabled=0`.
- **Injection wiring:** low-voltage bench source positive connected to
  `TP19` (`/OUTV1`, ADC-side panel-voltage sense node). Bench source
  negative connected to board GND. This was a direct ADC-side injection, not
  a high-voltage rail injection.
- **Measurement wiring:** DMM red probe on the same `TP19` pad, DMM black
  probe on board GND. Operator reported `TP19 = 0.138 V` during the active
  nonzero firmware readings.
- **What firmware reports:** `sensor_panel_v divider` is not the raw TP19
  pin voltage. Firmware scales the ADC pin voltage back through the OUTV1
  divider ratio, so `0.138 V` at TP19 should appear as about `3.971 V` in
  telemetry.
- **Safety limit:** direct `TP19` injection must remain a small ADC-side
  voltage; do not apply panel/buck rail voltage to TP19. For this sweep,
  keep TP19 below about `0.7 V` unless a wider calibrated sweep is planned.

### 2026-05-16 - Narrative setup clarification - Post-F7 TP19 ADC verification

For the successful post-F7 ADC verification, the board was not being tested
as a power converter and no PV, battery, or buck power-transfer path was
energized. The board was only powered enough for the MCU and sensor-reading
firmware to run, using the normal logic supply connection on `J5 PDU_3V3`
and `J5 GND`. The auxiliary logic/reference rail at `TP5` had been measured
in this bench session at about `2.8 V` (`2.86 V` recorded during the ADC
calibration work), which is why the firmware ADC reference constant was
changed from the ideal `3300 mV` assumption to `2860 mV` before the reflash.

After reflashing the firmware, the board was commanded into a safe readout
state with `off`, then `set_sensor_source real`. The following polls
confirmed that the firmware was in `mode=off`, that PWM was disabled
(`requested=0`, `applied=0`, `enabled=0`), and that the telemetry path was
using real board sensors rather than injected simulator values. This matters
because the test was meant to validate the real ADC hardware path and the
firmware scaling applied to that ADC reading.

The injected signal was a small bench voltage applied directly to `TP19`,
which is the `/OUTV1` ADC-side panel-voltage sense node. The bench supply
positive lead was connected to `TP19`, and the bench supply negative lead
was connected to board GND. The DMM was connected in parallel with that
injection point: red probe on the same `TP19` pad and black probe on board
GND. During the active, nonzero readings, the operator reported that the DMM
measured `0.138 V` from `TP19` to GND.

This was intentionally not a high-voltage panel-rail injection. `TP19` is
already on the MCU/ADC side of the OUTV1 resistor divider, so it must only
receive a small ADC-safe voltage. The firmware field being checked,
`sensor_panel_v divider`, does not print the raw `TP19` pin voltage. It
first reads the ADC pin voltage and then scales it back through the OUTV1
divider ratio. With R32 = `100 kOhm` and R33 = `3.6 kOhm`, the ratio is
`(100000 + 3600) / 3600 = 28.7778`. Therefore the DMM-measured
`0.138 V` at `TP19` should be reported by firmware as
`0.138 * 28.7778 = 3.971 V`.

The corrected firmware reported `sensor_panel_v divider` values including
`3971 mV` twice, with the central readings averaging about `4007 mV`. That
is a `+0.9 %` error relative to the expected `3971 mV`, and two samples
matched exactly at the displayed millivolt precision. The conclusion of this
specific verification point is therefore that the F7 ADC-reference firmware
fix worked for the `TP19` / `OUTV1` voltage-divider path.

---

## Open follow-ups (not yet tested — do NOT mark done until an entry exists)

- PV eFuse **power** proof: power PV input, `set_manual_pv on/off`, probe
  **TP10** vs **TP8**, scope **TP9**. (Continues Test B2.)
- BAT eFuse **power** proof: probe **TP17** (DMM-confirm TP14/TP17 pre/post first,
  see `What's TP for what test.md` §3).
- ADC calibration: inject known V, probe **TP19/TP20**.
- F6 fix + regression (deferred by decision; re-run Tests 1–5 after).
- F1 fix: correct INA226 I²C addresses + invalid-read flagging.
