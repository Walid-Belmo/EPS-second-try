# Research Log — Agent I: PWM probe points on PCU V4.1

Purpose: Identify exactly where on the physical PCU V4.1 board to put
oscilloscope probes for PA12 (PWM_H), PA13 (PWM_L), and a clean ground
reference, so the user can perform Test A — PWM waveform sign-off —
without having to read the schematic himself. The downstream decision
is the probe layout in the "Bench setup per test" section of
`src-pds/how_to_test.md`.

Ground rules:
- Prefer official primary sources (the PCB repo schematics, the BOM,
  component datasheets) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-15.

---

## Source 1: Top-level schematic `testingPCU.kicad_sch` — PWM_H / PWM_L nets cross from MCU sheet to BUCK sheet

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_sch` (downloaded via `gh api repos/CHESS-mission/eps_pcu_eng/contents/testingPCU.kicad_sch` on 2026-05-15; local cached copy `research_logs/_pcb_files_agent_I/testingPCU.kicad_sch`)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Confirms that on the top-level sheet, the MCU sub-sheet exposes two output pins named `PWM_H` and `PWM_L`, and the BUCK sub-sheet exposes two input pins named `BUCK_HS_IN` and `BUCK_LS_IN`. The board-level nets that bridge them are literally labelled `PWM_H` and `PWM_L` at both ends — i.e. the PCB net name is `PWM_H` / `PWM_L`, not `BUCK_HS_IN` / `BUCK_LS_IN`. The BUCK-sheet labels of the same wires are `PWM_H` at `(184.15, 212.09)` and `PWM_L` at `(184.15, 219.71)`, immediately to the left of the BUCK-sheet pin stubs:
  ```
  2855: (label "PWM_H" (at 200.66 101.6 90) ...)        ; MCU side
  2865: (label "PWM_L" (at 208.28 101.6 90) ...)        ; MCU side
  2975: (label "PWM_H" (at 184.15 212.09 0) ...)        ; BUCK side
  2655: (label "PWM_L" (at 184.15 219.71 0) ...)        ; BUCK side
  5747:   (pin "PWM_H" output (at 200.66 93.98 270) ...)  ; MCU sheet pin
  5757:   (pin "PWM_L" output (at 208.28 93.98 270) ...)  ; MCU sheet pin
  5534:   (pin "BUCK_HS_IN" input (at 193.04 212.09 180) ...)  ; BUCK sheet pin
  5524:   (pin "BUCK_LS_IN" input (at 193.04 219.71 180) ...)  ; BUCK sheet pin
  ```
- **Proof — why this source is trustworthy here:**
  Direct text of the KiCad schematic in the official PCB repo on the default branch. The pin and label coordinates match line-for-line on the BUCK side: BUCK-sheet pin `BUCK_HS_IN` is at y=212.09 and label `PWM_H` is at the same y=212.09. Same for the LS pair at y=219.71. This is mathematically the same horizontal wire, confirming `BUCK_HS_IN` = `PWM_H` net and `BUCK_LS_IN` = `PWM_L` net.
- **Confidence: HIGH**
  Primary schematic source, exact text quoted, line numbers given. Anyone can re-read the file and confirm.
- **Implication for our build:**
  When the user probes the board, the net the probe is touching is called `PWM_H` (for PA12) and `PWM_L` (for PA13). The "BUCK_HS_IN" / "BUCK_LS_IN" names are local to the BUCK sub-sheet — they are the same physical copper traces. So any pad on the wire between MCU pin 12 and EPC2152 high-side input is a valid PA12 probe point. Likewise PA13 to EPC2152 low-side input.
- **Why I'm recording it:**
  This resolves the schematic-name vs board-net ambiguity before I look for test points.

---

## Source 2: `MCU.kicad_sch` — wires from MCU pin PA12/PA13 to the sheet exit are direct, no intermediate components

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/MCU.kicad_sch` (local cached copy `research_logs/_pcb_files_agent_I/MCU.kicad_sch`)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  On the MCU sheet, the only conductive elements between the MCU pin and the sheet exit on each PWM net are a single straight wire. There is no series resistor, no test point, no via stub, nothing else attached.
  - PWM_H wire (line 3733–3734): `(xy 180.34 119.38) (xy 180.34 133.35)` — from MCU pin PA12 (at y=119.38) straight down to the sheet pin (at y=133.35).
  - PWM_L wire (line 3373–3374): `(xy 182.88 119.38) (xy 182.88 133.35)` — from MCU pin PA13 to the sheet pin.
  - Label `"PWM_H"` at `(180.34, 121.92, 270)` (line 3992) sits exactly on the PWM_H wire.
  - Label `"PWM_L"` at `(182.88, 121.92, 270)` (line 4062) sits exactly on the PWM_L wire.
  - The hierarchical sheet-pin exits are `"PWM_H"` at `(180.34, 133.35)` (line 4134) and `"PWM_L"` at `(182.88, 133.35)` (line 4222).
  No test point symbol appears on the MCU sheet at all (`Grep` for `"TP[0-9]+"` returned no matches).
- **Proof — why this source is trustworthy here:**
  Direct text of the official KiCad schematic on `main`. The wire endpoints, label positions, and sheet-pin positions all share the same X coordinate, which is the only condition needed in a KiCad schematic for them to be electrically the same net. No other component coordinate touches this line on the MCU sheet.
- **Confidence: HIGH**
  Primary schematic, quoted coordinates, exhaustive (full file grepped for test-point references).
- **Implication for our build:**
  On the PCB, the only points the user can touch electrically on the PWM_H net on the MCU side are: (a) MCU pin PA12 itself, and (b) the via or copper that carries the trace from PA12 to the EPC2152. Same for PWM_L / PA13. There is no PCB-side shaping resistor that gives a convenient probe pad here.
- **Why I'm recording it:**
  Eliminates the MCU side as a probe option except for the bare MCU pin or a copper trace.

---

## Source 3: `BUCK.kicad_sch` — TP12 and TP13 exist on the BUCK sheet, but they are on BUCK_IN and BUCK_OUT, NOT on PWM_H/PWM_L

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/BUCK.kicad_sch` (local cached copy `research_logs/_pcb_files_agent_I/BUCK.kicad_sch`)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Two test-point symbols exist on the BUCK sheet:
  ```
  2320:    (symbol (lib_id "Connector:TestPoint_Alt") (at 82.55 102.87 0) ...
  2330:        (property "Reference" "TP12" ...
  2348:        (property "Footprint" "TestPoint:TestPoint_Pad_D1.5mm" ...
  2387:    (symbol (lib_id "Connector:TestPoint_Alt") (at 218.44 93.98 0) ...
  2397:        (property "Reference" "TP13" ...
  2415:        (property "Footprint" "TestPoint:TestPoint_Pad_D1.5mm" ...
  ```
  Wire tracing on the BUCK sheet:
  - TP12 anchor (82.55, 102.87) connects via wire line 1269 `(xy 82.55 102.87) (xy 82.55 109.22)` then line 1289 `(xy 82.55 109.22) (xy 129.54 109.22)`. Y=109.22 is the BUCK_IN row (hierarchical label `"BUCK_IN"` at `(78.74, 109.22)` line 1398). So **TP12 = BUCK_IN net** (power input to the converter, ~12 V rail).
  - TP13 anchor (218.44, 93.98) connects via wire line 1099 `(xy 218.44 93.98) (xy 218.44 99.06)` then line 1239 `(xy 218.44 99.06) (xy 233.68 99.06)`. Y=99.06 ends at hierarchical label `"BUCK_OUT"` at `(233.68, 99.06)` line 1387. So **TP13 = BUCK_OUT net** (the buck regulator's output rail, fed by the LC filter L2/C18 — NOT the switching node, NOT a PWM signal).
  - BUCK_HS_IN (PWM_H) wire: line 1039 `(xy 78.74 104.14) (xy 133.35 104.14)` — runs along y=104.14, and **no test-point symbol touches it**.
  - BUCK_LS_IN (PWM_L) wire: line 1019 `(xy 78.74 106.68) (xy 133.35 106.68)` — runs along y=106.68, and **no test-point symbol touches it**.
- **Proof — why this source is trustworthy here:**
  Direct schematic text. Coordinates of test-point symbols and label endpoints are exact and reproducible. Lines 1019/1039 are the only wires that share the BUCK_HS_IN / BUCK_LS_IN Y values, and neither (82.55, 102.87) nor (218.44, 93.98) lies on them.
- **Confidence: HIGH**
  Primary schematic, full file grepped for test points, all four BUCK sheet test points accounted for (TP12, TP13).
- **Implication for our build:**
  There is **no dedicated through-hole or pad test point for PWM_H or PWM_L anywhere on the board**. The user CANNOT probe via TPxx for these signals. They must probe a component pin: either MCU pin 12/13 (PA12/PA13) or the EPC2152 input pins (BUCK_HS_IN / BUCK_LS_IN, on IC6).
- **Why I'm recording it:**
  Confirms there is no easy test-point probe option. Forces the user to probe at a chip pin.

---

## Source 4: `BUCK.kicad_sch` — full test-point inventory across all sub-sheets confirms no PWM test point exists

- **URL / path:** Cross-search via `gh api ... | base64 -d | grep` over `AUX_SUPPLY.kicad_sch`, `BAT.kicad_sch`, `CTRL.kicad_sch`, `PV.kicad_sch` on `https://github.com/CHESS-mission/eps_pcu_eng/tree/main`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Test points TP1–TP21 are distributed across the sub-sheets as follows:
  - AUX_SUPPLY.kicad_sch — TP1, TP2, TP3, TP4, TP5, TP6, TP7
  - PV.kicad_sch — TP8, TP9, TP10, TP11
  - BUCK.kicad_sch — TP12, TP13
  - BAT.kicad_sch — TP14, TP15, TP16, TP17
  - CTRL.kicad_sch — TP18, TP19, TP20, TP21
  - MCU.kicad_sch — none
  - testingPCU.kicad_sch (top sheet) — none
  Neither `PWM_H` nor `PWM_L` appears in any of these sub-sheets (PWM nets are MCU-output → BUCK-input only).
- **Proof — why this source is trustworthy here:**
  Repository-wide grep over every official `.kicad_sch` file on the default branch. Exhaustive.
- **Confidence: HIGH**
  Primary sources, exhaustive enumeration.
- **Implication for our build:**
  Definitively: **no silkscreen test point exists for PA12 or PA13 anywhere on PCU V4.1**. The probe must land on a chip pin.
- **Why I'm recording it:**
  Closes the question "could there be a test point we missed".

---

## Source 5: `BUCK.kicad_sch` — EPC2152 reference designator is IC6 (NOT U1), and its input pins are #3 HSIN and #4 LSIN

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/BUCK.kicad_sch` (local copy)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The EPC2152 reference designator on the board is **IC6**, not U1. Schematic symbol placed at anchor `(133.35, 99.06)` (line 1695), `Reference = "IC6"` (line 1703). The chip's symbol-library pin map (lines 533–748 of the same file) gives:
  ```
  pin 1  VBOOT   offset (0,   0)
  pin 2  VDD     offset (0,  -2.54)
  pin 3  HSIN    offset (0,  -5.08)   ← high-side gate input (PWM_H)
  pin 4  LSIN    offset (0,  -7.62)   ← low-side  gate input (PWM_L)
  pin 5  VIN_1   offset (0, -10.16)
  pin 6  SW_1    offset (0, -12.7)
  pin 7  SW_2    offset (30.48,   0)
  pin 8  GND_1   offset (30.48, -2.54)
  pin 9  VIN_2   offset (30.48, -5.08)
  pin 10 SW_3    offset (30.48, -7.62)
  pin 11 SW_4    offset (30.48,-10.16)
  pin 12 GND_2   offset (30.48,-12.7)
  ```
  With the chip anchor at (133.35, 99.06), pin 3 (HSIN) is at absolute (133.35, 104.14) and pin 4 (LSIN) is at absolute (133.35, 106.68). The BUCK_HS_IN wire (line 1039) `(xy 78.74 104.14) (xy 133.35 104.14)` ends exactly on pin 3. The BUCK_LS_IN wire (line 1019) `(xy 78.74 106.68) (xy 133.35 106.68)` ends exactly on pin 4.
  Pins 8 (GND_1) and 12 (GND_2) of the EPC2152 sit at absolute (163.83, 101.6) and (163.83, 111.76) on the schematic.
- **Proof — why this source is trustworthy here:**
  Schematic pin definitions and wire endpoints share exact coordinates. Reference designator quoted verbatim. This is the same EPC2152 part already documented in `docs/mainboard_pinout_pcu_v4_1.md`, but that file mislabelled the reference as `U1`; this entry corrects it to `IC6` per the schematic.
- **Confidence: HIGH**
  Primary schematic file, exact coordinates, reference designator quoted verbatim from line 1703.
- **Implication for our build:**
  The user must look for chip **IC6** on the silkscreen to find the EPC2152. Its **pin 3 (HSIN)** is the PWM_H probe point and **pin 4 (LSIN)** is the PWM_L probe point. Its **pins 8 and 12 (GND_1, GND_2)** are clean signal-ground pads right next to the same chip — these are the recommended scope-ground clip locations because they minimize the loop area of the probe relative to the signal being measured (any ground far from IC6 will add noise to the captured edge).
- **Why I'm recording it:**
  Fixes the chip reference designator (IC6, not U1) and identifies the exact chip pins that the scope probe must touch.

---

## Source 6: `testingPCU.kicad_pcb` + `MCU.kicad_sch` — Connector J10 is a 2-pin through-hole probe header dedicated to PWM_H / PWM_L  **(THE BEST PROBE POINT)**

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_pcb` (local cached copy at `research_logs/_pcb_files_agent_I/testingPCU.kicad_pcb`), and `MCU.kicad_sch` same repo.
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  A two-pin pin-header footprint exists on the PCB at row 2387+ of the PCB file:
  ```
  2387:    (footprint "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
  2388:        (layer "F.Cu")
  2390:        (at 91.225 148.25 90)               ; rotation 90 deg
  2393:        (property "Reference" "J10" ...)
  2443:        (attr through_hole)
  2605:    (pad "1" thru_hole rect (at 0 0 90) (size 1.7 1.7) (drill 1)
  2611:           (net 45 "/PWM_H") (pinfunction "Pin_1") ... )
  2616:    (pad "2" thru_hole circle (at 0 2.54 90) (size 1.7 1.7) (drill 1)
  2622:           (net 44 "/PWM_L") (pinfunction "Pin_2") ... )
  ```
  Schematic side (MCU.kicad_sch line 6209 onwards):
  ```
  6209:    (symbol (lib_id "Connector:Conn_01x02_Pin")
  6211:           (at 237.49 41.91 0) ...
  6219:           (property "Reference" "J10" ...)
  ```
  Wires (lines 3444 and 3684 of MCU.kicad_sch) drop the connector pin coordinates onto the labels:
  - `(xy 255.27 41.91) (xy 242.57 41.91)` runs into pin 1 of J10 and terminates at label `PWM_H` (label at `(255.27, 41.91)`, line 3813).
  - `(xy 255.27 44.45) (xy 242.57 44.45)` runs into pin 2 of J10 and terminates at label `PWM_L` (label at `(255.27, 44.45)`, line 3903).
  J10 is listed in the BOM (`testingPCU.csv` row 51): `J10,Conn_01x02_Pin,Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical,...,Excluded from BOM,`. "Excluded from BOM" means no header pins are bought / soldered by default — but the **plated through-holes are still on the bare PCB** (a 1.0 mm drill with a 1.7 mm annular ring, which is plenty for a scope probe hook or grabber clip).
- **Proof — why this source is trustworthy here:**
  Two independent files agree (PCB + schematic). The wires terminating at the connector pin coordinates and at the PWM labels share exact (x, y) values. The footprint definition explicitly assigns net 45 ("/PWM_H") to pad 1 and net 44 ("/PWM_L") to pad 2. The BOM confirms the part. This is as authoritative as KiCad data gets.
- **Confidence: HIGH**
  Cross-confirmed across PCB file, MCU schematic, and BOM.
- **Implication for our build:**
  **This is the recommended probe point for Test A.** J10 is on the top side (F.Cu) of the board at PCB coordinates (91.225, 148.25). The pads are large (1.7 × 1.7 mm) plated through-holes on 2.54 mm (0.1") pitch. They sit on the bottom edge of the board, just to the LEFT of J7 (the 3-terminal screw block at (106.78, 148.05)) and just to the RIGHT of J6 (the 3-terminal screw block at (71.98, 148)). The footprint rotation is 90°, so the two pins are arranged with pin 1 directly above (PCB-Y smaller) and pin 2 below (PCB-Y larger) — wait, with rotation 90° around the (91.225, 148.25) origin and the original local pin 2 offset of (0, 2.54), after rotation the pin 2 lands at (91.225 - 2.54, 148.25) = (88.685, 148.25). So **pin 1 (PWM_H) is at PCB (91.225, 148.25)** and **pin 2 (PWM_L) is at PCB (88.685, 148.25)** — the two holes lie along the horizontal at Y=148.25, 2.54 mm apart in X. They are right next to the EPC2152 (IC6 at (90.175, 135.978)) — only ~12 mm below it.
  Pin 1 is the **square-pad hole** (rect, line 2605) and pin 2 is the **round-pad hole** (circle, line 2616). Convention on bare PCBs: the square pad is pin 1 → PWM_H. Most KiCad footprints also stamp a "1" in the silkscreen next to pin 1.
  **The user should clip / hook two scope probes here** rather than risk damaging the 0.32 × 0.32 mm IC6 SMD pads or the 0.25 × 0.9 mm MCU QFN64 pads.
- **Why I'm recording it:**
  This **completely changes the test-A probe strategy**: there are dedicated probe-friendly through-holes for PA12 and PA13. The plan to probe at chip pads (which was the only alternative thought from the previous schematic-only docs) is downgraded to a backup plan only used if J10 happens to be obstructed.

---

## Source 7: `testingPCU.kicad_pcb` — Physical placement of IC12 (MCU), IC6 (EPC2152), J10, J6, J7 on the PCB

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_pcb` (local cached copy)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Footprint placements (PCB-page coordinates, mm — note KiCad Y grows downward):
  - **Board outline** (Edge.Cuts, lines 35956–36033): rounded rectangle X = 43.25 to 143.25 (width 100 mm), Y = 54.25 to 154.25 (height 100 mm). So the board is 100 × 100 mm with rounded corners.
  - **IC12 (MCU ATSAMD21J17D-MUT, QFN64)** at `(at 118.18 74.8)` (line 26267), rotation 0, layer `F.Cu` (top side). Body is 9 × 9 mm. Sits in the **upper-right quadrant** of the board.
    - Pad 29 (PA12, net `/PWM_H`): offset `(2.25, 4.4)` (line 26751) → absolute (120.43, 79.2). Pad size 0.25 × 0.9 mm. Layer F.Cu (top).
    - Pad 30 (PA13, net `/PWM_L`): offset `(2.75, 4.4)` (line 26760) → absolute (120.93, 79.2). Pad size 0.25 × 0.9 mm. Layer F.Cu (top).
    - Pad 22 (GND_1): offset `(-1.25, 4.4)` → absolute (116.93, 79.2). Layer F.Cu. (closest MCU-side GND)
    - Pad 33 (GND_2): offset `(4.4, 3.75)` → absolute (122.58, 78.55). Layer F.Cu.
  - **IC6 (EPC2152 gate driver)** at `(at 90.175 135.978)` (line 35552), rotation 0, layer `F.Cu` (top side). Body about 4 × 2 mm. Sits **center-bottom of the board** (about 47 mm from left, 16 mm from the bottom edge).
    - Pad 3 (HSIN, net `/PWM_H`): offset `(-1.643, 0.32)` (line 35843) → absolute (88.532, 136.298). Pad 0.32 × 0.32 mm. Layer F.Cu (top).
    - Pad 4 (LSIN, net `/PWM_L`): offset `(-1.643, 0.96)` (line 35852) → absolute (88.532, 136.938). Pad 0.32 × 0.32 mm. Layer F.Cu (top).
    - Pad 8 (GND_1): offset `(-0.438, 0.96)` → absolute (89.737, 136.938). 0.32 × 1.25 mm. Layer F.Cu.
    - Pad 12 (GND_2): offset `(1.192, 0.96)` → absolute (91.367, 136.938). 0.32 × 1.25 mm. Layer F.Cu.
  - **J10 (Conn_01x02_Pin, the PWM probe header)** at `(at 91.225 148.25 90)` (line 2390), rotation 90°, layer `F.Cu` (top side). Through-hole, both pads visible top AND bottom (`layers "*.Cu" "*.Mask"`).
    - Pad 1 (PWM_H, square rect): local `(0, 0)` after 90° rotation → absolute (91.225, 148.25). 1.7 × 1.7 mm pad, 1.0 mm drill.
    - Pad 2 (PWM_L, round circle): local `(0, 2.54)` after 90° rotation → absolute (88.685, 148.25). 1.7 × 1.7 mm pad, 1.0 mm drill.
    - The two holes are 2.54 mm apart in X, on a horizontal row at Y=148.25, about 6 mm above the bottom edge of the board (Y=154.25). J10 sits ~12 mm directly below IC6 (which is at Y=135.978).
  - **J6 (3-pin screw terminal, near J10)** at `(71.98, 148)` rotation 0. Pads at X = 71.98, 75.79, 79.6 (all at Y=148). **J6 pad 2 = GND** (line "pad 2: net 1 GND" in the earlier extract). Through-hole, 1.0 mm drill, 1.65 × 1.65 mm.
  - **J7 (3-pin screw terminal, also near J10)** at `(106.78, 148.05)` rotation 0. Pads at X = 106.78, 110.59, 114.4. **J7 pad 2 = GND**. Through-hole, same size.
- **Proof — why this source is trustworthy here:**
  Every coordinate is quoted from the official KiCad PCB file on `main`, with line numbers. The net assignments are explicit `(net N "<name>")` entries inside each `pad` block — definitive in KiCad format.
- **Confidence: HIGH**
  Primary source, exhaustive pad-level data.
- **Implication for our build:**
  - **Recommended probe layout for Test A:** Hook one scope probe into **J10 pin 1** (the square-pad hole, east side) for PA12 / PWM_H. Hook a second scope probe into **J10 pin 2** (the round-pad hole, west side, 2.54 mm to the left of pin 1) for PA13 / PWM_L. Clip both probes' ground leads to the **middle screw of J6** (GND, ~13 mm to the west of J10). All four points are on the top side of the board, so the user does NOT need to flip the board. All four are along the bottom edge of the board, making them easy to reach.
  - **Fallback if J10 holes are obscured by a probe-pin already soldered or a connector:** use IC6 pin 3 (PWM_H, at PCB 88.532, 136.298) and IC6 pin 4 (PWM_L, at 88.532, 136.938), with the ground clip on IC6 pin 8 (89.737, 136.938) or pin 12 (91.367, 136.938). Caveat: these are 0.32 mm SMD pads — needs micro-clip / probe tip and a steady hand.
  - **Worst-case fallback:** MCU IC12 pads 29 / 30 on the south side of the chip — 0.25 × 0.9 mm pads at 0.5 mm pitch. Bridging the adjacent unused pads (PA14, PA11) with a slipped probe tip will short signals; avoid unless the cleanest signal is required and a micropositioner is available.
- **Why I'm recording it:**
  Anchors the abstract schematic findings (Sources 1–6) to physical PCB coordinates so the test instructions can name a specific hole/pad location.

---

## Source 8: `testingPCU.kicad_pcb` — All vias are tented (covered by solder mask), so the 0.6 mm vias on PWM_H / PWM_L are NOT usable as probe pads without scraping mask off

- **URL / path:** `testingPCU.kicad_pcb` line 102 (PCB-wide setup block)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  PCB-level tenting directive (line 102):
  ```
  (tenting front back)
  ```
  Multiple 0.6 mm vias (0.3 mm drill) exist on PWM_H (net 45) and PWM_L (net 44):
  - PWM_H vias at (97.5, 88.25), (97.5, 103.75), (86.69, 137.62), (120.25, 82.25)
  - PWM_L vias at (99.25, 89), (99.25, 103.75), (87.75, 138.5), (120.25, 83.25)
  But `(tenting front back)` means every via is covered with solder mask on both the top and bottom of the board — they are not exposed copper.
- **Proof — why this source is trustworthy here:**
  Single-line directive in the official PCB file. KiCad emits this exactly once at top level; no per-via override is present (`Grep "tent"` returned only line 102).
- **Confidence: HIGH**
  Direct source.
- **Implication for our build:**
  The user CANNOT probe a via to reach PWM_H / PWM_L without first scraping the solder mask off (not recommended on a shared board). All vias on these nets are out of bounds as probe points. This further reinforces J10 as the only good probe option.
- **Why I'm recording it:**
  Eliminates "use a via" as a possible probe option; closes the alternative-points question.

---

## Source 9: `testingPCU.kicad_pcb` — Test-point inventory and net assignments (TP1–TP21 are all 1.5 mm SMD pads on top side; none on PWM_H/PWM_L/GND)

- **URL / path:** `testingPCU.kicad_pcb` (full file) extracted with awk
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Every test point uses footprint `TestPoint:TestPoint_Pad_D1.5mm` on layer `F.Cu` (top side). The TP-to-net mapping extracted from the PCB:
  ```
  TP1  (59.2 , 62.00 ) net 2   "Net-(D1-K…)" (D1 cathode)
  TP2  (79   , 69.25 ) net 3   "/AUX/LDO_12V"
  TP3  (93.75, 69.25 ) net 4   "/AUX/LDO_5V"
  TP4  (109.5, 62.00 ) net 5   "/AUX/LDO_3V3"
  TP5  (98.5 , 77.75 ) net 7   "/AUX/3V3"
  TP6  (83.5 , 77.75 ) net 107 "/AUX_5V"
  TP7  (68.25, 77.75 ) net 13  "/AUX/AUX_12V"
  TP8  (68.1 , 91.70 ) net 24  "/PV_RAW"
  TP9  (56.5 , 86.25 ) net 40  "/CTRL/BAT_OC_F"
  TP10 (68   , 98.60 ) net 10  "/+PV"
  TP11 (64.25,129.25 ) net 60  "/-PV"
  TP12 (81.25,124.50 ) net 16  "/BUCK/BUCK_IN"
  TP13 (106.05,124.50) net 15  "/BUCK/BUCK_OUT"
  TP14 (118  , 98.50 ) net 17  "/+BAT"
  TP15 (127.2,105.00 ) net 105 "/CTRL/BAT_UV_F"
  TP16 (127.25,108.00) net 65  "/-BAT"
  TP17 (118  , 91.70 ) net 19  "/VBAT"
  TP18 (79   ,118.50 ) net 53  "Net-(IC9-1IN+)"
  TP19 (81.75,107.25 ) net 54  "/OUTV1"
  TP20 (108  ,106.75 ) net 59  "/OUTV2"
  TP21 (107.75,118.50) net 57  "/CTRL/IN3_MINUS"
  ```
  All 21 test points are 1.5 mm diameter pads (BOM row 52). None is on GND, PWM_H, or PWM_L.
- **Proof — why this source is trustworthy here:**
  Direct extraction from PCB file. BOM cross-references the footprint.
- **Confidence: HIGH**
  Exhaustive, primary source.
- **Implication for our build:**
  Confirms there is no dedicated test point for PWM_H, PWM_L, OR a clean GND. For a ground reference the user must use a screw terminal pad (J6 pad 2 or J7 pad 2 are GND) or an EPC2152 GND pad (IC6 pin 8 or 12). The 1.5 mm test-point pads are reserved for power-rail monitoring, not signal-edge probing.
- **Why I'm recording it:**
  Documents the absence of a TP for GND alongside the absence of TPs for PWM.

---

## Source 10: EPC2152 datasheet (page 2) — confirms HSIN/LSIN pin numbering and that both inputs are referenced to GND

- **URL / path:** `https://epc-co.com/epc/Portals/0/epc/documents/datasheets/EPC2152_datasheet.pdf` page 2 (downloaded 2026-05-15)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Page 2 of the EPC2152 datasheet contains "Figure 2: EPC2152 Chip Scale Package (transparent top view)" and "EPC2152 Pinout Description" table. From the table:
  - Pin 1: V_BOOT — Floating bootstrap power supply referenced to **SW**.
  - Pin 2: V_DD — Internal power supply referenced to **GND**.
  - Pin 3: **HS_IN** — *"High-side PWM logic input referenced to GND. Internal pull-down resistor is connected between HS_IN and GND."* Pin Type: L (Logic Input).
  - Pin 4: **LS_IN** — *"Low-side PWM logic input referenced to GND. Internal pull-down resistor is connected between LS_IN and GND."* Pin Type: L.
  - Pins 5, 9: V_IN — power bus input.
  - Pins 6, 7, 10, 11: SW — output switching node.
  - Pins 8, 12: **GND** — *"Power ground. Connected to low side eGaN FET source terminal. The operating power supply, V_DD, is also referenced to GND."*
  Also from page 4, "Recommended Operating Conditions" table: HS_IN, LS_IN logic-input voltage 0 – 5 V; V_IH (high-level threshold) min = 2.4 V; V_IL (low-level threshold) max = 0.8 V.
  Page 2 "Figure 3: Functional Block Diagram" shows HS_IN and LS_IN entering Schmitt-trigger comparators that feed a single "Logic + UVLO" block. The high-side output driver is referenced to V_BOOT / SW (floating), but the **logic inputs themselves are GND-referenced**.
- **Proof — why this source is trustworthy here:**
  Official manufacturer datasheet downloaded directly from epc-co.com (URL also listed in the schematic property line 1729). Pin description table is reproduced verbatim above.
- **Confidence: HIGH**
  Primary source, exact quotes.
- **Implication for our build:**
  Two key facts that close question 7 (scope-ground safety):
  - **HSIN and LSIN are GND-referenced single-ended CMOS-style inputs**, not floating high-side inputs. The user can clip a scope ground anywhere on the GND net (J6 pin 2, J7 pin 2, IC6 pin 8 or 12, MCU IC12 pads 22/33) and capture the signals without high-side / floating-reference complications. The floating-reference concern would only apply if probing internal HS_OUT or the SW node — which Test A does NOT do.
  - **The logic inputs are 0 – 5 V CMOS with V_IH = 2.4 V** (datasheet p.4). The MCU drives them with 3.3 V CMOS — this is firmly inside the input range with healthy noise margin. The signal the user will see on the scope is a clean 0 V → 3.3 V CMOS square wave at 300 kHz, NOT a high-voltage half-bridge waveform.
  - Additional firmware confirmation (cross-reference): Test A in `src-pds/how_to_test.md` line 67-68 explicitly says "ground clip to board GND" — consistent with this finding.
- **Why I'm recording it:**
  Answers Q7 (safety / floating reference) definitively: a normal scope ground clip on board GND is correct and safe. There is no need for a high-voltage differential probe or any floating-reference workaround.

---

## Source 11: `src-pds/how_to_test.md` (Test A spec + Common assumptions) — Test A is a low-voltage CMOS PWM check with the buck input rail OFF

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\src-pds\how_to_test.md` lines 14–28 (common assumptions) and 49–99 (Test A).
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  - Lines 21–22: *"The bench DC supply, oscilloscope, multimeter, board, ESP32, and laptop all share a common ground. Never run a test with floating grounds."*
  - Lines 67–68: *"Probe 1 tip on PA12 (high-side PWM), ground clip to board GND. Probe 2 tip on PA13 (low-side PWM), ground clip to board GND."*
  - Test A appears in the priority table (line 32) as "No" under "Needs power resistors?". The buck input (BUCK_IN, J6 pad 3) is NOT energized during Test A, so the EPC2152's SW node is not switching real power.
  - However: the firmware needs the AUX_12V rail on for IC6 V_DD (pin 2). The AUX supply is generated on-board from the 3.3 V rail / charge-pump, not the bench DC supply.
- **Proof — why this source is trustworthy here:**
  Test plan written by the firmware engineer running the tests; this file is part of the project the bench tests are being run for.
- **Confidence: HIGH (for the project's own test-plan intent)**
  Primary source (in-repo test spec).
- **Implication for our build:**
  - Confirms Test A is a 0–3.3 V CMOS measurement (no high voltage).
  - Confirms the chosen ground clip strategy is "board GND" — anywhere on the GND copper plane is valid.
  - The recommended ground clip for J10-based probing is the **middle screw of J6 (J6 pad 2, PCB (75.79, 148))** because it's closest to J10 (~13 mm west) and is a large, robust screw-terminal pad.
- **Why I'm recording it:**
  Bridges the PCB-layout findings to the actual test the user is running, so the deliverable directly answers the bench question.

---

## Source 12: `testingPCU.kicad_pcb` — Silkscreen "J10" text is printed on the top layer at the connector

- **URL / path:** `testingPCU.kicad_pcb` line 2393–2402
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  ```
  2393:    (property "Reference" "J10"
  2394:        (at 0 -2.38 90)
  2395:        (layer "F.SilkS")
  ```
  The reference text "J10" is rendered on `F.SilkS` (top silkscreen) at footprint-local offset (0, -2.38), font size 1 mm, with the parent footprint at (91.225, 148.25) rotated 90°. So a "J10" label is visibly printed next to the two-pin header on the top side of the board.
- **Proof — why this source is trustworthy here:**
  Direct PCB file quoting.
- **Confidence: HIGH**
- **Implication for our build:**
  The user can find the probe header by reading silkscreen text "J10" on the top side of the board, near the bottom edge between the two big green screw terminals.
- **Why I'm recording it:**
  Confirms the silkscreen label exists so the user can locate the probe point by visual inspection rather than ruler measurement.

---

# Final answer set (consolidated)

This block consolidates the entries above into a single answer the parent
agent can paste into the test instructions. Each answer is tagged with the
log sources that back it.

**Q1 — Where is PA12 (PWM_H) physically probeable?**
- **Best:** Solder a pin or insert a probe hook into **J10 pin 1** (the SQUARE-pad hole) on the top side of the board, near the bottom edge between screw terminals J6 and J7. PCB coords (91.225, 148.25). Through-hole, 1.7 × 1.7 mm pad, 1.0 mm drill. Reference designator "J10" is printed on the top silkscreen. Net: `/PWM_H`. (Sources 1, 6, 7, 12)
- **Backup:** EPC2152 (IC6) pin 3 (HSIN), a 0.32 × 0.32 mm SMD pad on the west side of IC6, top-side, at PCB (88.532, 136.298). (Sources 5, 7, 10)
- **Worst-case:** MCU (IC12) pad 29 (PA12) on the south side of the QFN64, top-side, at PCB (120.43, 79.2). Pad 0.25 × 0.9 mm, 0.5 mm pitch — short-risk to neighbors. (Source 7)

**Q2 — Where is PA13 (PWM_L) physically probeable?**
- **Best:** **J10 pin 2** (the ROUND-pad hole, 2.54 mm to the WEST of pin 1). PCB coords (88.685, 148.25). Same size/drill as pin 1. Net: `/PWM_L`. (Sources 1, 6, 7)
- **Backup:** EPC2152 (IC6) pin 4 (LSIN), SMD pad 0.32 × 0.32 mm at PCB (88.532, 136.938) — immediately south of pin 3. (Sources 5, 7, 10)
- **Worst-case:** MCU (IC12) pad 30 (PA13) at PCB (120.93, 79.2). (Source 7)

**Q3 — Closest GND for the ground clip(s)?**
- **Best:** **J6 pin 2 (middle screw)** of the green 3-position screw terminal J6, on the bottom-left edge of the board. PCB (75.79, 148). About 13 mm west of J10. Through-hole, 1.65 × 1.65 mm pad, 1.0 mm drill. The screw clamps a wire — a scope ground-clip alligator can also grab the metal screw head once a small jumper wire is inserted, or directly clip the pad. (Source 7)
- **Alternative:** J7 pin 2 (also GND), 19.4 mm east of J10's pin 1. (Source 7)
- **Alternative on the chip:** IC6 pin 8 or 12 (GND pads, top-side at (89.737, 136.938) and (91.367, 136.938)) — very close to IC6 pins 3/4 if probing the chip directly, but 0.32 × 1.25 mm SMD pads. (Sources 5, 7, 10)
- **Test-point GND:** None — no dedicated GND test point exists. (Source 9)

**Q4 — Are EPC2152 input pins probeable?**
- Yes, on the top side at PCB (88.532, 136.298) for HSIN/pin 3 and (88.532, 136.938) for LSIN/pin 4. They are 0.32 × 0.32 mm SMD pads on the west side of IC6. Schematic-name "BUCK_HS_IN" and "BUCK_LS_IN" refer to the same nets as PWM_H and PWM_L on the top-level schematic. (Sources 1, 5, 7)
- They are accessible but small and very close together (0.64 mm center-to-center). Use a fine probe tip and a steady hand; risk of short to adjacent pins (pin 5 V_IN_1 or pin 2 V_DD) is real.

**Q5 — Is there a dedicated probe header / connector for PWM signals?**
- **Yes — J10**, a 2-pin 2.54 mm-pitch through-hole header, **explicitly wired to PWM_H (pin 1) and PWM_L (pin 2)**. (Sources 1, 6) This is the ideal probe point.
- No other connector or header carries these signals.

**Q6 — Top-side or bottom-side? Flip the board?**
- All recommended probe points are on the **top side** (`F.Cu`). J10 holes are through-hole and visible/accessible from both sides, but the probe lands on top. IC6 and IC12 pads are SMD on the top layer. (Source 7)
- The user does **not** need to flip the board for Test A.

**Q7 — Clearance / safety considerations (floating reference)?**
- The PWM_H and PWM_L nets are **GND-referenced 0–3.3 V CMOS signals**, NOT floating high-side waveforms. The EPC2152 datasheet (page 2) explicitly states the HSIN/LSIN logic inputs are referenced to GND. A normal scope ground clip on board GND is correct. (Source 10)
- During Test A the buck input (BUCK_IN screw J6 pin 3) is NOT energized — only AUX_12V (the chip's V_DD) is on. So the SW node and the half-bridge are inactive: no high-voltage transients to worry about. (Source 11)
- Project-wide assumption (line 21 of `how_to_test.md`): all instruments and the board share a common earth ground. (Source 11)
- One mild caution: J10 pin 2 (PWM_L) is at PCB X=88.685, and the closest IC6 pad is at X=88.532 — that is ~12 mm vertically away (Y=136.298 vs Y=148.25). The trace between them is relatively short, but the user's probe leads should not be allowed to short across IC6's small pads while reaching for J10. Approach J10 from south (the bottom edge of the board) rather than reaching across IC6.

**Q8 — Best probe option among the alternatives?**
- **J10 is the unambiguous best choice** for these reasons:
  1. Pads are 1.7 × 1.7 mm — five times larger than IC6's SMD pads and roughly 8× larger than IC12's QFN pads. Easy to clip with standard scope hooks.
  2. The holes are 1.0 mm drilled — accepts standard 0.025"/0.6 mm test pins, scope hook ground springs, or even a permanently-soldered male header for repeated tests.
  3. The two holes are 2.54 mm apart, matching standard female-jumper connectors and most scope-probe pin pitches.
  4. The signal path from MCU pad → trace → J10 hole is short. Capacitive loading from a probe at J10 will be virtually identical to loading at the EPC2152 input, so the captured waveform represents the actual signal seen by the chip.
  5. Probing at J10 avoids the risk of accidentally shorting two adjacent QFN pads or two adjacent EPC2152 pads.
  6. Both J10 pins are along the same row on the bottom edge of the board, leaving the rest of the top side clear for the user's hands and the screw-terminal wires for power.
- Ground-clip choice: J6 pin 2 is recommended over J7 pin 2 only because it sits to the west (same side of J10 as no other active wiring is expected during Test A; and the AUX-12V wire and bench-supply wires typically land on J7 / J5 / J4, keeping J6 freer). Either GND screw works electrically.

