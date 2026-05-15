# What's TP For What Test — CHESS EPS PCU Testing Board V4.1

Authoritative map of every physical test point (TPx) on the board → its
schematic net → which firmware-driven behavior it lets a bench operator
prove with a multimeter / oscilloscope, plus how to physically find each
pad and how to actually re-run each test.

**This document is meant to be self-sufficient for a future Claude Code
instance: it should be able to re-run every firmware/hardware test using
only this file plus the defect log.**

---

## 0. Provenance & how to re-verify any claim here

Produced by four independent research sub-agents that traced the *actual
PCB design files* (not prose), each line cited in its log:

| Range | Log (primary source of truth) |
|---|---|
| TP1–TP5 | `research_logs/agent_N_testpoint_map_tp1_tp5.md` |
| TP6–TP10 | `research_logs/agent_O_testpoint_map_tp6_tp10.md` |
| TP11–TP14 | `research_logs/agent_P_testpoint_map_tp11_tp14.md` |
| TP15–TP21 + roster | `research_logs/agent_Q_testpoint_map_tp15_tp17_and_full_roster.md` |

Design files (offline cache): `research_logs/_pcb_files_agent_I/`
(`testingPCU.kicad_pcb` = as-fabricated copper truth, `BUCK.kicad_sch`,
`MCU.kicad_sch`, `testingPCU.csv` = BOM), `research_logs/_pcb_files_agent_J/`
(`PV.kicad_sch`, `BAT.kicad_sch`, `CTRL.kicad_sch`),
`research_logs/_tmp_schematics/` (`AUX_SUPPLY.kicad_sch`). Online mirror:
`https://github.com/CHESS-mission/eps_pcu_eng` (branch `main`). The
`.kicad_pcb` pad→net binding is ground truth for the physical board;
verify a TP by grepping `(property "Reference" "TPnn"` then the following
`(net <id> "<name>")` in that footprint block.

**Bench condition assumed for the "Bench V now" column:** only 3.3 V
applied to **J5**, buck input cold, **no PV source, no battery**. This is
the current setup.

Date: 2026-05-16.

---

## 1. Read this first — board-wide facts that prevent false conclusions

1. **There are 21 test points, TP1–TP21, contiguous, no gaps.** Not 17.
   `how_to_test.md` and `docs/mainboard_pinout_pcu_v4_1.md` predate this
   and are wrong/incomplete. (Proof: 21 TP footprints in
   `testingPCU.kicad_pcb`; BOM `testingPCU.csv` line 52.)
2. **All 21 TPs are bare SMD copper pads on the FRONT (F.Cu) side**
   (`TestPoint_Pad_D1.5mm`). The BOM marks them "Excluded from BOM" —
   this means *there is no part to solder*, **not** that they are
   missing. All 21 pads physically exist; probe the bare pad directly.
3. **TP1–TP4 reading ≈ 0 V on the current bench is EXPECTED, NOT a
   fault.** The MCU runs off external 3.3 V: J5 → `PDU_3V3` → OR-diode D2
   → **TP5 (`/AUX/3V3`)** → MCU. The on-board 12/5/3.3 V LDOs (TP2/TP3/TP4)
   are fed from `PV_RAW`/`AUX_IN`, which is cold, so they sit at 0 V while
   the board runs perfectly. Keep a meter on **TP5 (≈3.0–3.3 V)** as the
   "MCU is alive" reference during every test.
4. **The dangerous buck switching node (`SW`, high dv/dt) has NO test
   point.** TP13 (`BUCK_OUT`) is *after* inductor L2 — a safe DC probe.
   No TP exposes raw `SW`. Good: you cannot accidentally hang a scope on
   the hazardous node via a TP.
5. **Two net-name-vs-function traps** (a future instance grepping by
   function will miss these):
   - **TP9** copper net is literally `/CTRL/BAT_OC_F`, but functionally
     it is the *PV-eFuse enable / fault gate* — a wired-OR of the PV
     eFuse EN/UVLO line with all four `BAT_OV/BAT_OC/PV_OV/PV_OC`
     comparator outputs (KiCad just named the merged net after one
     member). It is the scope point for the **PA16 PV-EN logic**, not a
     battery-OC-only signal.
   - **TP15** copper net is `/CTRL/BAT_UV_F`, which *is* electrically the
     BAT eFuse `BAT_EN` pin (the UV comparator gates the eFuse enable).
     It is the **PA17 BAT-EN logic** node, not a standalone UV flag.

---

## 2. Authoritative roster — TP1 … TP21

Position = `(x, y)` of the pad on the **front (F.Cu)** side, in the
`.kicad_pcb` coordinate frame (use it with the board's silkscreen `TPn`
labels to locate the pad).

| TP | Net (as-fabricated PCB) | Sheet | Pos (x,y) F.Cu | Subsystem / pre·post | Bench V now | Max V if powered later | Firmware action it verifies | Probe safety |
|----|--------------------------|-------|----------------|----------------------|-------------|------------------------|------------------------------|--------------|
| TP1 | `Net-(D1-K)` | /AUX/ | 59.2, 62 | AUX raw input, **after** D1 Schottky reverse-protect; upstream of all AUX regs | ≈0 V (PV_RAW cold) | ~20 V practical (D1 100 V/1 A) | none (input rail) | safe DC |
| TP2 | `/AUX/LDO_12V` | /AUX/ | 79, 69.25 | Raw on-board 12 V reg out, **before** OR-diode | ≈0 V (LDO unpowered) | ~12–13 V | none (always-on rail) | safe DC |
| TP3 | `/AUX/LDO_5V` | /AUX/ | 93.75, 69.25 | Raw on-board 5 V reg out, **before** OR-diode | ≈0 V | ~5.5 V | none | safe DC |
| TP4 | `/AUX/LDO_3V3` | /AUX/ | 109.5, 62 | Raw on-board 3.3 V reg out, **before** OR-diode D2 | ≈0 V (MCU runs off TP5 path) | ~3.6 V | none | safe DC |
| **TP5** | `/AUX/3V3` | /AUX/ | 98.5, 77.75 | OR-ed 3.3 V rail = max(PDU_3V3, LDO_3V3) − Schottky; → MCU VDDIN | **≈3.0–3.3 V** | ~3.6 V | **"MCU is powered/alive" reference** | safe DC |
| TP6 | `/AUX_5V` | /AUX/ | 83.5, 77.75 | Aux 5 V rail (OR-ed) | ≈0 V | ≤5 V | none (supply presence) | safe DC |
| TP7 | `/AUX/AUX_12V` | /AUX/ | 68.25, 77.75 | Aux 12 V rail (OR-ed) | ≈0 V | ≤12 V | none (supply presence) | safe DC |
| **TP8** | `/PV_RAW` | /PV/ | 68.1, 91.7 | PV input **BEFORE** PV eFuse IC4 (= eFuse `PV_IN`) | 0 V (no PV) | **≤17.67 V** (PV OV trip; UV 9.52 V) | reference (pre-eFuse) for the PA16 test | care ≤17.7 V |
| **TP9** | `/CTRL/BAT_OC_F` *(functionally PV-EN/fault gate — see §1.5)* | /PV/ | 56.5, 86.25 | Wired-OR: PV eFuse EN/UVLO + BAT_OV/BAT_OC/PV_OV/PV_OC comparators | ~3.3 V idle (open-collector+pullup) | logic | **PA16 PV-EN logic reached IC4** (scope it while toggling); fault-injection path | safe (logic) |
| **TP10** | `/+PV` (= PV_OUT1) | /PV/ | 68, 98.6 | PV rail **AFTER** PV eFuse IC4 (post-eFuse) | 0 V (no PV) | ≤17.67 V | **PRIMARY `set_manual_pv`/PA16 power proof** — rises to PV when enabled | care ≤17.7 V |
| TP11 | `/-PV` ⚠️ **PCB-vs-schematic conflict** | /PV/ | 64.25, 129.25 | PCB: PV negative / LT6108(IC10) sense-return @ J6. Schematic: `PV_OUT2` post-eFuse | ≈0 V | small (return) or ≤17.7 V if it's really PV_OUT2 | **AMBIGUOUS — DMM-verify (§3)**. If `/-PV`: none. If `PV_OUT2`: PA16 power | care until resolved |
| TP12 | `/BUCK/BUCK_IN` | /BUCK/ | 81.25, 124.5 | Buck **input**, before switches/L2; fed from PV connector **J6** | ≈0 V | buck Vin (≤ board limit) | confirms buck input rail present (pre-PWM test) | safe DC (input cap) |
| **TP13** | `/BUCK/BUCK_OUT` | /BUCK/ | 106.05, 124.5 | Buck **output**, **AFTER inductor L2** (NOT the SW node) | ≈0 V | ≤14 V (C17 derate) | **PRIMARY PWM proof** — power BUCK_IN, drive PA12/PA13, V(TP13) tracks duty | safe DC |
| TP14 | `/+BAT` (net 17) ⚠️ **P/Q conflict** | /BAT/ | 118, 98.5 | **Best guess (Agent Q, IC7 pin-function): BAT eFuse `IN` = raw battery, PRE-eFuse.** Agent P: post-eFuse. | ≈0 V (no battery) | ≤ pack V (~4.2 V) | If pre-eFuse: none (raw input ref). If post: PA17. **DMM-verify (§3)** | care if battery live |
| **TP15** | `/CTRL/BAT_UV_F` (= BAT `BAT_EN`) | /BAT/ | 127.2, 105 | BAT eFuse IC7 **EN** pin, gated by UV comparator (~6.2 V) | 0/3.3 V logic | logic | **PA17 BAT-EN logic reached IC7** | safe (logic) |
| TP16 | `/-BAT` (net 65) | /BAT/ | 127.25, 108 | Battery-negative **sense/return** (R29/R31/R34/R36); **NOT board GND** | ≈0 V | small offset | none (sense/return reference) | do NOT use as ground clip |
| **TP17** | `/VBAT` (net 19) | /BAT/ | 118, 91.7 | **BAT eFuse IC7 OUTPUT (POST-eFuse)** — proven by IC7 pad `OUT_1..5` = net 19 | ≈0 V (eFuse off) | ≤ pack V (~4.2 V) | **PRIMARY `set_manual_bat`/PA17 power proof** — rises to pack V when enabled | care if battery live |
| TP18 | `Net-(IC9-1IN+)` | /CTRL/ | 79, 118.5 | CTRL comparator IC9 input+ (fault/telemetry sense) | logic/analog | ≤3.3 V | CTRL sense (characterize) | safe (logic) |
| **TP19** | `/OUTV1` | /CTRL/ | 81.75, 107.25 | CTRL voltage-sense net OUTV1 (ADC chain) | analog | ≤3.3 V | **Test B ADC calibration (OUTV1)** | safe ≤3.3 V |
| **TP20** | `/OUTV2` | /CTRL/ | 108, 106.75 | CTRL voltage-sense net OUTV2 (ADC chain) | analog | ≤3.3 V | **Test B ADC calibration (OUTV2)** | safe ≤3.3 V |
| TP21 | `/CTRL/IN3_MINUS` | /CTRL/ | 107.75, 118.5 | CTRL comparator reference/sense | analog | ≤3.3 V | CTRL sense (characterize) | safe (logic) |

---

## 3. ⚠️ Conflicts — MANDATORY DMM continuity check before trusting TP11 / TP14 / TP17

Two TPs have a genuine **as-fabricated-PCB vs schematic** net-name
disagreement (same component UUID, different net). The `.kicad_pcb` is
normally authoritative for a physical board, so the best-guess physical
truth is listed — but a wrong pre/post-eFuse call would invalidate the
battery/PV eFuse results, so **ring them out before using them as proof.**

**TP11 — `/-PV` (PCB, best guess) vs `PV_OUT2` (schematic):**
- DMM continuity, board unpowered:
  - TP11 ↔ J6 "−PV" terminal **or** R17 / IC10(LT6108) pins → it is
    **`/-PV`** = PV input negative / sense-return → **no firmware action;
    use TP10 for the PA16 PV-eFuse proof.**
  - TP11 ↔ PV eFuse **IC4 OUT** side → it is **`PV_OUT2`** (post-eFuse) →
    then it *would* track PA16.
- Best guess: `/-PV` (Agent P Source 6/8).

**TP14 vs TP17 — which is post-BAT-eFuse:**
- Agent Q (decisive, IC7's own pad `pinfunction`): IC7 `IN_1..IN_5` =
  net 17 `/+BAT` (raw battery, **pre-eFuse**); IC7 `OUT_1..OUT_5` =
  net 19 `/VBAT` (**post-eFuse**). Agent P reached the opposite via
  weaker root-sheet alignment and downgraded it to MEDIUM.
- DMM continuity, board unpowered: ring **TP14** and **TP17** against
  IC7 (TPS25940, BAT eFuse) IN pins (9–13) vs OUT pins (4–8):
  - The TP continuous with IC7 **OUT** = post-eFuse → **use it for the
    PA17 `set_manual_bat` proof.** (Expected: **TP17**.)
  - The TP continuous with IC7 **IN** = pre-eFuse raw battery (reference
    only). (Expected: **TP14**.)
- **Caveat:** connector J7 ties `/+BAT`, `GND`, `/BUCK/BUCK_OUT` on one
  screw terminal. If a battery is wired to J7 it back-feeds `/+BAT`
  regardless of eFuse state. For a clean PA17 test, inject the battery on
  the eFuse-input side and observe the post-eFuse TP.

---

## 4. Which TP for which firmware test (with how to actually run it)

**Command bridge (how a future instance drives the board):** one
persistent background process owns COM3 —
`tail -f cmds.txt | "/c/Program Files/PuTTY/plink.exe" -serial COM3 -sercfg 115200,8,n,1,N > com3.log`
— then each command is `echo "<cmd>" >> cmds.txt` and the reply is read
from `com3.log`. COM3 must be free first (close Arduino IDE Serial
Monitor / any web app). The board must be **reset after flashing** (the
reset pin must be connected) or new firmware does not run.

**F6 telemetry caveat (see `src-pds/defects_to_correct.md`):** the reply
built immediately after a `set_*` command, *and* any `get_values` batched
in the same UART drain, report the **pre-apply** state. To read true
state, send the `set_*` command, then send `get_values` as a **separate**
command (a second later) so it lands in a later loop iteration.

| Firmware test | Command sequence | Probe | Expected (before → after) |
|---|---|---|---|
| **Buck PWM waveform** | `enter_manual` → `set_manual_pwm duty=N` | scope **PA12/PA13** directly (header J10, or EPC2152 IC6 pins) | complementary ~300 kHz, duty = N·159/65535 /160, ~125 ns dead-time both edges, never both-high |
| **Buck PWM → output** | power BUCK_IN at **J6**, then `enter_manual` → `set_manual_pwm duty=N` | **TP13** (`BUCK_OUT`), ref **TP12** (`BUCK_IN`) | TP13: 0 V → ≈ duty·V(TP12) |
| **PV eFuse enable (PA16)** | `enter_manual` → `set_manual_pv on` / `off` | **TP10** (`/+PV`, post) vs **TP8** (`/PV_RAW`, pre); scope **TP9** for EN logic | TP10: 0 V → ≈ V(TP8) when on; TP9 logic transitions. PV ≤ 17.67 V (OV trip), ≥ 9.52 V (UV) |
| **BAT eFuse enable (PA17)** | `enter_manual` → `set_manual_bat on` / `off` | **TP17** (`/VBAT`, post — DMM-confirm §3); scope **TP15** for EN logic | TP17: 0 V → ≈ pack V when on; TP15 logic transitions. Feed battery on eFuse-IN side, not J7 |
| **Status LED (PB22)** | `enter_manual` → `set_manual_led on` / `off` | the green **LED2** (visual) | LED on/off (verified PASS, Test B1) |
| **ADC voltage-sense calibration** | `set_sensor_source real`; inject known V | **TP19** (`/OUTV1`), **TP20** (`/OUTV2`) | reported telemetry mV vs applied V |
| **MCU alive (every test)** | — | **TP5** (`/AUX/3V3`) | ≈3.0–3.3 V whenever board powered |

Ground clip: a board **GND** screw (J6/J7 GND). **Never** use TP11
(`/-PV`) or TP16 (`/-BAT`) as ground — they are sense-return nodes, not
GND.

---

## 5. Corrections to `src-pds/how_to_test.md` (it has wrong TP assignments)

| `how_to_test.md` claims | Reality (proven) | Fix |
|---|---|---|
| only TP1–TP17 exist | **21 TPs (TP1–TP21)**; TP18–TP21 are CTRL sense nets | add TP18–21; TP19/TP20 are the ADC-cal probes |
| "TP11 = PV post-eFuse" | TP11 = `/-PV` (PV sense-return) per as-fab PCB | use **TP10** for the PA16 proof; DMM-verify TP11 (§3) |
| "TP13 = V_battery" | TP13 = `/BUCK/BUCK_OUT` (buck output, post-L2) | TP13 is the **buck-output / PWM** probe, not battery |
| "TP16/TP17 = BAT post-eFuse" | only **TP17 = `/VBAT`** is post-eFuse; TP16 = `/-BAT` sense-return; TP15 = EN logic | use **TP17** for PA17 proof; TP16 is a return, not a rail |
| TP14 = pre/post unclear | best guess **TP14 = pre-eFuse `/+BAT`** (raw battery in) | DMM-verify before trusting (§3) |

---

## 6. Unresolved — must be settled on the bench

1. **TP11 net identity** (`/-PV` vs `PV_OUT2`) — DMM continuity, §3.
2. **TP14 vs TP17 pre/post BAT-eFuse** — DMM continuity, §3. Best guess:
   TP17 = post (use for PA17), TP14 = pre.
3. **TP16 `/-BAT` exact potential vs board GND** under load — it is a
   sense-return through R29/R31/R34/R36, not net-1 GND; measure its
   offset before relying on it as a reference.
4. **TP15 / TP9 firmware polarity** — `BAT_UV_F`/`BAT_OC_F` are
   active-low fault per the CTRL note, but the actual `src-pds` PA16/PA17
   drive polarity was not cross-checked. Confirm direction on first
   toggle (watch the node while sending `set_manual_pv/bat on`→`off`).

---

## 7. Cross-references

- Firmware defects found in parallel: `src-pds/defects_to_correct.md`
  (esp. **F1** INA226 garbage, **F6** telemetry-lag — affects how you
  read back any TP-correlated state).
- Bench test procedures (note its TP assignments are corrected by §5):
  `src-pds/how_to_test.md`.
- Per-TP proof with cited PCB/schematic line numbers: the four
  `research_logs/agent_{N,O,P,Q}_*.md` logs.
