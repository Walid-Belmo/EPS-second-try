# Research Log — Agent N: PCB test points TP1–TP5 → net map

Purpose: map physical test points TP1..TP5 on the CHESS EPS PCU testing
board V4.1 to their schematic nets, so a bench operator can probe them to
verify firmware-driven actuation (PWM, eFuse enables, rail voltages).

Ground rules:
- Prefer the actual PCB project files (.kicad_pcb, .kicad_sch, BOM .csv)
  over any prose doc or prior research log.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-16.

---

## Source 1: testingPCU.kicad_pcb — TP4 footprint

- **URL / path:** C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb (lines 1678–1780)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP4 is a `TestPoint:TestPoint_Pad_D1.5mm` SMD pad on `F.Cu` at `(at 109.5 62)`.
  Its single pad carries `(net 5 "/AUX/LDO_3V3")`. The footprint belongs to
  `(sheetname "/AUX/")` / `(sheetfile "AUX.kicad_sch")`. Reference proof:
  `(property "Reference" "TP4"` followed by `(net 5 "/AUX/LDO_3V3")`.
- **Confidence: HIGH**
  Authoritative source: the actual PCB layout file; reference and net are in
  the same footprint block, unambiguous.
- **Implication for our build / bench test:**
  TP4 = the AUX/LDO 3V3 rail (the regulated 3.3 V auxiliary supply that powers
  the MCU IC12). With only 3.3 V applied to J5 and the LDO active, operator
  should read ~3.3 V DC at TP4 vs GND. This verifies the board/MCU is powered;
  it is NOT a firmware-actuated net (always-on rail). Safe to probe with bench
  ground; low voltage (3.3 V).
- **Why I'm recording it:**
  Establishes TP4 as the 3V3 power-presence reference probe point.

## Source 2: testingPCU.kicad_pcb — TP3 footprint

- **URL / path:** ...\_pcb_files_agent_I\testingPCU.kicad_pcb (lines 4335–4437)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP3 = `TestPoint_Pad_D1.5mm` on F.Cu at `(at 93.75 69.25)`. Pad:
  `(net 4 "/AUX/LDO_5V")`. Sheet `/AUX/` (AUX.kicad_sch). Proof:
  `(property "Reference" "TP3"` ... `(net 4 "/AUX/LDO_5V")`.
- **Confidence: HIGH**
  Actual PCB layout file; reference and net in the same footprint block.
- **Implication for our build / bench test:**
  TP3 = AUX/LDO 5V rail. Operator should read ~5 V DC vs GND if the 5 V
  regulator is fed. With only 3.3 V on J5 this rail may be 0 V or low if the
  5 V LDO is downstream of a higher input — verify with AUX schematic.
  Always-on rail, not firmware-actuated. Safe to probe; low voltage.
- **Why I'm recording it:**
  TP3 is a supply-rail presence probe point (5 V).

## Source 3: testingPCU.kicad_pcb — TP2 footprint

- **URL / path:** ...\_pcb_files_agent_I\testingPCU.kicad_pcb (lines 5852–5954)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP2 = `TestPoint_Pad_D1.5mm` on F.Cu at `(at 79 69.25)`. Pad:
  `(net 3 "/AUX/LDO_12V")`. Sheet `/AUX/`. Proof:
  `(property "Reference" "TP2"` ... `(net 3 "/AUX/LDO_12V")`.
- **Confidence: HIGH**
  Actual PCB layout file; unambiguous.
- **Implication for our build / bench test:**
  TP2 = AUX/LDO 12V rail. Operator should read ~12 V if a 12 V source feeds
  the AUX block. With only 3.3 V on J5, expect ~0 V (no 12 V source). Always-on
  rail, NOT firmware-actuated. CAUTION: up to 12 V if powered — within safe
  probe range but note it is the highest AUX rail.
- **Why I'm recording it:**
  TP2 is the 12 V rail presence probe point.

## Source 4: testingPCU.kicad_pcb — TP5 footprint

- **URL / path:** ...\_pcb_files_agent_I\testingPCU.kicad_pcb (lines 9488–9590)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP5 = `TestPoint_Pad_D1.5mm` on F.Cu at `(at 98.5 77.75)`. Pad:
  `(net 7 "/AUX/3V3")`. Sheet `/AUX/`. Proof:
  `(property "Reference" "TP5"` ... `(net 7 "/AUX/3V3")`.
- **Confidence: HIGH**
  Actual PCB layout file; unambiguous.
- **Implication for our build / bench test:**
  TP5 = AUX/3V3 net. NOTE there are TWO 3.3 V-related nets: `/AUX/LDO_3V3`
  (TP4, net 5) and `/AUX/3V3` (TP5, net 7). Must check AUX.kicad_sch whether
  3V3 is the post-jumper / board-distributed 3.3 V vs LDO_3V3 the raw LDO out.
  Likely the actual 3.3 V powering MCU/digital. Operator should read ~3.3 V.
  Not firmware-actuated; power-presence reference. Safe to probe.
- **Why I'm recording it:**
  TP5 is the distributed 3V3 rail probe point; need to disambiguate vs TP4.

## Source 5: testingPCU.kicad_pcb — TP1 footprint

- **URL / path:** ...\_pcb_files_agent_I\testingPCU.kicad_pcb (lines 28515–28617)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP1 = `TestPoint_Pad_D1.5mm` on F.Cu at `(at 59.2 62)`. Pad:
  `(net 2 "Net-(D1-K)")`. Sheet `/AUX/`. Proof:
  `(property "Reference" "TP1"` ... `(net 2 "Net-(D1-K)")`.
- **Confidence: HIGH (for net identity) / MEDIUM (for meaning)**
  PCB file is authoritative for the net name, but `Net-(D1-K)` is an
  auto-generated name = cathode of diode D1. Need AUX.kicad_sch to know what
  D1 is (input protection / OR-ing diode) and what voltage it carries.
- **Implication for our build / bench test:**
  TP1 = cathode of D1 in the AUX sheet — likely the input-protection /
  reverse-blocking diode of the auxiliary supply (the J5 3.3 V input feed,
  post-diode). Must trace D1 in AUX.kicad_sch. Not firmware-actuated.
- **Why I'm recording it:**
  TP1 is a supply-input node; must resolve D1's role before stating expected V.

## Note: full TP set observed

Grep of `(property "Reference" "TPx"` in testingPCU.kicad_pcb shows test
points TP1–TP21 exist (TP1..TP21 references all present). TP1–TP5 are mine;
TP6–TP21 belong to other agents (not researched here).

## Source 6: AUX_SUPPLY.kicad_sch — D1 (TP1) trace + LDO rail labels

- **URL / path:** C:\Users\iceoc\Documents\EPS-second-try\research_logs\_tmp_schematics\AUX_SUPPLY.kicad_sch (D1 symbol L8223-8340; pin lib def L1779-1815; labels L3704-3783; AUX_IN hier label L3916-3919; wires around y=33.02)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  D1 = `(property "Value" "YQ1MM10ATFTR")`, `(property "Description"
  "Schottky Diodes & Rectifiers RECT 100V 1A SM SCHOTTKY")` — a ROHM 100 V / 1 A
  Schottky reverse-protection diode placed at `(at 44.45 33.02 0) (mirror y)`.
  Symbol pins: pin 1 = `(name "K")` cathode at local (2.54,0); pin 2 =
  `(name "A")` anode at local (17.78,0). With `(mirror y)` about origin
  (44.45,33.02): cathode K → global (41.91, 33.02); anode A → global
  (26.67, 33.02). Wire `(xy 24.13 33.02) (xy 26.67 33.02)` ties the anode to
  hierarchical_label `AUX_IN` `(at 24.13 33.02 180)`. From the cathode the
  rail runs right via wires (41.91→44.45→55.88→68.58 …) toward label
  `LDO_12V (at 127 33.02)` and the 12 V regulator. Internal AUX nets seen:
  `LDO_12V`, `LDO_5V`, `LDO_3V3` (raw regulator outputs) and a separate `3V3`
  net (labels at 116.84,120.65 and 195.58,92.71). Sheet outputs are the
  hierarchical labels AUX_3V3 / AUX_5V / AUX_12V / VDDIN / VDDANA / VDDCORE /
  VDDIO. On the top sheet testingPCU.kicad_sch the AUX_SUPPLY child-sheet pin
  `AUX_IN (at 109.22 48.26)` is wired `(xy 97.79 48.26)(xy 109.22 48.26)` to
  label `PV_RAW (at 97.79 48.26)`.
- **Confidence: HIGH**
  Pin names/coords come from the symbol library definition and the placed
  instance with explicit mirror; wire endpoints and labels are exact matches
  in the actual schematic. Net direction (anode=AUX_IN, cathode=TP1=toward
  regulators) is geometrically proven, not inferred.
- **Implication for our build / bench test:**
  TP1 (`Net-(D1-K)`) = the AUX-supply input rail AFTER the D1 Schottky
  reverse-protection diode. Source path: top-sheet PV_RAW → AUX_IN → D1 anode
  → D1 cathode = TP1 → feeds the AUX 12 V / 5 V / 3.3 V regulators. It is the
  protected raw input, NOT a regulated rail and NOT firmware-actuated.
  Under our bench condition (only 3.3 V into J5, no PV/BAT power) PV_RAW is
  unpowered, so expect TP1 ≈ 0 V. If PV_RAW is later powered, TP1 = PV_RAW
  minus one Schottky drop (~0.3–0.5 V). Max design rating of D1 is 100 V but
  PV_RAW on this system is low-voltage (single-digit V); treat ≤ ~20 V as the
  practical safe-probe ceiling. DC node, safe with common bench ground.
  Firmware verification: NONE directly — TP1 is upstream of all regulators and
  is not switched by any MCU pin. It is only an input-power-presence probe
  (confirms PV_RAW reaching the AUX block past D1).
  This also resolves the TP4 vs TP5 question: `LDO_3V3` (TP4) is the raw 3.3 V
  regulator output; `3V3` (TP5) is the separate distributed/filtered 3.3 V net
  that becomes AUX_3V3/VDDIN/VDDIO/VDDCORE for the MCU. LDO_5V (TP3) and
  LDO_12V (TP2) are likewise the raw 5 V and 12 V regulator outputs.
- **Why I'm recording it:**
  Converts the auto-name `Net-(D1-K)` into a functional identity (post-D1 AUX
  input) and fixes expected bench voltages for TP1–TP5.

## Source 7: AUX_SUPPLY.kicad_sch — 3V3 / LDO_3V3 OR-diode topology (TP4, TP5)

- **URL / path:** C:\Users\iceoc\Documents\EPS-second-try\research_logs\_tmp_schematics\AUX_SUPPLY.kicad_sch (D2 BAT54W L6906-6915 at (104.14,114.3); second BAT54W L4938-4946 at (104.14,128.27); PDU_3V3 hier input L3795-3797 at (62.23,114.3); LDO_3V3 label L3714-3715 at (81.28,128.27); 3V3 label L3754-3755 at (116.84,120.65); AUX_3V3 hier output L3872-3873 region near (132.08,120.65); wires L2366,2646,2746,2776,3106,3166,3456,3536)
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  The 3.3 V section uses a BAT54W dual-Schottky OR-ing network (`(property
  "Reference" "D2")`, `(property "Value" "BAT54W-HG3-08")`). One input leg
  comes from hierarchical input `PDU_3V3` (external 3.3 V, wire
  `(xy 62.23 114.3)(xy 86.36 114.3)`), the other from `LDO_3V3` (the on-board
  3.3 V regulator output, wire `(xy 81.28 128.27)(xy 86.36 128.27)`). The
  OR-ed common cathode node is the `3V3` net at (116.84,120.65): junction
  wires `(xy 107.95 120.65)(xy 107.95 114.3)` and `(xy 107.95 120.65)
  (xy 107.95 128.27)` tie both legs to it, and `(xy 113.03 120.65)
  (xy 132.08 120.65)` carries `3V3` out to hierarchical output `AUX_3V3`
  (and on to VDDIN/VDDIO/VDDCORE for the MCU).
- **Confidence: HIGH**
  Component value/reference, hierarchical-label coords, and wire endpoints are
  exact matches in the actual schematic; the OR-diode topology (two inputs,
  one common output node) is geometrically unambiguous.
- **Implication for our build / bench test:**
  Resolves the TP4 vs TP5 ambiguity definitively:
  • TP5 = `/AUX/3V3` = the OR-ed 3.3 V RAIL = max(PDU_3V3, LDO_3V3) minus a
    Schottky drop. This is the rail that actually powers the running MCU.
    Under our bench condition (external 3.3 V on J5 → PDU_3V3 path) TP5 should
    read ≈ 3.0–3.3 V (3.3 V minus ~0.2–0.3 V BAT54 drop). Strong "MCU is
    alive" probe. NOT firmware-actuated (always-on while board is powered).
  • TP4 = `/AUX/LDO_3V3` = the RAW output of the on-board 3.3 V regulator,
    BEFORE the OR diode. On our bench we feed external 3.3 V (PDU_3V3), and
    the LDO is fed from the AUX_IN/PV_RAW chain which is cold (see Source 6),
    so the on-board LDO is unpowered → expect TP4 ≈ 0 V even though the MCU
    runs (it runs off PDU_3V3 via TP5). TP4 = 0 V while TP5 ≈ 3.3 V is the
    EXPECTED, correct bench signature, not a fault.
  Both are low-voltage DC, safe with common bench ground; max ~3.6 V if
  powered later. Neither is firmware-switched.
- **Why I'm recording it:**
  Prevents the operator from mis-reading TP4≈0 V as a board fault and confirms
  TP5 as the definitive MCU-power-presence probe.

## Source 8: AUX_SUPPLY.kicad_sch — 5V & 12V OR-diode topology (TP2, TP3)

- **URL / path:** C:\Users\iceoc\Documents\EPS-second-try\research_logs\_tmp_schematics\AUX_SUPPLY.kicad_sch (OR-diode instances at (104.14,143.51) L5180, (104.14,157.48) L5367, (104.14,173.99) L5060, (104.14,186.69) L4438; PDU_5V L3806-3808 (62.23,143.51); PDU_12V L3839-3841 (62.23,173.99); LDO_5V label L3724 (81.28,157.48); LDO_12V label L3764 (81.28,186.69); AUX_5V out L3905 (132.08,149.86); AUX_12V out L3850 (132.08,181.61))
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  The 5 V and 12 V sections replicate the 3 V OR-diode pattern (Source 7).
  Each has an OR-ing Schottky pair: hierarchical input `PDU_5V` OR internal
  `LDO_5V` → `AUX_5V`; and `PDU_12V` OR `LDO_12V` → `AUX_12V`. The `LDO_5V`
  and `LDO_12V` labels are the RAW on-board-regulator outputs on the input
  side of the OR diodes; `AUX_5V`/`AUX_12V` are the OR-ed rail outputs that
  leave the sheet.
- **Confidence: HIGH**
  Component placements and the PDU_x / LDO_x / AUX_x hierarchical-label and
  wire coordinates are exact matches; topology is identical to the
  independently-verified 3V3 section (Source 7).
- **Implication for our build / bench test:**
  • TP2 = `/AUX/LDO_12V` = raw output of the on-board 12 V regulator, BEFORE
    its OR diode. The on-board 12 V regulator is fed from the AUX_IN/PV_RAW
    chain (Source 6), which is COLD on our bench → expect TP2 ≈ 0 V. (Applying
    an external PDU_12V would NOT light up TP2; it feeds the other diode leg
    and AUX_12V only.) Max ~12–13 V if PV_RAW later powers the LDO.
  • TP3 = `/AUX/LDO_5V` = raw output of the on-board 5 V regulator, BEFORE its
    OR diode. Same reasoning → expect TP3 ≈ 0 V on our current bench. Max
    ~5–5.5 V if PV_RAW later powers it.
  Both DC, safe with common bench ground. NEITHER is firmware-actuated — they
  are always-on regulator outputs, not switched by any MCU pin. TP2≈0 V and
  TP3≈0 V on the current bench (3.3 V-only, PV cold) is the EXPECTED correct
  signature, not a fault — exactly like TP4 (LDO_3V3≈0 V) in Source 7.
- **Why I'm recording it:**
  Fixes expected bench voltages for TP2/TP3 and prevents mis-reading their
  0 V as a fault; confirms none of TP1–TP5 verifies a firmware action.

---

# FINAL SUMMARY TABLE — TP1–TP5 (Agent N)

Bench condition assumed: only 3.3 V applied at J5, buck cold, NO PV/BAT power.
All TP footprints are `TestPoint_Pad_D1.5mm` on `F.Cu`, sheet AUX_SUPPLY
(child-sheet UUID 5314f4df-93fe-4be1-a0f8-df63d80e8237).

| TP  | Net (PCB)        | Functional meaning                                           | Subsystem            | Pre/Post component                              | Expected V on our bench | Max safe probe V (if powered later) | Firmware action it verifies | Common-gnd safe? | PCB (x,y) F.Cu | Backing log entries |
|-----|------------------|--------------------------------------------------------------|----------------------|-------------------------------------------------|-------------------------|-------------------------------------|-----------------------------|------------------|----------------|---------------------|
| TP1 | `Net-(D1-K)`     | AUX-supply input AFTER D1 Schottky reverse-protection diode; fed by top-sheet PV_RAW → AUX_IN | AUX input (raw)      | POST D1 (cathode); UPSTREAM of all AUX regulators | ≈ 0 V (PV_RAW cold)     | ~20 V practical (D1 rated 100 V/1 A) | NONE (input rail, not MCU-switched) | Yes (DC) | (59.2, 62)     | 1(orig PCB),5,6     |
| TP2 | `/AUX/LDO_12V`   | RAW output of on-board 12 V regulator, BEFORE its OR-diode    | AUX 12 V (raw LDO)   | PRE OR-diode; regulator fed from AUX_IN/PV_RAW   | ≈ 0 V (LDO unpowered)   | ~12–13 V                            | NONE (always-on rail)       | Yes (DC) | (79, 69.25)    | 3,6,8               |
| TP3 | `/AUX/LDO_5V`    | RAW output of on-board 5 V regulator, BEFORE its OR-diode     | AUX 5 V (raw LDO)    | PRE OR-diode; regulator fed from AUX_IN/PV_RAW   | ≈ 0 V (LDO unpowered)   | ~5–5.5 V                            | NONE (always-on rail)       | Yes (DC) | (93.75, 69.25) | 2,6,8               |
| TP4 | `/AUX/LDO_3V3`   | RAW output of on-board 3.3 V regulator, BEFORE its OR-diode (D2 BAT54W) | AUX 3.3 V (raw LDO) | PRE OR-diode D2; regulator fed from AUX_IN/PV_RAW | ≈ 0 V (LDO unpowered; MCU runs off PDU_3V3 via TP5) | ~3.6 V | NONE (always-on rail) | Yes (DC) | (109.5, 62)    | 1(this log),7       |
| TP5 | `/AUX/3V3`       | OR-ed 3.3 V RAIL = max(PDU_3V3, LDO_3V3) − Schottky drop; powers MCU (→ AUX_3V3/VDDIN/VDDIO/VDDCORE) | AUX 3.3 V (final rail) | POST OR-diode D2 | ≈ 3.0–3.3 V (external 3.3 V via PDU_3V3) | ~3.6 V | NONE directly (but = "MCU is powered/alive" presence check) | Yes (DC) | (98.5, 77.75)  | 4,7                 |

## Resolution status
- TP1: RESOLVED (net HIGH; functional meaning HIGH via geometric D1 trace).
- TP2: RESOLVED (HIGH).
- TP3: RESOLVED (HIGH).
- TP4: RESOLVED (HIGH).
- TP5: RESOLVED (HIGH).
- TP in my range I could NOT resolve: NONE.

## Key bench-test conclusions
1. NONE of TP1–TP5 verifies a firmware-driven actuation (no buck PWM, no
   eFuse enable, no LED, no MCU GPIO is on these nets). They are all AUX
   power-supply nodes. For firmware actuation proof the operator must use
   other TPs (TP6–TP21, other agents' scope).
2. TP5 (`/AUX/3V3`) is the single most useful of my five for bench work: it
   should read ≈ 3.3 V and confirms the MCU is actually powered/running while
   we exercise firmware via other probes.
3. TP1/TP2/TP3/TP4 reading ≈ 0 V on the current bench is EXPECTED and
   CORRECT (PV_RAW/AUX_IN is cold, so the on-board LDOs are unpowered; the
   MCU lives off external PDU_3V3 through the D2 OR-diode → TP5). Do not flag
   these zeros as faults.
4. Full board test-point set per BOM/PCB: TP1–TP21 (21 points). Only TP1–TP5
   analysed here.
