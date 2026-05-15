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

---

## Open follow-ups (not yet tested — do NOT mark done until an entry exists)

- PV eFuse **power** proof: power PV input, `set_manual_pv on/off`, probe
  **TP10** vs **TP8**, scope **TP9**. (Continues Test B2.)
- BAT eFuse proof: probe **TP17** (DMM-confirm TP14/TP17 pre/post first,
  see `What's TP for what test.md` §3).
- Buck output under PWM: power BUCK_IN at J6, probe **TP13**.
- ADC calibration: inject known V, probe **TP19/TP20**.
- F6 fix + regression (deferred by decision; re-run Tests 1–5 after).
- F1 fix: correct INA226 I²C addresses + invalid-read flagging.
