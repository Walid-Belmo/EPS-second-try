# Research Log — Agent O: PCB test points TP6–TP10 → net map

Purpose: map physical test points TP6..TP10 on the CHESS EPS PCU testing
board V4.1 to their schematic nets, so a bench operator can probe them to
verify firmware-driven actuation (PWM, eFuse enables, rail voltages).

Ground rules:
- Prefer the actual PCB project files (.kicad_pcb, .kicad_sch, BOM .csv)
  over any prose doc or prior research log.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-16.

---

## Source 1: testingPCU.kicad_pcb — TP6 footprint

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb` (lines 23847–23949)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP6 is a `TestPoint:TestPoint_Pad_D1.5mm` footprint at `(at 83.5 77.75)` on
  layer `F.Cu`, sheet `/AUX/` (`AUX.kicad_sch`). Its single pad 1 carries
  `(net 107 "/AUX_5V")`. Proof string: `(property "Reference" "TP6" ...` at
  line 23853 and `(net 107 "/AUX_5V")` at line 23943.
- **Confidence: HIGH**
  Authoritative routed PCB project file; explicit net assignment on the pad.
- **Implication for our build / bench test:**
  TP6 = the AUX_5V rail (auxiliary 5 V supply, AUX sheet). This is a power
  rail, NOT a firmware-controlled GPIO. Probing it verifies the on-board
  5 V aux supply is alive (expected ~5.0 V if the AUX supply is powered).
  Under current bench condition (only 3.3 V on J5, no PV/BAT) AUX_5V may be
  unpowered/0 V unless the aux regulator is fed from the 3.3 V rail — needs
  AUX.kicad_sch trace to confirm source. No direct firmware action verified;
  it is a supply-presence check. Safe to probe with bench ground (≤5 V).
- **Why I'm recording it:** Establishes TP6 net identity for the TP map.

## Source 2: testingPCU.kicad_pcb — TP7 footprint

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb` (lines 28618–28720)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP7 is a `TestPoint_Pad_D1.5mm` footprint at `(at 68.25 77.75)` on `F.Cu`,
  sheet `/AUX/` (`AUX.kicad_sch`). Pad 1 carries `(net 13 "/AUX/AUX_12V")`.
  Proof: `(property "Reference" "TP7"` at line 28624; `(net 13 "/AUX/AUX_12V")`
  at line 28714.
- **Confidence: HIGH**
  Authoritative routed PCB file with explicit net on the pad.
- **Implication for our build / bench test:**
  TP7 = AUX_12V rail (auxiliary 12 V supply on the AUX sheet). Power rail,
  not a firmware GPIO. Probing verifies the 12 V aux supply is present
  (~12 V expected if powered). Under current bench condition (3.3 V only on
  J5) AUX_12V is likely 0 V / unpowered unless a boost converter fed from
  3.3 V/5 V generates it (need AUX.kicad_sch to confirm). No direct firmware
  action; supply-presence check. CAUTION: up to 12 V — within safe bench
  probe range but verify multimeter range; safe vs common ground.
- **Why I'm recording it:** Establishes TP7 net identity for the TP map.

## Source 3: testingPCU.kicad_pcb — TP8 footprint

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb` (lines 14516–14618)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP8 is a `TestPoint_Pad_D1.5mm` at `(at 68.1 91.7)` on `F.Cu`, sheet
  `/PV/` (`PV.kicad_sch`). Pad 1 carries `(net 24 "/PV_RAW")`. Proof:
  `(property "Reference" "TP8"` line 14522; `(net 24 "/PV_RAW")` line 14612;
  `(sheetname "/PV/")` line 14572.
- **Confidence: HIGH**
  Authoritative routed PCB file, explicit pad net.
- **Implication for our build / bench test:**
  TP8 = /PV_RAW net = the raw PV input rail. See Source 4 for proof that
  PV_RAW drives the PV sheet's `PV_IN` pin (the eFuse IC4 INPUT), i.e. TP8
  is electrically BEFORE the PV eFuse. Probing TP8 measures the PV source
  voltage applied externally (J connector) prior to the eFuse. Under
  current bench condition (no PV power) expect 0 V. If PV later powered,
  per PV.kicad_sch note `PV_IN_MAX : 17.67 V (OV)` — could reach ~17 V; use
  appropriate DMM range and care. Firmware verification: not a GPIO, but
  with PV powered the operator can compare TP8 (pre-eFuse) vs TP10
  (post-eFuse) to prove firmware PV_EN (PA16) gated the eFuse pass element.
- **Why I'm recording it:** Establishes TP8 net identity + pre-eFuse position.

## Source 4: testingPCU.kicad_sch + PV.kicad_sch — PV eFuse topology

- **URL / path:** `...\_pcb_files_agent_I\testingPCU.kicad_sch` (PV sheet symbol lines 4832–4973; labels PV_RAW @2955 (100.33 196.85), +PV @2625 (151.13 196.85)) and `...\_pcb_files_agent_J\PV.kicad_sch` (TPS25940ARVCR eFuse symbol @ line 690/3342; PV_IN_MAX note line 1358; hierarchical_label PV_IN @2906, PV_OUT1 @2972)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  The PV hierarchical sheet instantiates a `TPS25940ARVCR` eFuse (`18V, 5A,
  42m eFuse...`, IC ref on PV sheet). Sheet pins: `PV_IN` (input) at
  (109.22 196.85), `PV_OUT1` (output) at (139.7 196.85), plus `PV_EN`,
  `PV_IMON`, `~{PV_FLT}`, `PV_PGOOD`. On the parent sheet the top-level net
  `PV_RAW` label sits at (100.33 196.85) — same wire row as PV_IN — and
  `+PV` label at (151.13 196.85) — same wire row as PV_OUT1. PV note:
  `PV_IN_MAX : 17.67 V (OV)\nPV_IN_MIN :  9.52 V (UV)`.
- **Confidence: HIGH**
  Two authoritative project files cross-checked; pin/label Y-coordinates
  match exactly, proving the wire connections.
- **Implication for our build / bench test:**
  Proves PV_RAW = eFuse INPUT side (pre-eFuse) → TP8 is BEFORE PV eFuse.
  Proves +PV = PV_OUT1 = eFuse OUTPUT side (post-eFuse) → TP10 is AFTER the
  PV eFuse. Firmware PV_EN (PA16) drives the eFuse enable; with PV powered,
  TP8 stays at the source voltage while TP10 only rises to ~PV when firmware
  asserts PV_EN. Probing TP8 vs TP10 is the definitive bench test that
  firmware PA16 physically gated the PV eFuse pass-FET.
- **Why I'm recording it:** Anchors TP8 (pre) and TP10 (post) eFuse positions.

## Source 5: testingPCU.kicad_pcb — TP9 footprint

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb` (lines 28045–28146)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP9 is a `TestPoint_Pad_D1.5mm` at `(at 56.5 86.25)` on `F.Cu`. Footprint
  sheet attr `(sheetname "/PV/")`. Pad 1 carries `(net 40 "/CTRL/BAT_OC_F")`.
  Proof: `(property "Reference" "TP9"` line 28051; `(net 40 "/CTRL/BAT_OC_F")`
  line 28141.
- **Confidence: HIGH**
  Authoritative routed PCB file with explicit pad net. (Net is owned by the
  CTRL sheet; TP footprint is physically placed in the PV silkscreen region
  but electrically belongs to the CTRL net — see Source 6 to confirm meaning.)
- **Implication for our build / bench test:**
  TP9 = /CTRL/BAT_OC_F = Battery Over-Current Fault signal (CTRL sheet). A
  digital/logic fault flag, not a power rail. Expected to be a logic-level
  line (likely open-drain, pulled to 3V3 = no-fault, low = fault). Under
  current bench condition (3V3 only) expect it to sit at its idle level
  (~3.3 V if pulled up, no battery OC possible). Firmware verification:
  lets the operator observe the battery eFuse/OC fault status line that
  firmware reads (and/or that asserts a fault to the MCU). Safe to probe
  with bench ground (logic-level). NEEDS Source 6 to confirm exact CTRL net
  meaning and polarity before relying on it.
- **Why I'm recording it:** Establishes TP9 net identity; flags need to
  resolve CTRL/BAT_OC_F semantics.

## Source 6: testingPCU.kicad_pcb — TP10 footprint

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb` (lines 28412–28514)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP10 is a `TestPoint_Pad_D1.5mm` at `(at 68 98.6)` on `F.Cu`, sheet
  `/PV/`. Pad 1 carries `(net 10 "/+PV")`. Proof: `(property "Reference"
  "TP10"` line 28418; `(net 10 "/+PV")` line 28508.
- **Confidence: HIGH**
  Authoritative routed PCB file; corroborated by Source 4 showing +PV =
  PV_OUT1 = eFuse output.
- **Implication for our build / bench test:**
  TP10 = /+PV = the PV rail AFTER the PV eFuse (post-eFuse). Confirms the
  task's external note that "TP10 is a PV post-eFuse point" — VERIFIED, not
  assumed. Under current bench condition (no PV) expect 0 V. If PV powered,
  voltage near PV source (~9.5–17.7 V window per PV note) only WHEN firmware
  asserts PV_EN (PA16) and the eFuse passes. This is the primary TP to
  verify firmware-driven PV eFuse enable: assert PA16 → TP10 rises;
  deassert → TP10 falls to 0. Probe with care if PV later powered (up to
  ~17.7 V); safe vs common bench ground.
- **Why I'm recording it:** Confirms TP10 = post-eFuse PV; key firmware test.

## Source 7: CTRL.kicad_sch wire-chain trace + agent_K log cross-check (TP9 reconciliation)

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_J\CTRL.kicad_sch` (junctions @1379/1421/1493/1499 on x=241.3; wires lines 1596, 1666, 1716, 2216, 2286, 2296; labels BAT_OC_F @2814 (228.6 73.66), PV_OC_F @2824 (228.6 104.14)); cross-checked vs `...\research_logs\agent_K_efuse_switch_path.md` lines 153, 247, 252, 257
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  Independent trace: a continuous vertical wire at x=241.3 runs y=43.18 →
  58.42 → 73.66 → 88.9 with junctions at each node. Horizontal stubs join
  it to labels `BAT_OV_F` (228.6,58.42), `BAT_OC_F` (228.6,73.66),
  `PV_OV_F` (228.6,88.9), `PV_OC_F` (228.6,104.14), plus stub
  `(xy 212.09 43.18) (xy 241.3 43.18)` tying in `CTRL_EN_IN1`. So all four
  LM139 open-collector fault-comparator outputs AND the PV eFuse enable
  path are ONE electrical net. KiCad picked the name `/CTRL/BAT_OC_F` for
  this merged net — which is exactly the net TP9's pad carries (Source 5).
  Agent K independently called TP9 the "PV-side eFuse enable signal".
- **Confidence: HIGH**
  My own wire/junction trace in the authoritative CTRL schematic confirms
  the net merge; Agent K's prose log agrees on the electrical meaning. The
  raw PCB net string `/CTRL/BAT_OC_F` and Agent K's "PV_EN" describe the
  SAME physical node (KiCad just labels merged nets by one member).
- **Implication for our build / bench test:**
  TP9 (net `/CTRL/BAT_OC_F`) = the wired-OR node of the PV eFuse
  enable/UVLO line + the BAT_OV/BAT_OC/PV_OV/PV_OC open-collector fault
  comparators. Probing TP9 lets the operator watch the analog EN/fault
  level at the PV eFuse (IC4) EN/UVLO pin while firmware toggles PA16
  (PV_EN): a clean transition proves PA16 reached the eFuse; a comparator
  pulling it low proves a hardware fault-injection path engaged. Open-
  collector + pull-up → idle ~3.3 V (no fault, EN asserted region), pulled
  low on fault. Logic-level, safe with common bench ground. This is a
  scope-diagnostic point, not a power rail. NOTE: this CONTRADICTS nothing
  in Sources 5/6 — it explains why the PCB net name is `BAT_OC_F` despite
  the node functionally being the PV-EN/fault-gate.
- **Why I'm recording it:** Resolves TP9 semantics flagged in Source 5;
  reconciles raw net name vs functional meaning for the bench operator.
