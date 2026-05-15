# Research Log — Agent Q: PCB test points TP15–TP17 → net map + full TP roster

Purpose: map TP15..TP17 on the CHESS EPS PCU testing board V4.1 to their
schematic nets, and produce the authoritative complete list of every TPx
that exists on the board, so bench probing is complete and nothing is missed.

Ground rules:
- Prefer the actual PCB project files (.kicad_pcb, .kicad_sch, BOM .csv)
  over any prose doc or prior research log.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-16.

---

## Source 1: testingPCU.kicad_pcb — TP footprint roster + TP15 net

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb`
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  Grepping `(property "Reference" "TPnn"` yields exactly 21 TP footprints:
  TP1 through TP21, each one a distinct `TestPoint:*` footprint. No
  duplicates. The numbering is contiguous 1..21 (no gaps). This OVERTURNS
  the assumption that the board has ~17 TPs — there are 21, including
  TP18/TP19/TP20/TP21 beyond the assumed range.
  TP15 footprint block (line ~13493): `(footprint "TestPoint:TestPoint_Pad_D1.5mm"` `(at 127.2 105)` layer `"F.Cu"`, `(sheetname "/BAT/")`, pad 1 net = `(net 105 "/CTRL/BAT_UV_F")`.
- **Confidence: HIGH**
  Authoritative source = the actual KiCad PCB layout file with placed
  footprints and copper net assignments. This is the design ground truth.
- **Implication for our build / bench test:**
  Full roster = TP1..TP21 (21 points), contiguous. Operator must look for
  21 labelled pads, not 17. TP15 = net `/CTRL/BAT_UV_F` (battery
  under-voltage fault flag, a logic/comparator signal from the CTRL sheet),
  located at PCB (127.2, 105) on the FRONT (F.Cu) side, near the BAT
  subsystem / connector J8. TP15 is NOT a BAT post-eFuse power node — it is
  a fault/status digital line.
- **Why I'm recording it:**
  Establishes the authoritative roster count and TP15 identity for the
  bench probe map.

## Source 2: testingPCU.kicad_pcb — TP16 and TP17 net assignments

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb`
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP16 footprint (line ~3844): `(at 127.25 108)` layer `"F.Cu"`,
  `(sheetname "/BAT/")`, pad 1 = `(net 65 "/-BAT")`.
  TP17 footprint (line ~12862): `(at 118 91.7)` layer `"F.Cu"`,
  `(sheetname "/BAT/")`, pad 1 = `(net 19 "/VBAT")`.
  So TP16 = battery negative/return net `/-BAT`; TP17 = battery positive
  rail `/VBAT`. These are battery TERMINAL nets, NOT "post-eFuse" nodes.
  The prior-doc claim that TP16/TP17 are "BAT post-eFuse points" is
  CONTRADICTED by the copper net names: `/-BAT` is the battery return and
  `/VBAT` is the battery rail. Whether `/VBAT` is pre- or post-eFuse must
  be confirmed from BAT.kicad_sch (next source).
- **Confidence: HIGH** (for net identity)
  Direct copper net assignment in the authoritative layout file.
- **Implication for our build / bench test:**
  TP16 = `/-BAT` (battery minus / a ground-referenced battery return —
  treat as a return node, measure other BAT points relative to it).
  TP17 = `/VBAT` (battery positive bus). Under current bench condition
  (only 3.3 V on J5, no battery connected), TP17 reads ~0 V (or whatever a
  battery emulator supplies). With a real battery these can be at full
  pack voltage — probe with care, NOT a logic node. TP16 likely tied to
  system GND through the BAT sense path — verify in schematic.
- **Why I'm recording it:**
  Core deliverable: TP16/TP17 net identity, and it overturns the
  unverified "post-eFuse" assumption — flagged for schematic confirmation.

## Source 3: BAT.kicad_sch + CTRL.kicad_sch — TP15 net trace (BAT_UV_F / BAT_EN)

- **URL / path:** `...\_pcb_files_agent_J\BAT.kicad_sch` and `...\_pcb_files_agent_J\CTRL.kicad_sch`
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  In BAT.kicad_sch the TP15 symbol (`uuid 03562545-...`, matches the
  .kicad_pcb path of TP15) pin 1 is at (139.7, 60.96). A wire runs
  `(xy 139.7 60.96) (xy 139.7 64.77)` then horizontally along y=64.77
  (x 135.89 -> 166.37). At x=135.89 sits `(hierarchical_label "BAT_EN"
  (shape input) (at 135.89 64.77 180))`. So TP15 net = BAT eFuse ENABLE
  node. At top level the .kicad_pcb names this copper `(net 105
  "/CTRL/BAT_UV_F")`. In CTRL.kicad_sch a text note states `TEST FOR UV
  (6.2 V) / IF IN4- > IN4+ / THEN BAT_UV_F = '0'` and `label "BAT_UV_F"`
  appears (lines 2874, 2904). Same physical net is the battery
  under-voltage comparator output AND the BAT eFuse enable input: the UV
  comparator gates the BAT eFuse enable.
- **Confidence: HIGH**
  Geometric wire trace in the schematic + matching footprint UUID path +
  the top-level copper net name in the layout all agree.
- **Implication for our build / bench test:**
  TP15 = the BAT eFuse enable / battery-UV-fault logic node
  (`/CTRL/BAT_UV_F` == BAT sheet `BAT_EN`). LOGIC-LEVEL signal
  (~0/3.3 V), gated by the 6.2 V UV comparator. Firmware verification:
  this is the BAT eFuse enable path — with the MCU asserting BAT eFuse
  enable (PA17) and the UV comparator not faulting, expect logic HIGH
  (~3.3 V); a battery-UV condition or firmware de-assert pulls it LOW.
  NOT a power node — safe with common bench ground.
- **Why I'm recording it:**
  Pins down TP15 specifically (deliverable 1) and refutes any "BAT
  post-eFuse power" reading of TP15.

## Source 4: testingPCU.kicad_pcb — IC7 (BAT eFuse TPS25940) pin/net map: PRE vs POST eFuse

- **URL / path:** `...\_pcb_files_agent_I\testingPCU.kicad_pcb` (IC7 footprint, ref line 31920; pads ~line 32149+)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  IC7 = `TPS25940` BAT eFuse. Its pads carry explicit `pinfunction`
  names with resolved net names:
  - Pads 9–13 `IN_1..IN_5` = `(net 17 "/+BAT")` -> battery raw INPUT
  - Pads 4–8 `OUT_1..OUT_5` = `(net 19 "/VBAT")` -> eFuse OUTPUT/load side
  - Pad 14 `EN` = `(net 105 "/CTRL/BAT_UV_F")` -> confirms TP15 = eFuse EN
  - Pad 2 `PGOOD` = `/BAT_PGOOD`; pad 19 `IMON` = `/BAT_IMON`;
    pad 20 = `/~{BAT_FLT}`; pads 1/16 = GND.
  So the raw battery rail is `/+BAT` (net 17, eFuse INPUT) and
  `/VBAT` (net 19) is the eFuse OUTPUT. `/-BAT` (net 65) connects to
  TP16 and to sense/divider resistors R29, R31, R34, R36 (NOT the GND
  net 1) -> it is the battery-negative sense/return node.
- **Confidence: HIGH**
  The eFuse's own pin-function strings in the authoritative layout file
  unambiguously label IN vs OUT; no inference required.
- **Implication for our build / bench test:**
  RESOLVED key question: **TP17 `/VBAT` is the POST-eFuse (load-side)
  battery rail** — this CONFIRMS the prior docs' "TP17 = BAT post-eFuse"
  claim. Raw battery input is `/+BAT` (net 17), which has no dedicated TP
  in 15–17. TP16 `/-BAT` = battery-negative sense/return (separate from
  board GND net 1). With the BAT eFuse enabled by firmware (PA17 ->
  CTRL -> EN), TP17 should rise to ~battery voltage; with the eFuse
  disabled or UV-faulted, TP17 ≈ 0 V. That makes TP17 the natural probe
  to verify firmware BAT-eFuse-enable behaviour.
- **Why I'm recording it:**
  This is THE deliverable-1 question (pre/post eFuse). Now answered with
  a cited proof line.

## Source 5: testingPCU.kicad_pcb (all TP footprints) + testingPCU.csv BOM — authoritative full roster

- **URL / path:** `...\_pcb_files_agent_I\testingPCU.kicad_pcb` (21 TP footprints) and `...\_pcb_files_agent_I\testingPCU.csv` (line 52)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  Extracted every TP footprint's position/side/sheet/net. ALL 21 TPs are
  on `layer "F.Cu"` (FRONT side), footprint `TestPoint_Pad_D1.5mm`.
  Roster (TP -> net): TP1 `Net-(D1-K)` (/AUX); TP2 `/AUX/LDO_12V`;
  TP3 `/AUX/LDO_5V`; TP4 `/AUX/LDO_3V3`; TP5 `/AUX/3V3`; TP6 `/AUX_5V`;
  TP7 `/AUX/AUX_12V`; TP8 `/PV_RAW` (/PV); TP9 `/CTRL/BAT_OC_F` (/PV);
  TP10 `/+PV`; TP11 `/-PV`; TP12 `/BUCK/BUCK_IN`; TP13 `/BUCK/BUCK_OUT`;
  TP14 `/+BAT` (/BAT, raw battery input/eFuse IN); TP15 `/CTRL/BAT_UV_F`;
  TP16 `/-BAT`; TP17 `/VBAT` (post-eFuse); TP18 `Net-(IC9-1IN+)` (/CTRL);
  TP19 `/OUTV1`; TP20 `/OUTV2`; TP21 `/CTRL/IN3_MINUS`.
  BOM csv line 52: `"TP1,TP2,...,TP21",TestPoint,
  TestPoint:TestPoint_Pad_D1.5mm,~,test point (alternative shape),21,
  ...,Excluded from BOM`.
- **Confidence: HIGH**
  Layout file + BOM agree exactly: 21 TPs, contiguous TP1..TP21, no gaps,
  no duplicates, all front-side, all "Excluded from BOM".
- **Implication for our build / bench test:**
  Authoritative roster = **TP1..TP21, 21 points, contiguous (no gaps),
  all on the FRONT (component) side**. "Excluded from BOM" here does NOT
  mean depopulated/missing — `TestPoint_Pad_D1.5mm` is a bare SMD copper
  pad by design (nothing to populate); the operator probes the bare pad
  directly. So all 21 pads physically exist; none are "empty footprints
  for a missing part". TP18..TP21 lie OUTSIDE the assumed 1..17 range and
  must be added to any probe checklist (TP18 IC9 comparator input,
  TP19 /OUTV1, TP20 /OUTV2, TP21 /CTRL/IN3_MINUS — CTRL sense/telemetry).
- **Why I'm recording it:**
  Deliverables 9–12: exact roster, gaps, populated status, TP18+ list.

---

# FINAL SUMMARY (Agent Q)

## TP15–TP17 detail (backing entries in brackets)

### TP15 — `/CTRL/BAT_UV_F` == BAT sheet `BAT_EN`  [S1, S3, S4]
- Net / subsystem: BAT eFuse (IC7 TPS25940) **EN** pin, driven by the CTRL
  battery under-voltage comparator (UV trip ~6.2 V). Logic/control node.
- Position: F.Cu (front), (127.2, 105), sheet /BAT/.
- Relative to key part: it is the **input EN of BAT eFuse IC7** (gates it).
- Expected V (bench: only 3.3 V on J5, no battery): depends on firmware +
  AUX 3V3 logic; nominally a 0/3.3 V logic level. With no battery and
  eFuse enable asserted by firmware it can read ~3.3 V; UV/fault or
  firmware de-assert -> ~0 V.
- Max safe probe V: logic level (~3.3 V); never a power rail. Safe.
- Firmware action verifiable: BAT eFuse enable path (MCU PA17 -> CTRL ->
  EN). Toggling firmware BAT-eFuse-enable should toggle this node.
- Probe safety: LOW-voltage logic, safe with common bench ground.

### TP16 — `/-BAT`  [S2, S4]
- Net / subsystem: battery-NEGATIVE sense/return node (net 65). Ties to
  TP16 + sense/divider resistors R29/R31/R34/R36. NOT board GND (net 1).
- Position: F.Cu (front), (127.25, 108), sheet /BAT/.
- Relative to key part: battery-minus terminal side / current-sense
  return; not gated by the eFuse.
- Expected V (bench, no battery): ~0 V / floating-ish through dividers.
- Max safe probe V: with a real battery this is the pack-negative; treat
  as a return reference, expect near board GND but verify before relying
  on it as ground.
- Firmware action verifiable: none directly (it is a sense/return node);
  used as the reference for BAT voltage/current sensing.
- Probe safety: reference node; generally safe but confirm it is not at a
  raised potential before clipping bench ground to it.

### TP17 — `/VBAT`  (POST BAT eFuse)  [S2, S4]
- Net / subsystem: **OUTPUT (load side) of BAT eFuse IC7** (pins OUT_1..5
  = net 19 /VBAT). The raw battery input is `/+BAT` (net 17, IC7 IN pins;
  exposed separately at TP14).
- Position: F.Cu (front), (118, 91.7), sheet /BAT/.
- Relative to key part: AFTER BAT eFuse IC7 (downstream of the pass FET).
- Expected V (bench, no battery, eFuse off/UV): ~0 V.
- Max safe probe V: full battery pack voltage when a real battery is
  connected and the eFuse is on — POWER node, probe with care.
- Firmware action verifiable: **YES** — with firmware asserting BAT eFuse
  enable (PA17, via CTRL -> IC7 EN) and no UV fault, TP17 rises to the
  battery voltage; disabling the eFuse drops TP17 to ~0 V. Best TP to
  confirm firmware BAT-eFuse-enable behaviour.
- Probe safety: switched power node (can be at full pack V); not a logic
  pin. Use care; reference to TP16/`-BAT` or board GND as appropriate.

NOTE: prior-doc claim "TP16/TP17 are BAT post-eFuse points" is
**partly right, partly wrong**: TP17 = post-eFuse `/VBAT` (CONFIRMED);
TP16 = `/-BAT` battery-negative sense/return (NOT a post-eFuse positive
rail); TP15 is the eFuse EN/UV logic node (NOT a power node at all).

## AUTHORITATIVE FULL ROSTER — TP1..TP21 (21 points, contiguous, no gaps, all FRONT/F.Cu, all "Excluded from BOM" = bare SMD pads, all physically present)  [S5]

| TP   | Net                  | Sheet  | Pos (x,y) F.Cu | Notes |
|------|----------------------|--------|----------------|-------|
| TP1  | Net-(D1-K)           | /AUX/  | 59.2, 62       | AUX diode D1 cathode |
| TP2  | /AUX/LDO_12V         | /AUX/  | 79, 69.25      | AUX 12 V LDO |
| TP3  | /AUX/LDO_5V          | /AUX/  | 93.75, 69.25   | AUX 5 V LDO |
| TP4  | /AUX/LDO_3V3         | /AUX/  | 109.5, 62      | AUX 3V3 LDO |
| TP5  | /AUX/3V3             | /AUX/  | 98.5, 77.75    | AUX 3V3 rail |
| TP6  | /AUX_5V              | /AUX/  | 83.5, 77.75    | AUX 5 V rail |
| TP7  | /AUX/AUX_12V         | /AUX/  | 68.25, 77.75   | AUX 12 V rail |
| TP8  | /PV_RAW              | /PV/   | 68.1, 91.7     | PV raw input |
| TP9  | /CTRL/BAT_OC_F       | /PV/   | 56.5, 86.25    | BAT over-current fault flag (logic) |
| TP10 | /+PV                 | /PV/   | 68, 98.6       | PV positive |
| TP11 | /-PV                 | /PV/   | 64.25, 129.25  | PV negative |
| TP12 | /BUCK/BUCK_IN        | /BUCK/ | 81.25, 124.5   | Buck input |
| TP13 | /BUCK/BUCK_OUT       | /BUCK/ | 106.05, 124.5  | Buck output |
| TP14 | /+BAT                | /BAT/  | 118, 98.5      | Raw battery / BAT eFuse INPUT |
| TP15 | /CTRL/BAT_UV_F (BAT_EN) | /BAT/ | 127.2, 105  | BAT eFuse EN / UV fault (logic) |
| TP16 | /-BAT                | /BAT/  | 127.25, 108    | Battery-negative sense/return |
| TP17 | /VBAT                | /BAT/  | 118, 91.7      | BAT eFuse OUTPUT (post-eFuse) |
| TP18 | Net-(IC9-1IN+)       | /CTRL/ | 79, 118.5      | CTRL comparator IC9 input+ |
| TP19 | /OUTV1               | /CTRL/ | 81.75, 107.25  | CTRL sense/telemetry OUTV1 |
| TP20 | /OUTV2               | /CTRL/ | 108, 106.75    | CTRL sense/telemetry OUTV2 |
| TP21 | /CTRL/IN3_MINUS      | /CTRL/ | 107.75, 118.5  | CTRL sense IN3- |

- Gaps: NONE. Numbering is fully contiguous TP1..TP21.
- TP numbers outside TP1..TP17: **TP18, TP19, TP20, TP21** (all /CTRL
  sense/telemetry/comparator nets, listed above).
- Not-populated: none in the "missing part" sense — every TP is a bare
  SMD copper pad by design (`TestPoint_Pad_D1.5mm`, "Excluded from BOM"
  in testingPCU.csv line 52). Operator should expect 21 bare labelled
  pads, all on the FRONT side.

## Could NOT resolve with full confidence
- Whether TP16 `/-BAT` is at exactly board-GND potential or slightly
  offset (sense path through R29/R31/R34/R36). Net is clearly the
  battery-negative sense/return, distinct from net 1 GND, but its DC
  potential vs GND under powered conditions was not numerically derived
  from the schematic — flagged for bench measurement.
- Exact firmware HIGH/LOW polarity of TP15 (BAT_UV_F/BAT_EN) was inferred
  from the CTRL note `IF IN4- > IN4+ THEN BAT_UV_F = '0'` (active-low
  fault); not cross-checked against src-pds firmware (out of scope here).
