# context.md — Read this first

If you are a fresh Claude Code instance and the user said *"read the
context.md"*, this file is your orientation. The repository does not make
sense if you just start reading files top-down — its history is in two
eras and most of the top-level material is about the **old** era. This
file tells you what was done, what is being done now, and what to trust.

---

## 1. The mission

This is firmware for the **EPS (Electrical Power System)** of the **CHESS
CubeSat** (EPFL). The job of the firmware: run the power-conditioning
unit (PCU) — track the solar panel's maximum power point (**MPPT**), drive
a synchronous **buck converter** to charge the battery, manage the PCU
state machine (charge / float / load-follow / discharge / safe mode),
gate the PV and battery **eFuses**, and report telemetry to the OBC.

Two physical targets, **same source, same Cortex-M0+ silicon**:
- **Curiosity Nano DM320119** dev board — `SAMD21G17D` (48-pin). Used for
  early bring-up only.
- **CHESS EPS PCU testing board V4.1** — `SAMD21J17D-MUT` (64-pin QFN,
  schematic ref `IC12`). This is the **real EPS hardware** and what we
  test on now. PCB design repo: `https://github.com/CHESS-mission/eps_pcu_eng`.

Bare-metal C, no HAL/RTOS/bootloader, direct register access, DFP
v3.6.144. The authoritative coding/house rules are in the repo-root
`CLAUDE.md` — follow it.

---

## 2. The two eras of this repository (this is the key to everything)

### Era 1 — the original firmware, built around `src/` ("source")

The first working implementation lives in **`src/`** (often called "the
source folder"). It was first brought up on the **dev board**, then
ported to the **real PCU mainboard** — Era-1 `src/` already has a full
mainboard build (`make BOARD=mainboard` →
`src/main_mainboard_chips_injection_demo.c` + the mainboard driver set,
`SAMD21J17D`). So `src/` is *not* a dev-board-only relic; it is the
proven firmware lineage `src-pds` re-implements, and it still runs on the
same real hardware. Its documentation is the **`docs/`** folder, and
**`notes/README.md` is the table of contents for `docs/`**. That firmware
works, but its file and function names grew organically and are hard to
navigate.

- `docs/` — all the Era-1 technical docs (build/flash, clocks, DMA UART,
  MPPT, mainboard pinout, etc.). Still useful as background, but written
  for `src/`, not for the rebuild.
- `notes/README.md` — TOC for `docs/`. Fine to use as an index.
- **`notes/plan.md` — DISREGARD.** It is a historical artifact, not a
  plan. Do not act on it.
- The repo-root `server.py` and other top-level scaffolding belong to
  Era 1 / abandoned MVP attempts. Not the reference for current work.

### Era 2 — the rebuild, `src-pds/` ("Source PDS")

**`src-pds/` is the only code that matters now.** It re-implements the
same proven Era-1 behavior with clearer names, clearer file structure,
and a single unified local web app — so the project is legible to someone
who is not already inside it. Everything current lives under `src-pds/`:
the application, the command/CHIPS layer, the runtime state, the state
machine, the MPPT loop, the board-output/safety layers, and the web app.

The rebuild also added the **local web app** (`src-pds/local_web_app/`):
one Python process owns the ESP32 USB link and serves per-feature pages
(MPPT convergence, state transitions, manual control, etc.).

When in doubt: **read `src-pds/`, treat `src/` + `docs/` as the reference
for *intended behavior*, ignore `notes/plan.md`.**

---

## 3. Where the project is right now: the testing phase

Era-2 firmware is built and running on the **real PCU board V4.1**, and
we are doing **adversarial bench/hardware testing** — driving the board
over the ESP32 bridge and checking firmware behavior against an
oscilloscope/multimeter, deliberately hunting for defects.

> **MANDATORY RULE — testing_history.md.** Every time you run a test
> against the firmware or hardware (any command sent to the board, any
> scope/meter measurement), you **must** append a structured entry to
> **`src-pds/testing_history.md`** before moving on — what was tested, on
> what firmware/version, what was sent, what came back, PASS/FAIL/PARTIAL,
> and which defect it produced. It is append-only (never rewrite past
> entries). A test not recorded there did not happen. The entry schema is
> at the top of that file.

Test/▾status artifacts (all in `src-pds/`, created during testing):
- **`defects_to_correct.md`** — living defect log (F1–F6 so far, with
  evidence). Read this to know what is known-broken and *not yet fixed*.
  Note especially **F1** (INA226 telemetry is garbage) and **F6**
  (telemetry lags one loop iteration — affects how you read back state).
- **`What's TP for what test.md`** — authoritative map of the 21 physical
  test points (TP1–TP21) → schematic net → which firmware action each
  one proves, with pad locations and the command-bridge how-to. This is
  how you physically verify firmware actuation on the real board.
- **`hardware_test_report_2026-05-14.md`** — earlier hardware results.
- `../research_logs/agent_{N,O,P,Q}_*.md` — per-TP proof with cited
  PCB/schematic line numbers (primary source behind the TP map). The
  `../research_logs/_pcb_files_*` and `_tmp_schematics` dirs are offline
  caches of the actual KiCad PCB project.

---

## 4. Build & flash reality (gotchas that already cost us hours)

- The Makefile has four `BOARD` targets. The two that matter:
  `BOARD=mainboard-pds` (real PCU board — **drives real PWM/eFuses**) and
  `BOARD=devboard-pds` (dev board — **PWM is intentionally stubbed out;
  no PWM will ever appear, by design**). Always `make clean` (or wipe
  `build/`) when switching `BOARD`.
- **The Makefile only emits `.bin`/`.elf`/`.map` — there is NO `.hex`
  rule.** GUI flashers point at `build/satellite_firmware.hex`, which
  goes stale. Regenerate it explicitly:
  `arm-none-eabi-objcopy -O ihex build/satellite_firmware.elf build/satellite_firmware.hex`.
- **The board must be RESET after flashing** (the reset pin must be
  physically connected) or it keeps running the old firmware. This
  symptom (board "dead"/unstable) wasted a long debug session.
- On Windows the Makefile recipes use `cmd` syntax; `make clean`'s
  `rm -rf` is not on PATH — wipe `build/` manually if needed.

## 5. How to drive the board for tests

One persistent background bridge owns COM3:
`tail -f cmds.txt | "/c/Program Files/PuTTY/plink.exe" -serial COM3 -sercfg 115200,8,n,1,N > com3.log`
— then `echo "<command>" >> cmds.txt` and read replies from `com3.log`.
COM3 must be free first (close the Arduino IDE Serial Monitor / web app).
Command vocabulary and reply format: ESP32 bridge in
`esp32_test_harness/03_source_pds_bridge/`. Key commands: `enter_manual`,
`set_manual_pwm duty=N`, `set_manual_pv on|off`, `set_manual_bat on|off`,
`set_manual_led on|off`, `run_pwm duty=N`, `get_values fields=all`,
`stream_values on period=N`, `off`. **F6 caveat:** to read true state,
send the `set_*` command and the `get_values` poll as *separate*
commands (a second apart), never batched.

---

## 6. The `src-pds/` documentation map

| File | What it is |
|---|---|
| `context.md` | **this file** — start here |
| `plan.md` | the rebuild's design record (see verdict below) |
| `conventions.md` | coding standards for the rebuild |
| `command_architecture.md` | CHIPS command/contract design |
| `state-transitions.md` | flight-grade PCU state-machine target |
| `manual_control_mode.md` | the manual-control feature design |
| `sensor_documentation.md` | sensor abstraction layers |
| `how_to_test.md` | bench-test procedures — **its TP assignments are wrong**; use `What's TP for what test.md` §5 corrections |
| `what_the_simulator_does_and_what_real_flight_software_still_needs.md` | sim vs real-flight gap |
| `implementation_checklist.md` | rebuild progress checklist |
| `defects_to_correct.md` | live defect log (testing phase) |
| `testing_history.md` | **append-only record of every test run — MUST be updated on every test** |
| `What's TP for what test.md` | physical test-point → firmware-test map |
| `local_web_app/README.md` | web app routes/architecture |

---

## 7. Verdict on `src-pds/plan.md` (the user asked)

**Keep it, but treat it as the rebuild's design rationale, not a live
task list.** It accurately records *why* `src-pds` is structured the way
it is (the per-step rebuild rule, the reference-file mapping back to
`src/`, the big implementation steps, the web-app page intent) — that is
genuinely useful for a new instance understanding intent. **However its
"Current Blocking Items" / page-status sections are PARTIALLY stale** — we
are past the build phase and into hardware testing, and the web app's
architecture is unified with the MPPT / state / manual pages working; BUT
its claim that the **comm/status page is not implemented is still true**,
so do not assume every blocking item there is obsolete — verify each
before trusting it. So: read `plan.md` for architecture/intent; treat its
status/blocking claims as unverified-until-checked; for the real current
state use this `context.md`, `defects_to_correct.md`, and `What's TP for
what test.md`. Unlike
`notes/plan.md` (pure historical noise — disregard entirely),
`src-pds/plan.md` still has design value.

---

## 8. Quick start for a fresh instance

1. Read this file, then `src-pds/defects_to_correct.md` and
   `src-pds/What's TP for what test.md`.
2. Skim repo-root `CLAUDE.md` (house rules — mandatory).
3. Treat `src-pds/` as the live code; `src/` + `docs/` as behavior
   reference; `notes/plan.md` as disregarded.
4. To test hardware: build `BOARD=mainboard-pds`, regenerate the `.hex`,
   flash, **confirm the board was reset**, bring up the COM3 plink
   bridge, drive commands, verify on the TPs per `What's TP for what
   test.md`, log any new defect in `defects_to_correct.md`, **and append
   every test you run to `testing_history.md` (mandatory, append-only).**
