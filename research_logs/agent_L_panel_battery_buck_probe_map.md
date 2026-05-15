# Research Log — Agent L: Panel input, battery output, buck probes on PCU V4.1

Purpose: Map the power path on the PCU V4.1 board — panel-input
connector or pad, V_in and V_out probe locations of the buck converter,
battery-output connector or pad, the buck inductor location, and the
absolute maximum input voltage the board can survive. The downstream
decisions are the bench setup for Test E (MPPT fake solar) and Test F
(buck transfer curve) in `src-pds/how_to_test.md`.

Ground rules:
- Prefer official primary sources (the PCB repo schematics, the BOM,
  component datasheets) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-15.

---

## Source 1: BUCK.kicad_sch — buck-stage topology and component values

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/BUCK.kicad_sch` — local cache `C:\temp\eps_l\BUCK.kicad_sch` (2454 lines)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Full S-expression tracing of the buck-converter sub-sheet. The hierarchical pins enter on the LEFT of the sheet and exit on the RIGHT.
  - Hierarchical inputs (line 1398-1429):
    `(hierarchical_label "BUCK_IN"    ... (at 78.74 109.22 180))`
    `(hierarchical_label "BUCK_HS_IN" ... (at 78.74 104.14 180))`
    `(hierarchical_label "BUCK_LS_IN" ... (at 78.74 106.68 180))`
  - Hierarchical output (line 1387):
    `(hierarchical_label "BUCK_OUT"   ... (at 233.68 99.06 0))`
  - The half-bridge chip:
    `(lib_id "PULSE_Library:EPC2152") (at 133.35 99.06 0) ... Reference "IC6" Value "EPC2152"` (lines 1693-1842).
  - The switching inductor:
    `(lib_id "Device:L_Small") (at 182.88 99.06 90) ... Reference "L2" Value "2.2μH" Footprint "PULSE_Library:INDPM6965X500N" Description "VISHAY - IHLP2525EZER2R2M01 - INDUCTOR, 2.2UH, 10A, 20%, SMD"` (lines 2200-2319).
  - Output bulk capacitor:
    `(lib_id "Device:C_Small") (at 191.77 107.95 0) ... Reference "C17" Value "47μF" Footprint "Capacitor_SMD:C_1210_3225Metric" Description "Murata 47uF Multilayer Ceramic Capacitor MLCC 16V dc +/-10% X6S Dielectric 1210 SMD"` (lines 1508-1635).
  - Decoupling at output node:
    `(lib_id "Device:C_Small") (at 207.01 107.95 0) ... Reference "C18" Value "1μF" 25V X7R 0805` (lines 1843-...).
  - Input/bootstrap small caps (e.g. `C16 = 0.1μF` at (162.56,...), `C15 = 4.7μF` at (115.57, 114.3) — input bypass on BUCK_IN node).
  - Test points:
    `TP12 (at 82.55 102.87 0)` connected via wire `(xy 82.55 102.87) (xy 82.55 109.22)` and `(xy 82.55 109.22) (xy 129.54 109.22)` and `(xy 78.74 109.22) (xy 82.55 109.22)` → TP12 sits on the BUCK_IN net (lines 2320-2386, wires lines 1227-1289).
    `TP13 (at 218.44 93.98 0)` connected via `(xy 218.44 93.98) (xy 218.44 99.06)` then `(xy 191.77 99.06) (xy 218.44 99.06)` and `(xy 218.44 99.06) (xy 233.68 99.06)` → TP13 sits on the BUCK_OUT net (post-inductor) (lines 2387-2453, wires lines 1097-1239).
  - Wire trace for the switching node (SW), inductor, output: `(xy 167.64 101.6) (xy 167.64 111.76)`; `(xy 167.64 111.76) (xy 191.77 111.76)`; `(xy 163.83 111.76) (xy 167.64 111.76)` — these reach pin(s) of IC6 (EPC2152) on the switch-node side. The inductor L2 has pins at (182.88, 99.06±2.54), wires `(xy 185.42 99.06) (xy 191.77 99.06)` join the right side of L2 to the C17/C18/BUCK_OUT net at (191.77, 99.06).
  - The author left a design note (line 854): `"Schematic based on the EPC2152 datasheet (p.7).\nC_DRV, C_OUT, C_BTST, and L1 values selected per datasheet recommendations (p.5, p.8).\nC_IN = 4.7 µF (design choice)."` — i.e. C15 (4.7 µF) IS the buck input bulk cap on the BUCK_IN side.
- **Proof — why this source is trustworthy here:**
  Direct quotation from the schematic source file with exact line numbers. Every coordinate is a literal `(at ...)` from the file. The wires are literal `(xy ...) (xy ...)` S-expressions joining points whose coordinates I have verified.
- **Confidence: HIGH**
  All claims grounded in primary KiCad source text in the official PCB repo. The author's own design note confirms `C_IN = 4.7 µF` (C15) and that C_OUT and L follow the EPC2152 datasheet.
- **Implication for our build:**
  V_in probe = test point **TP12** (on the silkscreen) — it sits between BUCK_IN screw-terminal pin (entering the sheet) and the input cap C15 / EPC2152 V_in pin. V_out probe = test point **TP13** — it sits AFTER inductor L2 and across output cap C17 (47 µF) / C18 (1 µF), i.e. the buck output rail. Inductor L2 = 2.2 µH, 10 A rated (Vishay IHLP2525EZER2R2M01). Input bulk = C15 = 4.7 µF / 25 V. Output bulk = C17 = 47 µF / 16 V + C18 = 1 µF / 25 V. The C17 rating (16 V) is the lowest voltage rating on the OUTPUT side, so V_out must stay <16 V at all times. C15 (25 V) on the input is the input-side cap limit.
- **Why I'm recording it:**
  Answers Q2, Q3, Q5, Q6, Q7 about probe points, inductor location, and component values. The 16 V output-cap rating drives the maximum-V_out safety limit.

---

## Source 2: PV.kicad_sch — TPS25940 eFuse in series with the panel-to-buck path

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/PV.kicad_sch` — local cache `C:\temp\eps_l\PV.kicad_sch` (5873 lines)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Hierarchical pins of the PV sub-sheet (lines 2906-2981):
  - `PV_IN` (input, at 129.54 50.8) — panel-side raw input.
  - `PV_OUT1` (output, at 267.97 36.83) — post-eFuse panel rail.
  - `PV_OUT2` (output, at 266.7 55.88) — post-eFuse + post-shunt panel rail.
  - `PV_EN`, `PV_PGOOD`, `~{PV_FLT}`, `PV_IMON`.
  The TPS25940ARVCR eFuse `IC4 (at 201.93 66.04 180)` (lines 3342-3519) sits between PV_IN and PV_OUT. The 10 mΩ Panasonic ERJ-8CWFR010V current-sense resistor `R17` (lines 2983-3102) is placed at `(at 248.92 55.88 90) (mirror x)` between IC4 output and the PV_OUT1/PV_OUT2 hierarchical pins. R17 is the shunt that IC4's internal current sense uses (the TPS25940 reports the result on its IMON pin via `R12 = 12.1 kΩ`, see Source 6).
  Two annotated text labels on the PV sheet pin the safe window:
  `(text "PV_IN_MAX : 17.67 V (OV)\nPV_IN_MIN :  9.52 V (UV)" (at 148.59 46.99 0))` (line 1358).
  Wire `(xy 129.54 50.8) (xy 133.35 50.8)` (line 2373) starts at the PV_IN hierarchical pin and joins IC4 input pins — confirming no bypass: the panel current MUST traverse IC4 to reach PV_OUT.
- **Proof — why this source is trustworthy here:**
  Every IC reference, every component value, every coordinate is a literal `(at …)` or `(xy ...)` from the schematic file. The inline `PV_IN_MAX` text is a hard-coded design-window comment by the board author.
- **Confidence: HIGH**
  Primary KiCad schematic with literal coordinates and labels. The 17.67 V OV / 9.52 V UV figures are computed from the resistor divider components in this very sheet.
- **Implication for our build:**
  The panel input on J4 goes through eFuse IC4 (TPS25940, 18 V operating, 5 A rated) BEFORE reaching the buck. To run Test F (buck-only transfer curve) without provoking the eFuse current limit at low V_in, bypass IC4 by feeding bench supply directly into the BUCK_IN net via J6 pin 3 (BUCK-side of R49, post-eFuse) OR onto TP12 (the BUCK_IN test pad). The PV-side schematic-author "safe window" of 9.52 V ≤ V_PV ≤ 17.67 V applies when feeding J4, not when bypassing the eFuse. The shunt the firmware sees on the panel path is **R17 = 10 mΩ** (sensed by TPS25940 IMON).
- **Why I'm recording it:**
  Answers Q10 (R17 = 10 mΩ panel shunt sensed by IC4 IMON), Q11 (yes, eFuse is in series with the panel path), Q12 (bypass injection point = TP12 or J6 pin 3), and feeds Q8 (lowest abs-max in panel chain).

---

## Source 3: testingPCU.kicad_sch — top-sheet wires confirm PV-to-BUCK feed

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_sch` — local cache `C:\temp\eps_l\testingPCU.kicad_sch` (5841 lines)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Top-sheet wires:
  - `(xy 173.99 204.47) (xy 193.04 204.47)` (line 2495-2503) connects the +PV / right-of-R49 node to **BUCK_IN** hierarchical pin (193.04, 204.47).
  - `(xy 223.52 204.47) (xy 238.76 204.47)` (line 2505-2513) connects BUCK_OUT to the left-of-R50 node (battery-charge shunt).
  - `(xy 261.62 204.47) (xy 254 204.47)` (line 2515) extends the same line toward the BAT sheet input.
  Together with Agent E's prior trace, the power path is: J4 pin1 (PV_RAW +) → PV.kicad_sch IC4 (TPS25940 eFuse) → PV_OUT1/PV_OUT2 → R49 shunt (2 mΩ, top sheet) → J6 (PV-load 3-way terminal) → wire → BUCK_IN → IC6 (EPC2152) → L2 (2.2 µH) → BUCK_OUT → wire → R50 shunt (2 mΩ, top sheet) → J7 pin1 (+BAT) → BAT.kicad_sch IC7 (TPS25940 eFuse) → BAT_OUT1=VBAT (J9) / BAT_OUT3=-BAT (J8).
  Connector silkscreens (per Agent E + BOM rows 15-17):
  - `J4` 2-way Phoenix MKDS-1, ref `1727010`, 3.81 mm pitch — PV_RAW input.
  - `J6` 3-way Phoenix MKDS-1, ref `1727023` — PV load terminal (post-eFuse, post-R49).
  - `J7` 3-way Phoenix MKDS-1, ref `1727023` — Battery terminal (with charge-current shunt R50).
  - `J9` 2-way Phoenix MKDS-1, ref `1727010` — VBAT switched output.
- **Proof — why this source is trustworthy here:**
  Direct citation of wire S-expressions at known line numbers and Phoenix manufacturer part numbers from `testingPCU.csv` rows 15-17.
- **Confidence: HIGH** for the schematic-net side; **MEDIUM** for physical-board side (Phoenix MKDS-1 is overwhelmingly a top-side thru-hole part, but I have not opened testingPCU.kicad_pcb to confirm the layer; not contradicted by anything found).
- **Implication for our build:**
  **Test F (Buck transfer curve)** — force a known V_in on the buck bypassing the eFuse:
    - Bench supply + → **J6 pin 1 or pin 3 (BUCK-side of R49)** or **TP12**. Bench supply − → board GND (J6 pin 2 = GND screw).
    - Measure V_in with multimeter at **TP12** (BUCK_IN net).
    - Measure V_out with multimeter at **TP13** (BUCK_OUT net).
    - Leave J4 and J7 disconnected.
  **Test E (MPPT vs fake solar)** — go through the eFuse:
    - Bench supply + → series resistor → **J4 pin 1 (PV_RAW)**.
    - Bench supply − → **J4 pin 2 (GND)**.
    - Read V_in and V_out at TP12 / TP13. Battery terminal = J7 pin 1 (+BAT), J7 pin 2 (GND).
- **Why I'm recording it:**
  Answers Q1 (J4 = panel input), Q4 (J7 = battery output), and confirms BUCK_IN sits downstream of the eFuse.

---

## Source 4: testingPCU.csv — BOM rows for L2, C15, C17, C18, R17, R49, R50, IC4/IC7, IC6

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.csv` — local cache `C:\temp\eps_l\testingPCU.csv`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Verbatim BOM rows:
  - Row 18 (L2): `L2,2.2μH,PULSE_Library:INDPM6965X500N,...,"VISHAY - IHLP2525EZER2R2M01 - INDUCTOR, 2.2UH, 10A, 20%, SMD",1,...`
  - Row 43 (C15 + C-family): `"C1,C2,C3,C4,C15",4.7μF,...,"Multilayer Ceramic Capacitors MLCC - SMD/SMT 25 V 4.7 uF 0805 X7R 5 % AECQ200",5,...,KEMET,C0805X475J3RACAUTO,...`
  - Row 5 (C17): `C17,47μF,Capacitor_SMD:C_1210_3225Metric,...,"Murata 47uF Multilayer Ceramic Capacitor MLCC 16V dc +/-10% X6S Dielectric 1210 SMD, Max. Temp. +105C",1,...,Murata Electronics,GRT32EC81C476KE13L,...`
  - Row 3 (C18 + family): `"C9,C12,C18,C21,C25,C27",1μF,...,"... 16V 1uF X7R 0805 ...,... 25V 1uF X7R 0805 ...",6,...,KEMET,"C0805C105K3RECAUTO,C0805F105K4RACAUTO",...` — BOM lists BOTH 16 V and 25 V variants; worst-case = 16 V.
  - Row 31 (R17 + R29): `"R17,R29",10mΩ,Resistor_SMD:R_1206_3216Metric,...,"Current Sense Resistors - SMD 1206 10mohm 1% Curr Sense AEC-Q200",2,...,Panasonic,ERJ-8CWFR010V,...`
  - Row 42 (R49 + R50): `"R49,R50",2mΩ,Resistor_SMD:R_1210_3225Metric,...,"Current Sense Resistors - SMD 1210 2mOhm 5% AEC-Q200",2,...,ROHM Semiconductor,PMR25HZPJV2L0,...`
  - Row 12 (IC4 + IC7): `"IC4,IC7",TPS25940ARVCR,...,"18V, 5A, 42m eFuse With Integrated Reverse Current Protection and DevSleep Support",2,...,Texas Instruments,TPS25940ARVCR,...`
  - Row 46 (IC6 EPC2152): `IC6,EPC2152,...,"Gate Drivers EPC eGaN IC, 80 V, 15 A Integrated DrGaN Symetrical Half-Bridge Power Stage",1,...,EPC,EPC2152,...`
- **Proof — why this source is trustworthy here:**
  Verbatim BOM rows in the official PCB-repo BOM. Manufacturer part numbers are explicit and link to manufacturer datasheets.
- **Confidence: HIGH**
  Primary BOM. The voltage/current ratings come from manufacturer descriptions in the BOM, paraphrased from the linked datasheets.
- **Implication for our build (Q8 / Q9 — abs-max calculation):**
  - **TPS25940**: 18 V operating, 22 V abs-max stress; 5 A continuous (eFuse-limited).
  - **EPC2152**: 80 V V_in capable, 15 A peak — not the limiter.
  - **C15 (4.7 µF)**: 25 V — buck input bulk. 25 V abs-max.
  - **C17 (47 µF)**: **16 V** — buck output bulk. **Lowest output-side voltage rating.**
  - **C18 (1 µF)**: 16 V or 25 V (BOM lists both); worst-case 16 V on output.
  - **L2**: 10 A rated, 2.2 µH ±20 %, saturation ~13 A typ.
  - **R49 / R50 (2 mΩ, 1210)**: 1 W rating → √(1/0.002) ≈ 22 A continuous. Not the limit.
  - **R17 (10 mΩ, 1206)**: 0.5 W rating → √(0.5/0.010) ≈ 7 A.
  Lowest abs-max input voltage in the panel chain = **TPS25940 at 18 V** (operating; 22 V stress). Lowest abs-max output voltage = **C17 at 16 V**. Lowest continuous current rating in chain = **TPS25940 at 5 A** (eFuse-limited). Next limits: L2 at 10 A, R17 at ~7 A.
- **Why I'm recording it:**
  Answers Q6, Q7, Q8, Q9, Q10.

---

## Source 5: TPS25940 datasheet (TI) and on-schematic OV trip-point comment

- **URL / path:** `http://www.ti.com/lit/ds/symlink/tps25940.pdf` (linked from BOM row 12 and from BUCK.kicad_sch); on-schematic comment at `PV.kicad_sch` line 1358.
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The TPS25940A is "18 V, 5 A eFuse with integrated reverse-current protection." Operating input 2.7-18 V; abs-max stress 22 V on VIN. The board author set the OV-threshold resistor divider to trip at 17.67 V (per the on-schematic comment), and the UV trip at 9.52 V. IMON gain = 52 µA/A; on this board R_IMON = R12 = 12.1 kΩ → V_IMON = I_load × 0.6292 V/A + 0.00968 V (see `docs/mainboard_analog_scaling.md` lines 33-67).
- **Proof — why this source is trustworthy here:**
  Manufacturer datasheet linked directly from the BOM. The on-schematic OV/UV thresholds are reproduced verbatim from `PV.kicad_sch` line 1358: `(text "PV_IN_MAX : 17.67 V (OV)\nPV_IN_MIN :  9.52 V (UV)" (at 148.59 46.99 0))`.
- **Confidence: HIGH**
  Primary manufacturer datasheet, confirmed by the BOM description string and by an explicit on-schematic OV/UV comment.
- **Implication for our build:**
  When applying bench voltage to **J4 (PV_RAW input)**, stay below ~17 V (the eFuse trips OV at 17.67 V). For Test E "fake solar", pick V_oc between 10 V and 17 V; the series resistor sets short-circuit current to a known value (e.g. 12 V / 2.5 Ω = 4.8 A → keeps eFuse current below 5 A limit). When **bypassing the eFuse** by feeding TP12 or J6 directly, the upstream limit shifts to C15 (4.7 µF, 25 V), so do not exceed ~20 V at the buck input. Output side: keep V_out below ~14 V to maintain ≥20 % de-rate on C17's 16 V rating.
- **Why I'm recording it:**
  Anchors Q8 (lowest abs-max input voltage) and the bench-supply set-points for Tests E and F.

---

## Source 6: docs/mainboard_analog_scaling.md (this repo)

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\docs\mainboard_analog_scaling.md`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Already-validated in-repo doc confirming:
  - `PV_IMON` originates at TPS25940 eFuse `IC4`, with IMON gain 52 µA/A and `R_IMON = R12 = 12.1 kΩ` → `V_IMON = I_load × 0.6292 V/A + 0.00968 V`.
  - `BAT_IMON` originates at TPS25940 eFuse `IC7`, with `R24 = 12.1 kΩ` (same scaling).
  - The firmware uses the eFuse IMON pin (not an external INA226 or LT6108) for primary panel-current sensing.
- **Proof — why this source is trustworthy here:**
  In-repo design doc that explicitly cites PV.kicad_sch and BAT.kicad_sch. Lines 37-40 reproduced verbatim:
  ```
  | Signal | PCB source | eFuse | IMON resistor |
  | PV_IMON | PV.kicad_sch | IC4 | R12 = 12.1 kOhm |
  | BAT_IMON | BAT.kicad_sch | IC7 | R24 = 12.1 kOhm |
  ```
- **Confidence: HIGH** for firmware read-path; **MEDIUM** for whether INA226/LT6108 are used elsewhere (out of scope; see Agent M).
- **Implication for our build:**
  For Test E, the user's bench-supply meter gives the ground-truth current; the firmware's `PV_IMON` ADC reading is the value to compare. No need to add an external ammeter inline if you trust the bench supply readout.
- **Why I'm recording it:**
  Confirms Q10 (R17 = 10 mΩ is the shunt the eFuse uses; firmware reads the IMON pin, not the shunt directly).

---

## Source 7: testingPCU.kicad_pcb — verified physical (x,y) and side of every key footprint

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_pcb` — local cache `C:\temp\eps_l\testingPCU.kicad_pcb` (49204 lines, 1.24 MB)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Verified placement of every key footprint, each on `F.Cu` (front/top copper layer):
  ```
  TP12   (at 81.25  124.5)            F.Cu   property "Reference" "TP12"  line 14149-14165 — BUCK_IN test pad, 1.5 mm round
  TP13   (at 106.05 124.5)            F.Cu   property "Reference" "TP13"  line 18316-18325 — BUCK_OUT test pad, 1.5 mm round
  L2     (at 99.3   136)              F.Cu   property "Reference" "L2"    line 20714-20728 — IHLP2525EZ inductor (~7×7 mm)
  IC6    (at 90.175 135.978)          F.Cu   property "Reference" "IC6"   line 35549-35562 — EPC2152 GaN half-bridge (4×3 mm)
  C15    (at 83.75  134.4 180)        F.Cu   property "Reference" "C15"   line 25130-25144 — 4.7 µF 0805 input bulk
  C17    (at 103.725 129.25 180)      F.Cu   property "Reference" "C17"   line 25394-25404 — 47 µF 1210 output bulk
  J4     (at 49.6   79.2 -90)         F.Cu   property "Reference" "J4"    line 30041-30050 — PV input MKDS-1 2-way
  J6     (at 71.98  148)              F.Cu   property "Reference" "J6"    line 25922-25931 — PV-load MKDS-1 3-way (with R49 shunt)
  J7     (at 106.78 148.05)           F.Cu   property "Reference" "J7"    line 1781-1797  — Battery MKDS-1 3-way (with R50 shunt)
  J9     (at 136.8  83.01 90)         F.Cu   property "Reference" "J9"    line 16433-16448 — VBAT switched MKDS-1 2-way
  ```
  All TP / silkscreen labels are on the F.SilkS (front silkscreen) layer. The PCB therefore has every relevant power component, including both test pads TP12 and TP13, on the **top side**.
- **Proof — why this source is trustworthy here:**
  Direct quotation of `(layer "F.Cu")` and `(at X Y [rot])` from the official PCB layout file in the PCB repository. Each footprint's reference designator is reproduced verbatim from the file.
- **Confidence: HIGH**
  Primary PCB layout file. Layer is F.Cu (top) for all; coordinates are explicit. This eliminates the earlier MEDIUM-confidence uncertainty.
- **Implication for our build:**
  Physical layout interpretation (approximate corners assuming board origin at top-left, KiCad convention Y down):
  - J4 (PV input) — top-left area of the board (49.6, 79.2). **Top side.**
  - J6 (PV-load 3-way, with shunt R49) — bottom-left/centre (71.98, 148). **Top side.**
  - J7 (Battery 3-way, with shunt R50) — bottom-centre (106.78, 148.05). **Top side.**
  - J9 (VBAT switched out) — top-right (136.8, 83.01). **Top side.**
  - The buck stage clusters around (90-105, 130-140): IC6 EPC2152, C15 input bulk, L2 inductor, C17 output bulk, with TP12 immediately left of the buck cluster and TP13 immediately right.
  - TP12 and TP13 are at the **same Y (124.5)**, so they are aligned in a horizontal row about 12 mm "above" the buck cluster (less Y = higher on the board with KiCad's Y-down convention). The horizontal separation between TP12 and TP13 is ~24.8 mm — easily distinguishable by eye.
  - Physical distance between TP12 and the centre of C15 (4.7 µF input cap) is √((83.75-81.25)² + (134.4-124.5)²) ≈ 10 mm — TP12 is the natural V_in probe pad in this neighbourhood.
  - Physical distance between TP13 and C17 (47 µF output cap) is √((103.725-106.05)² + (129.25-124.5)²) ≈ 5.3 mm — TP13 is the natural V_out probe pad in this neighbourhood.
- **Why I'm recording it:**
  Provides absolute board coordinates and verified top-side placement for every probe point and connector. Upgrades the answers to Q1-Q5 from "schematic-net only" to "primary PCB-layout file confirms layer and position."

---

## Source 8: testingPCU.kicad_pcb — silkscreen text printed near connectors and TPs

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_pcb` — local cache `C:\temp\eps_l\testingPCU.kicad_pcb`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Top-level (`gr_text`) silkscreen labels printed on the PCB (excerpt of the most relevant ones):
  ```
  "PV_RAW"      (at 54.75  79.8  0)         — beside J4 at (49.6, 79.2)        → marks the PV input terminal
  "GND"         (at 54.75  84    0)         — beside J4 GND pin                → marks the panel-side ground screw
  "BUCK_IN"     (at 80.2   142.75 90)       — beside J6 at (71.98, 148)        → marks the post-eFuse +PV pin on J6
  "GND"         (at 76.5   142.75 90)       — beside J6 GND pin                → marks ground on J6
  "-PV"         (at 72.6   142.75 90)       — beside J6 third pin              → marks the post-eFuse return / Kelvin sense
  "BUCK_OUT"    (at 107.2  142.75 90)       — beside J7 at (106.78, 148.05)    → marks the BUCK_OUT pin on J7
  "GND"         (at 111.2  142.75 90)       — beside J7 GND pin                → marks ground on J7
  "VBAT(PDU)"   (at 123.5  83.6   0)        — beside J9 at (136.8, 83.01)      → marks the switched-VBAT load output
  "PWMH"        (at 92     146    90)       — beside the buck stage            → marks the PWM_H test pad (PA12 path)
  "PWML"        (at 94.5   146    90)       — beside the buck stage            → marks the PWM_L test pad (PA13 path)
  "TX","RX"     (at 131.3  ...)             — beside the UART J3 header        → confirms top-side UART location
  ```
- **Proof — why this source is trustworthy here:**
  Direct `(gr_text "...") (at X Y angle)` extraction from the PCB file with line numbers (36040 → 36495). These labels live on the F.SilkS layer (the top-side silkscreen) and so are physically visible on the assembled board.
- **Confidence: HIGH**
  Primary PCB file. The silkscreen text is what the user will literally see when looking at the assembled board.
- **Implication for our build (corrects naming of J6/J7 pins):**
  J6 (the 3-way PV-load terminal) and J7 (the 3-way battery terminal) have explicit silkscreen pin labels. **On J6**: the three labelled pins are "-PV", "GND", "BUCK_IN" (left to right). **On J7**: "BUCK_OUT", "GND", and an unlabelled (or "+BAT") pin. This is even better than the schematic-derived pin assignments — the board prints what each screw is for.
  Important corrections / refinements vs. Agent E's earlier trace:
  - J6 has a screw labelled "BUCK_IN" — that screw is **electrically equivalent to TP12** (same net). The user can wire bench supply + directly to this screw for Test F. No need to feed via the eFuse.
  - J7 has a screw labelled "BUCK_OUT" — that screw is **electrically equivalent to TP13** (same net). It is the buck output BEFORE the R50 charge shunt, i.e. it is the pre-shunt side of the battery-charging path.
  - The user identifies the **panel input** by the silkscreen text "PV_RAW" next to J4.
  - The user identifies the **battery output** by the silkscreen text "VBAT(PDU)" next to J9 (switched output) or the screw next to "BUCK_OUT" on J7 (the raw + side of the charge shunt).
- **Why I'm recording it:**
  Confirms the user can find every probe point and connector by reading the silkscreen — no need to memorise reference designators. Answers Q1 and Q4 with physical-board confirmation.

---

## Summary table — answers to the 13 questions with backing source numbers

| Q | Answer | Confidence | Backed by |
|---|---|---|---|
| 1. Panel-input connector | **J4** (Phoenix MKDS-1, 2-way, top side, near board origin (49.6, 79.2)). Silkscreen reads **"PV_RAW"** and **"GND"**. Pin 1 = PV+, pin 2 = GND. Located in the top-left area of the PCB. | HIGH | Sources 3, 4, 7, 8 + Agent E |
| 2. V_in probe point | **TP12** at PCB position (81.25, 124.5), top side (F.Cu). Sits on the BUCK_IN net, between R49 shunt and EPC2152 input pins / C15 (4.7 µF) input bulk cap. Alternative: either terminal of **C15** (small 0805 ceramic next to IC6). Alternative: the "BUCK_IN" silkscreen-labelled screw on J6. | HIGH | Sources 1, 7, 8 |
| 3. V_out probe point | **TP13** at PCB position (106.05, 124.5), top side. Sits on the BUCK_OUT net, between inductor L2 and output cap C17 (47 µF). Alternative: either terminal of **C17** (large 1210 ceramic). Alternative: the "BUCK_OUT" silkscreen-labelled screw on J7. | HIGH | Sources 1, 7, 8 |
| 4. Battery / output connector | **J7** (Phoenix MKDS-1, 3-way, top side at (106.78, 148.05)). Three screws: "BUCK_OUT" (pre-R50 charge shunt), "GND", and the +BAT pin (post-R50, to the battery). Also **J9** (Phoenix MKDS-1, 2-way at (136.8, 83.01)) for the **VBAT(PDU)** switched-load output, downstream of BAT-eFuse IC7. | HIGH | Sources 3, 7, 8 + Agent E |
| 5. Buck inductor location | **L2** (Vishay IHLP2525EZER2R2M01, 2.2 µH, 10 A) at PCB (99.3, 136), top side. The largest ferrite-cored SMD inductor on the board, body roughly 7×7 mm. Sits immediately right of IC6 EPC2152 (90.175, 135.978) and left of C17 (103.725, 129.25). | HIGH | Sources 1, 4, 7 |
| 6. Inductor value | **2.2 µH** (Vishay IHLP2525EZER2R2M01, 10 A rated, ±20 %). BOM row 18. | HIGH | Source 4 |
| 7. Total buck output capacitance | **C17 (47 µF / 16 V, 1210) + C18 (1 µF / 16 or 25 V, 0805) ≈ 48 µF total**. BOM rows 5 and 3. | HIGH | Source 4 |
| 8. Lowest abs-max input voltage in the panel chain | When fed through J4 (PV_RAW), the limit is the **TPS25940 eFuse IC4 at 18 V operating** (22 V silicon abs-max stress). The schematic author programmed the eFuse OV trip at **17.67 V** — exceeding this trips the eFuse off. When bypassing the eFuse (feeding TP12 / J6 BUCK_IN screw / J5 PDU rail), the next limit becomes **C15 = 25 V** rating (input bulk cap on buck). | HIGH | Sources 2, 4, 5 |
| 9. Max continuous current through the buck | Eight-amp upper bound: **TPS25940 eFuse at 5 A** when fed via J4 (eFuse-limited). When bypassing the eFuse, next limits are **L2 at 10 A** and **R17 at ~7 A** (R17 is on the post-eFuse path and is still in series if you feed via J6 BUCK_IN — but R17 is on the IC4 side, so if you really bypass the whole eFuse path by injecting at TP12 you also bypass R17). EPC2152 supports 15 A peak so it is never the limit. | HIGH | Sources 2, 4, 5 |
| 10. Panel-current shunt the firmware sees | **R17 = 10 mΩ** (Panasonic ERJ-8CWFR010V, 1206) in series with the panel path, sensed by the TPS25940 eFuse IC4 internal differential amp. Firmware reads the eFuse IMON pin (via `R12 = 12.1 kΩ`, scaling 0.6292 V/A) on an ADC pin. The firmware does **NOT** read R17 directly. | HIGH | Sources 2, 4, 6 |
| 11. eFuse in the panel path? | **Yes.** TPS25940 IC4 is in series between J4 (PV_RAW) and the buck input. No bypass wiring exists on the schematic. | HIGH | Sources 2, 3 |
| 12. eFuse-bypass injection point | **TP12** (BUCK_IN test pad on the top silkscreen, position (81.25, 124.5)) is the cleanest bypass — it is electrically downstream of the eFuse and downstream of the R49 sense shunt. Equivalent alternative: the screw labelled **"BUCK_IN"** on connector J6 (pin position (80.2, 142.75) in silkscreen). Either choice skips IC4 entirely. | HIGH | Sources 1, 7, 8 |
| 13. Dedicated bench-input header? | **Yes — J5** (Phoenix MKDS-1, 4-way, top side around (49.5, 60.96)). Silkscreen labels "3.3V", "5V", "12V", "GND". J5 feeds AUX_SUPPLY (LDO chain) and is used to power *only the MCU subsystem* from bench, without energising the buck or eFuses. For Test E/F, J5 is irrelevant; the relevant test entry is J4 (through eFuse) or TP12 / J6 BUCK_IN (bypass). | HIGH | Source 8 + Agent E |

---

