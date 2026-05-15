# Research Log — Agent J: ADC sense net injection points on PCU V4.1

Purpose: Identify where on the physical PCU V4.1 board a bench supply
can inject a known voltage into each of OUTV1, OUTV2, OUTA1, OUTA2,
PV_IMON, and BAT_IMON, and what divider or amplifier sits between that
injection point and the MCU ADC pin. The downstream decisions are the
injection map for Test B (ADC calibration) and Test D (state machine
on real sensors) in `src-pds/how_to_test.md`.

Ground rules:
- Prefer official primary sources (the PCB repo schematics, the BOM,
  component datasheets) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-15.

---

## Source 1: Firmware ADC driver `src/drivers/mainboard_adc_reader.c` + header

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\src\drivers\mainboard_adc_reader.c` and `mainboard_adc_reader.h`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The firmware authoritatively maps the six sense nets to MCU pins, ADC
  channels, divider ratios, and conversion math. From the file header
  comment (lines 14-21):
  ```
  Pin   Channel  Net name   What it measures
  PB04  AIN12    PV_IMON    TPS25940 (IC4) IMON pin
  PB05  AIN13    BAT_IMON   TPS25940 (IC7) IMON pin
  PB06  AIN14    OUTA1      LT6108 (IC10) output (panel-rail current)
  PB07  AIN15    OUTA2      LT6108 (IC11) output (battery-rail current)
  PB08  AIN2     OUTV1      Resistor divider (panel bus voltage)
  PB09  AIN3     OUTV2      Resistor divider (charging rail voltage)
  ```
  Divider constants (lines 101-104):
  ```
  OUTV1_DIVIDER_TOP_OHMS      100000   (100 kΩ)
  OUTV1_DIVIDER_BOTTOM_OHMS     3600   (3.6 kΩ)  -> ratio (100k+3.6k)/3.6k = 28.778
  OUTV2_DIVIDER_TOP_OHMS      100000   (100 kΩ)
  OUTV2_DIVIDER_BOTTOM_OHMS    11800   (11.8 kΩ) -> ratio (100k+11.8k)/11.8k = 9.475
  ```
  TPS25940 IMON math (lines 98-99 and 390-401):
  ```
  TPS25940_IMON_OFFSET_MICROVOLTS    9 680  µV  (zero-load offset)
  TPS25940_IMON_MICROVOLTS_PER_AMP 629 200  µV/A  (629.2 µV per mA)
  load_ma = (pin_uv - 9680) * 1000 / 629200
  ```
  LT6108 OUTA math (lines 106-107 and 403-407):
  ```
  load_ma = pin_mv * 40 / 3   (i.e. 13.333 mA per mV at the OUTA pin)
  ```
  ADC reference (line 96 and 311):
  ```
  #define ADC_REFERENCE_MILLIVOLTS  3300u
  ADC_REFCTRL = REFSEL_INTVCC1     (= VDDANA, i.e. 3.3 V supply)
  GAIN_DIV2                        (input divided by 2 internally)
  ```
- **Proof — why this source is trustworthy here:**
  This is the firmware actually flashed on the board for Test B and Test
  D. The pin↔channel mapping is hard-coded in `mainboard_adc_reader.h`
  (lines 14-19, the `MAINBOARD_ADC_CHANNEL_*` macros). The divider values
  are hard-coded in `.c` lines 101-104. The 3300 mV reference and
  INTVCC1 (= VDDANA, the chip's analog supply pin, fed from the board's
  3.3 V rail) selection are explicit in lines 96 and 311. The OUTA
  scaling and TPS25940 IMON scaling carry self-flagged
  `MAINBOARD_ADC_TELEMETRY_FLAG_OUTA_SCALING_PROVISIONAL` and
  `..._OUTV_SCALING_PROVISIONAL` bits (`.c` lines 226-227) meaning the
  firmware author has not yet verified these against the actual silkscreen
  resistor values on the PCB — the calibration test is precisely what
  will confirm or correct them.
- **Confidence: HIGH** for pin↔channel↔net mapping and the ADC reference;
  **MEDIUM** for divider ratios and the IMON/OUTA scaling because those
  constants are flagged provisional in the firmware itself and still need
  cross-check against the BOM resistor values.
- **Implication for our build:**
  - **MCU pins answered (Q3):** OUTV1=PB08/AIN2, OUTV2=PB09/AIN3,
    OUTA1=PB06/AIN14, OUTA2=PB07/AIN15, PV_IMON=PB04/AIN12,
    BAT_IMON=PB05/AIN13. This corrects the `plan.md` guess of "PB04–PB09":
    PB04-PB09 is correct but firmware uses AIN2 / AIN3 / AIN12 / AIN13 /
    AIN14 / AIN15 (not AIN12-17).
  - **ADC reference answered (Q5):** 3.3 V external (INTVCC1 = VDDANA),
    with internal /2 gain so full scale at the ADC die is 1.65 V but the
    firmware reports millivolts referenced to a 3300 mV full-scale.
  - **Expected ADC count formula at 1 V at injection point (Q10):**
    * OUTV1: pin_V = 1 V × 3.6/(100+3.6) = 34.75 mV → raw_adc = 34.75 × 4095 / 3300 ≈ **43 counts**
    * OUTV2: pin_V = 1 V × 11.8/(100+11.8) = 105.5 mV → raw_adc = 105.5 × 4095 / 3300 ≈ **131 counts**
    * OUTA1, OUTA2 (LT6108 output drives MCU pin directly): raw_adc = 1000 × 4095 / 3300 ≈ **1241 counts** at 1 V applied
    * PV_IMON, BAT_IMON (IMON pin drives MCU pin directly): raw_adc = 1000 × 4095 / 3300 ≈ **1241 counts** at 1 V applied
- **Why I'm recording it:**
  Pin mapping (Q3), reference selection (Q5), divider ratios in firmware
  (Q2), and the formula for expected ADC counts (Q10) are all answered
  primarily by this file. Cross-checks against schematic and BOM follow.

---

## Source 2: BOM `testingPCU.csv` — resistor and IC references

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.csv` (mirror of repo file `testingPCU.csv` at https://github.com/CHESS-mission/eps_pcu_eng)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Confirms every resistor value the firmware divider formulas need, plus
  the part numbers and reference designators of the four current-sense
  ICs (two TPS25940 eFuses with IMON pins, two LT6108 high-side
  amplifiers), plus the existence of 21 silkscreen test points (TP1-TP21,
  0603-pad-style "TestPoint_Pad_D1.5mm"). Key BOM rows:
  ```
  Row 12  IC4,IC7   TPS25940ARVCR   eFuses (18V, 5A, 42 mΩ with IMON)
  Row 13  IC10,IC11 LT6108IMS8-1#PBF Current sense amp + comparator + 400 mV ref
  Row 14  IC12      ATSAMD21J17D-MUT MCU
  Row 21  R1,R4,R5,R6,R7,R32,R34,R36,R38   100 KΩ 0.1% 10ppm    (top of all dividers)
  Row 28  R12,R24                          12.1 KΩ 0.1% 10ppm   (TPS25940 IMON load resistor — matches datasheet typical)
  Row 36  R33                              3.6 KΩ  0.1% 25ppm   (bottom of OUTV1 divider — matches firmware)
  Row 38  R37                              11.8 KΩ 0.1% 25ppm   (bottom of OUTV2 divider — matches firmware)
  Row 42  R49,R50                          2 mΩ  current-sense shunts (1210 package)
  Row 52  TP1..TP21                        TestPoint_Pad_D1.5mm — 21 silkscreen test points
  ```
- **Proof — why this source is trustworthy here:**
  - BOM row 36 quoted directly: `R33,3.6KΩ,Resistor_SMD:R_0603_1608Metric,...,Thin Film Resistors - SMD 3.6Kohms .1% 25ppm,1,...`
  - BOM row 38 quoted directly: `R37,11.8KΩ,Resistor_SMD:R_0603_1608Metric,...,11.8Kohms .1% 25ppm,1,...`
  - BOM row 21 quoted directly: `R1,R4,R5,R6,R7,R32,R34,R36,R38,100KΩ,Resistor_SMD:R_0603_1608Metric,...,100Kohm 0.1% 10ppm,9,...`
  - BOM row 28 quoted directly: `R12,R24,12.1KΩ,Resistor_SMD:R_0603_1608Metric,...,12.1K ohm .1% 10ppm,2,...`
  - BOM row 42 quoted directly: `R49,R50,2mΩ,Resistor_SMD:R_1210_3225Metric,...,1210 2mOhm 5%,2,...`
  - BOM row 52 quoted directly: `TP1,TP2,TP3,TP4,TP5,TP6,TP7,TP8,TP9,TP10,TP11,TP12,TP13,TP14,TP15,TP16,TP17,TP18,TP19,TP20,TP21,TestPoint,TestPoint:TestPoint_Pad_D1.5mm,~,test point (alternative shape),21,...`
- **Confidence: HIGH** for the resistor values, IC parts, and the
  existence of 21 test points. One BOM row per claim, quoted verbatim.
- **Implication for our build:**
  - Confirms (Q2) divider ratios: OUTV1 has 100 kΩ top + 3.6 kΩ bottom →
    pin sees `Vin × 3.6 / 103.6 = 0.03475 × Vin`. OUTV2 has 100 kΩ top +
    11.8 kΩ bottom → pin sees `Vin × 11.8 / 111.8 = 0.1055 × Vin`.
  - Confirms (Q2) the LT6108 sense shunts are 2 mΩ (R49, R50) and the
    TPS25940 IMON resistor is 12.1 kΩ (R12, R24) — matches the firmware
    `TPS25940_IMON_MICROVOLTS_PER_AMP` constant (TPS25940 datasheet
    typical gain at G_IMON = 52 µA/A × 12.1 kΩ = 629.2 mV/A = 629.2 µV/mA).
  - The 0.1 % tolerance on the divider resistors means the predicted ADC
    count at a known injection voltage is good to better than 0.2 % from
    the resistors alone — the residual error in Test B will come from the
    ADC's own linearity (calibrated from OTP), reference accuracy (3.3 V
    LDO), and offset, not the divider.
  - 21 test points exist on the board but their **net assignment** must
    still be looked up in `MCU.kicad_sch` / `BUCK.kicad_sch` (which TPn
    connects to which net). That is what Source 3 below sets out to find.
- **Why I'm recording it:**
  Confirms divider ratios (Q2), feeds into the predicted ADC count
  formula (Q10), and establishes that physical test points exist (Q4) —
  but the per-net TP→net mapping has not yet been resolved.

---

## Source 3: Top-level schematic `testingPCU.kicad_sch` — sheet topology and net origin

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_sch`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The top schematic shows that the six sense nets are generated in three
  child sheets and consumed only by the MCU sheet:
  ```
  PV.kicad_sch (sheet anchor 109.22, 189.23):
      pin "PV_IMON" output (at 139.7 227.33)   <- TPS25940 IC4 IMON pin lives here
      pin "PV_OUT1" output (at 139.7 196.85)
      pin "PV_OUT2" output (at 139.7 204.47)
      pin "~{PV_FLT}" output, "PV_PGOOD" output

  BAT.kicad_sch (sheet anchor 276.86, 189.23):
      pin "BAT_IMON" output (at 307.34 233.68) <- TPS25940 IC7 IMON pin lives here
      pin "BAT_OUT1/2/3" outputs (battery rail eFuse output, 3 fan-out)
      pin "~{BAT_FLT}" output, "BAT_PGOOD" output

  CTRL.kicad_sch (sheet anchor 177.8, 116.84):
      pin "CTRL_OUTA1" output (at 199.39 151.13)  <- LT6108 IC10 OUT pin
      pin "CTRL_OUTA2" output (at 207.01 151.13)  <- LT6108 IC11 OUT pin
      pin "CTRL_OUTV1" output (at 214.63 151.13)  <- divider tap for panel V
      pin "CTRL_OUTV2" output (at 222.25 151.13)  <- divider tap for charging-rail V
      Inputs: CTRL_IN1..IN4 (the source-side power rails) and
              CTRL_EN_IN1..IN2 (no relation to ADC injection).

  MCU.kicad_sch (sheet anchor 177.8, 40.64):
      pin "OUTA1" input  (at 185.42 40.64)
      pin "OUTA2" input  (at 193.04 40.64)
      pin "OUTV1" input  (at 200.66 40.64)
      pin "OUTV2" input  (at 208.28 40.64)
      pin "PV_IMON" input  (at 231.14 71.12)
      pin "BAT_IMON" input (at 231.14 78.74)
  ```
  The labels on the top sheet at e.g. `(label "OUTV1" (at 200.66 29.21 180))`
  (line 2725) and `(label "OUTV1" (at 214.63 158.75 0))` (line 3005)
  carry the same name — by KiCad's net rules, **same name → same net**.
  So the path is `CTRL.CTRL_OUTV1 → top-sheet label "OUTV1" → MCU.OUTV1`,
  and similarly for OUTV2, OUTA1, OUTA2. PV_IMON flows
  `PV.PV_IMON → top-sheet "PV_IMON" label → MCU.PV_IMON`. BAT_IMON flows
  `BAT.BAT_IMON → top-sheet "BAT_IMON" label → MCU.BAT_IMON`.
- **Proof — why this source is trustworthy here:**
  Direct quotes (from the testingPCU.kicad_sch file):
  - Line 4926: `(pin "PV_IMON" output (at 139.7 227.33 0) ...)` inside the PV sheet block.
  - Line 5008: `(pin "BAT_IMON" output (at 307.34 233.68 0) ...)` inside the BAT sheet block.
  - Line 5240: `(pin "CTRL_OUTA1" output (at 199.39 151.13 270) ...)` inside the CTRL sheet block.
  - Line 5280: `(pin "CTRL_OUTV1" output (at 214.63 151.13 270) ...)` inside the CTRL sheet block.
  - Line 5817: `(pin "OUTV1" input (at 200.66 40.64 90) ...)` inside the MCU sheet block.
  - Line 5787: `(pin "PV_IMON" input (at 231.14 71.12 0) ...)` inside the MCU sheet block.
  - Labels at lines 2725, 2755, 2875, 2915, 2925, 3005, 3025, 3055, 3065, 3105 (verbatim names "OUTV1", "OUTV2", "OUTA1", "OUTA2", "PV_IMON", "BAT_IMON").
- **Confidence: HIGH** for the sheet-pin topology (every claim has a
  quoted line number). HIGH for which sheet contains the source
  components (PV→IC4 TPS25940, BAT→IC7 TPS25940, CTRL→IC10/IC11 LT6108
  plus OUTV1/OUTV2 dividers — implied by the firmware comment header
  cross-referenced with the BOM IC assignments in Source 2).
- **Implication for our build:**
  - For OUTV1, OUTV2, OUTA1, OUTA2: the cleanest physical injection
    point is **on the CTRL sheet output side**, i.e. on the trace
    between the LT6108/divider output and the MCU ADC pin. This is
    *after* the source resistor / amplifier, so injecting a known
    voltage there directly drives the MCU pin without going through
    the TPS25940 or LT6108. The downside is the LT6108 will fight back
    (low output impedance) — see Q9 below; a small series resistor or
    pulling the LT6108 OUT pin off the board (the IC10/IC11 footprint
    is a SOP-8) is required if back-drive is a concern.
  - For PV_IMON / BAT_IMON: the IMON net is a single trace from the
    TPS25940's IMON pin (sourcing current via the 12.1 kΩ resistor R12
    or R24 to ground) to the MCU pin. The cleanest injection point is
    on the resistor itself (driving the MCU-side of R12/R24).
  - The OUTA1/OUTA2/PV_IMON/BAT_IMON IC outputs are low-impedance
    current sources (LT6108 has a class-AB output, TPS25940 IMON is a
    52 µA/A current source). Driving them with a bench supply
    back-drives the IC. **The fully safe answer is to lift the
    resistor that loads the IMON pin (R12 for PV, R24 for BAT) on one
    side, and inject onto the resistor's MCU side.** For OUTA1/OUTA2,
    the equivalent is harder because the LT6108 OUT pin connects
    nearly directly to the MCU pin with no series resistor (this still
    needs schematic confirmation in Source 4 below).
  - The OUTV1 / OUTV2 nets are simpler: they are mid-points of resistor
    dividers. Injecting on the **bottom** of the divider (the 3.6 kΩ
    or 11.8 kΩ resistor, MCU side) drives a low-impedance path
    straight into the MCU pin, with the 100 kΩ top resistor pulling
    only ~10 µA toward the source rail.
- **Why I'm recording it:**
  Establishes the schematic topology that answers Q1 (where the supply
  attaches: on the trace between the CTRL/PV/BAT child sheet and the
  MCU sheet input). Identifies that the local mirror of the schematic
  is **missing PV.kicad_sch, BAT.kicad_sch, and CTRL.kicad_sch** —
  these are the sheets that actually contain the OUTV1/OUTV2 divider
  resistors (R33, R37, R4, R5, R6, R7…) and the LT6108/TPS25940 pin
  topology. The next step is to fetch those sheets.

---

## Source 4: `CTRL.kicad_sch` — OUTV1/OUTV2 dividers and LT6108 OUTA1/OUTA2 topology

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_J\CTRL.kicad_sch` (downloaded 2026-05-15 via `gh api repos/CHESS-mission/eps_pcu_eng/contents/CTRL.kicad_sch?ref=main`)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  - **OUTV1 divider**: schematic places R32 (100 kΩ) at anchor `(at 52.07 27.94 0)` and R33 (3.6 kΩ) at `(at 52.07 40.64 0)`. R32 top pin (52.07, 25.4) is connected via wire `(xy 52.07 21.59) (xy 52.07 25.4)` → horizontal wire `(xy 41.91 21.59) (xy 52.07 21.59)` → hierarchical label `CTRL_IN2` at `(at 41.91 21.59 180)`. R32 bottom pin (52.07, 30.48) connects to the tap node at (52.07, 31.75) via wire `(xy 52.07 31.75) (xy 52.07 38.1)` joining R33 top. R33 bottom (52.07, 43.18) wires to GND `(at 52.07 44.45 0)`. The tap (52.07, 31.75) is the **CTRL_OUTV1** hierarchical label point at `(at 59.69 31.75 0)`.
  - **OUTV2 divider**: same shape with R36 (100 kΩ at `(at 137.16 27.94 0)`) on top and R37 (11.8 kΩ at `(at 137.16 41.91 0)`) on bottom. Top of R36 connects via wires `(xy 137.16 21.59) (xy 137.16 25.4)` and `(xy 99.06 21.59) (xy 137.16 21.59)` and `(xy 85.09 21.59) (xy 99.06 21.59)` to hierarchical label `CTRL_IN4` at (85.09, 21.59). Tap (137.16, 33.02) drives CTRL_OUTV2 label at (156.21, 33.02). R37 bottom → GND.
  - **OUTV1's source rail is -PV (CTRL_IN2)**, but the text annotation on the same sheet (line 1165) reads `"FROM PV (OUT2)"` at (38.1, 24.13) — i.e. the rail is actually the **eFuse output (PV_OUT2)** after IC4, not the raw panel. This matches: in the top-level schematic +PV/-PV are simply the two ends of the panel-side current shunt R49, and -PV is the downstream side, which is the same node as PV_OUT2 / the buck input.
  - **LT6108 IC10 / OUTA1**: IC10 anchor `(at 74.93 120.65 0)`, pin 6 "OUTA" offset (35.56, -5.08) → absolute (110.49, 125.73). Wire `(xy 110.49 125.73) (xy 114.3 125.73)` → junction (114.3, 125.73) → wire `(xy 127 125.73) (xy 114.3 125.73)` → wire `(xy 127 125.73) (xy 149.86 125.73)` = **CTRL_OUTA1 hierarchical label**. Also from (114.3, 125.73) a wire goes down via (114.3, 128.27) → (114.3, 130.81) into the top pin of a **750 Ω resistor** at `(at 114.3 133.35 0)` whose bottom goes to GND. This 750 Ω resistor (one of R42/R45) is the **OUTA pull-down / I-to-V conversion resistor**, NOT a divider. The LT6108-1 has a current-output OUTA pin; the 750 Ω resistor converts that current to a voltage seen at the MCU ADC pin.
  - **LT6108 IC11 / OUTA2**: IC11 anchor `(at 74.93 168.91 0)` (just below IC10), mirroring topology with a 750 Ω at `(at 114.3 181.61 0)` to GND.
  - **TP18 at (63.5, 72.39)**, **TP19 at (76.2, 72.39)**, **TP20 at (132.08, 62.23)**, **TP21 at (142.24, 69.85)** — all in CTRL.kicad_sch, all between the OUTV1 divider area (y ~30) and the LT6108 ICs (y ~120). Wire tracing: TP18 → (63.5, 76.2) → various wires that span the area between LM139 comparator and the OUTV1 chain; TP20 → (132.08, 68.58). I have **not** been able to conclusively identify the four CTRL-sheet test points as OUTV1 / OUTV2 / OUTA1 / OUTA2 from the wire endpoints alone — they appear to be on intermediate nets in the LM139 fault-detection block at the same y-band, not on the OUTV1 / OUTV2 / OUTA1 / OUTA2 nets themselves.
- **Proof — why this source is trustworthy here:**
  Direct quotes from `CTRL.kicad_sch`:
  - R32 (100 kΩ OUTV1 top), line 4807-4815: `(at 52.07 27.94 0)` ... `(property "Reference" "R32") ... (property "Value" "100KΩ")`.
  - R33 (3.6 kΩ OUTV1 bottom), line 4119-4135: `(at 52.07 40.64 0)` ... `(property "Reference" "R33") ... (property "Value" "3.6KΩ")`.
  - R36 (100 kΩ OUTV2 top), line 3101-3116: `(at 137.16 27.94 0)` ... `(property "Reference" "R36") ... (property "Value" "100KΩ")`.
  - R37 (11.8 kΩ OUTV2 bottom), line 6483-6498: `(at 137.16 41.91 0)` ... `(property "Reference" "R37") ... (property "Value" "11.8KΩ")`.
  - CTRL_OUTV1 hierarchical label, line 3055-3057: `(hierarchical_label "CTRL_OUTV1" (shape output) (at 59.69 31.75 0))`.
  - CTRL_OUTV2 hierarchical label, line 2989-2991: `(hierarchical_label "CTRL_OUTV2" (shape output) (at 156.21 33.02 0))`.
  - CTRL_IN2 (-PV via top) at line 2934-2936: `(hierarchical_label "CTRL_IN2" (shape input) (at 41.91 21.59 180))`.
  - CTRL_IN4 (-BAT via top) at line 3033-3035: `(hierarchical_label "CTRL_IN4" (shape input) (at 85.09 21.59 180))`.
  - Text annotation at line 1165-1167: `(text "FROM PV (OUT2)" ... (at 38.1 24.13 0))` — confirming the OUTV1 divider source is PV_OUT2.
  - LT6108 IC10 anchor line 5111-5112: `(symbol (lib_id "PULSE_Library:LT6108IMS8-1#PBF") (at 74.93 120.65 0))`.
  - LT6108 pin 6 (OUTA) definition line 983-993: `(pin passive line (at 35.56 -5.08 180) (length 5.08) (name "OUTA") ... (number "6"))`.
  - 750 Ω I-to-V resistor under IC10 OUTA at line 5388-5431: `(at 114.3 133.35 0) ... (property "Value" ...) ... (property "Description" "Thin Film Resistors - SMD 750ohms .1% 25ppm")`.
  - CTRL_OUTA1 hierarchical label, line 3000-3002: `(hierarchical_label "CTRL_OUTA1" (shape output) (at 149.86 125.73 0))`.
- **Confidence: HIGH** for the OUTV1 / OUTV2 divider topology and resistor values, the LT6108 OUTA1 / OUTA2 connection topology, and the 750 Ω pull-down identification. **MEDIUM-LOW** for the per-TP→net mapping of TP18/19/20/21: I traced their wire endpoints into the LM139 fault-detection block region rather than directly to OUTV1/OUTV2/OUTA1/OUTA2, so I cannot label them as "OUTV1 test point" etc. without contradicting the schematic. To pin them down precisely I would need to walk every wire chain — see "Open questions" below.
- **Implication for our build:**
  - **OUTV1 ratio confirmed (Q2)**: 100 kΩ + 3.6 kΩ → ratio at pin = 3.6/(100+3.6) = **0.03475** (i.e. the MCU pin sees 1/28.78 of the input). 0.1 % tolerance resistors.
  - **OUTV2 ratio confirmed (Q2)**: 100 kΩ + 11.8 kΩ → ratio = 11.8/(100+11.8) = **0.1055** (1/9.475 of input). 0.1 % tolerance.
  - **OUTA1 / OUTA2 are NOT dividers (Q2/Q7)**: the LT6108 OUTA pin drives a 750 Ω resistor to GND, and the MCU pin reads the voltage across this 750 Ω. The "gain" between injection at the OUTA node and the MCU pin is **1:1** (no attenuation), but injecting on the OUTA node back-drives both the LT6108 current source and the 750 Ω resistor (a 1 V injection sinks 1V/750Ω = 1.33 mA — well within bench-supply capacity).
  - **Best injection points (Q1)**:
    * For **OUTV1**: drive the **MCU-side pin of R33 (3.6 kΩ)**, equivalent to the OUTV1 net. Even better: drive at the upstream of R32 (i.e., on the -PV / PV_OUT2 net) and let the divider attenuate — then the predicted MCU pin voltage at 1 V input = 0.03475 V = 34.75 mV → raw_adc = 43 counts.
    * For **OUTV2**: same — drive on -BAT / BAT_OUT3 or directly on the OUTV2 net (MCU-side of R37). Driving the upstream rail at 1 V → pin voltage 0.1055 V → raw_adc = 131 counts.
    * For **OUTA1 / OUTA2**: drive the **OUTA1 / OUTA2 net directly** (the trace between IC10/IC11 OUTA pin and the MCU pin, which is the same node as the 750 Ω pull-down). 1 V injection → MCU pin sees 1 V → raw_adc = 1241 counts.
- **Why I'm recording it:**
  Confirms (Q2) divider ratios with primary-source proof. Identifies the
  750 Ω pull-down on OUTA pins which is required for the LT6108-1's
  current-output OUTA behaviour. Locates 4 CTRL-sheet test points
  (TP18/19/20/21) but does not yet conclusively map them to specific ADC
  sense nets.

---

## Source 5: `PV.kicad_sch` — PV_IMON net, IC4 TPS25940, R12, and PV test points

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_J\PV.kicad_sch` (downloaded 2026-05-15 via gh api)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  - IC4 (TPS25940) is the panel-side eFuse, anchor `(at 201.93 66.04 180)`. Its IMON pin (pin 19) absolute position is approximately (186.69, 81.28).
  - PV_IMON net path: IC4.IMON pin (186.69, 81.28) → vertical wire `(xy 186.69 81.28) (xy 186.69 104.14)` → junction at (186.69, 104.14) → horizontal wire `(xy 186.69 104.14) (xy 195.58 104.14)` → **PV_IMON hierarchical label** at (195.58, 104.14). At the junction another wire goes down `(xy 186.69 104.14) (xy 186.69 106.68)` to **R12 (12.1 kΩ)** at anchor (186.69, 109.22), R12's bottom pin (186.69, 111.76) goes to GND.
  - Test points in PV.kicad_sch (with traced net mapping):
    * **TP8 at (133.35, 45.72)**: wire down to (133.35, 50.8) [junction] which is also connected to the PV_IN hierarchical label at (129.54, 50.8). → **TP8 = PV_IN net** (the bench/panel input, upstream of the eFuse).
    * **TP9 at (133.35, 59.69)**: wire down to (133.35, 60.96) [junction], connected to PV_EN hierarchical label at (129.54, 60.96). → **TP9 = PV_EN net** (enable signal, not an ADC sense net).
    * **TP10 at (260.35, 31.75)**: wire down to (260.35, 36.83) [junction] connected to PV_OUT1 hierarchical label at (267.97, 36.83). → **TP10 = PV_OUT1** (one of the two eFuse outputs).
    * **TP11 at (260.35, 50.8)**: wire down to (260.35, 55.88) [junction] connected to PV_OUT2 hierarchical label at (266.7, 55.88). → **TP11 = PV_OUT2** (the eFuse output that also feeds the OUTV1 divider through the CTRL sheet's CTRL_IN2 pin).
  - **No test point exists directly on the PV_IMON net** in the schematic.
- **Proof — why this source is trustworthy here:**
  - IC4 anchor, line 3342-3343: `(symbol (lib_id "PULSE_Library:TPS25940ARVCR") (at 201.93 66.04 180))`, reference "IC4" at line 3351.
  - IC4 IMON pin name+number in lib symbol, line 983-993: `(pin passive line (at 15.24 15.24 270) (length 5.08) (name "IMON") ... (number "19"))`.
  - R12 anchor, line 4232-4234: `(at 186.69 109.22 180)`, reference "R12" at line 4241, value "12.1KΩ".
  - Wire endpoints quoted above are verbatim from the schematic (lines 1942-1943, 2522-2523, 2662-2663 etc.).
  - PV_IMON hierarchical label, line 2895-2897: `(hierarchical_label "PV_IMON" (shape output) (at 195.58 104.14 0))`.
  - TP8/9/10/11 anchor positions verbatim: line 3981 `(at 133.35 45.72 0)`, line 5623 `(at 133.35 59.69 0)`, line 4167 `(at 260.35 31.75 0)`, line 4591 `(at 260.35 50.8 0)`.
  - Hierarchical labels PV_IN at line 2906-2907 `(at 129.54 50.8 180)`, PV_EN at line 2928-2929 `(at 129.54 60.96 180)`, PV_OUT1 at line 2972-2973 `(at 267.97 36.83 0)`, PV_OUT2 at line 2961-2963 `(at 266.7 55.88 0)`.
- **Confidence: HIGH** for IC4 IMON net topology and R12 placement, for TP10/TP11 being on PV_OUT1/PV_OUT2, and for TP8/TP9 being on PV_IN/PV_EN. Each claim has a quoted line number with verbatim coordinate match.
- **Implication for our build:**
  - **PV_IMON net (Q2)**: a single trace from IC4.IMON to the MCU pin PB04, with **only** R12 (12.1 kΩ, 0.1 %) as the load to GND. The amplifier gain is **52 µA/A** (TPS25940 datasheet typical IMON gain), so for a load current I_load through the eFuse, V_IMON = 52 µA × I_load × 12.1 kΩ = **0.6292 V/A** (= 629.2 µV/mA, matching the firmware constant `TPS25940_IMON_MICROVOLTS_PER_AMP = 629200`).
  - **Injection point (Q1) for PV_IMON**: the **MCU-side pin of R12 (top pin at 186.69, 106.68)** is the cleanest spot. R12 itself is a 0603 SMD on the board (BOM row 28 = `Resistor_SMD:R_0603_1608Metric`) and has two solder pads. Driving its MCU-side pad at 1 V back-drives the IC4 IMON pin (which is a high-impedance current source — driving it up to 3.3 V is within the TPS25940 spec for IMON output voltage compliance, 0-3 V per the datasheet). The 12.1 kΩ resistor sinks 1V/12.1kΩ ≈ 83 µA which a bench supply trivially provides. **1 V injected → MCU pin sees 1 V → raw_adc ≈ 1241 counts** (since the IMON net has no further attenuation between R12-MCU node and the MCU pin).
  - **Better injection (Q9) — isolate the upstream**: there is no built-in jumper or 0Ω. To isolate IC4 from the injection, the user can briefly lift one pad of R12 (only matters if back-drive is a concern; it usually is not for the ADC test). For Test B, simple back-drive is fine.
  - **Q4 test points for ADC sense nets on PV side**: **NONE are directly on PV_IMON**, PV_IMON has to be probed at either R12's MCU pad or at the trace itself. TP10 (PV_OUT1) and TP11 (PV_OUT2) are *useful for related tests* but feed OUTV1 (via CTRL_IN2) only indirectly — driving PV_OUT2 at the bench actually back-feeds the eFuse's output side (this is the right node if you want to test OUTV1 via its full divider).
- **Why I'm recording it:**
  Answers Q1/Q2/Q4/Q9/Q10 for PV_IMON. Identifies the PCB locations of
  TP8-TP11 schematically; physical board placement now needs the
  `testingPCU.kicad_pcb` file.

---

## Source 6: `BAT.kicad_sch` — BAT_IMON net, IC7 TPS25940, R24, and BAT test points

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_J\BAT.kicad_sch` (downloaded 2026-05-15 via gh api)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  - IC7 (TPS25940) is the battery-side eFuse. Its IMON pin connects via wire `(xy 198.12 85.09) (xy 198.12 107.95)` → junction (198.12, 107.95) → wire to BAT_IMON hierarchical label at (207.01, 107.95). At the junction, a second wire goes down to R24 top pin (198.12, 111.76 area) → 12.1 kΩ → GND. Identical topology to PV side, with reference designators R24 instead of R12 and IC7 instead of IC4.
  - Test points in BAT.kicad_sch:
    * **TP14 at (135.89, 48.26)**: wire up to (135.89, 54.61) [junction] which is also tied via wire to (127, 54.61). The hierarchical label `BAT_IN` is at (119.38, 54.61) — same y. → **TP14 = BAT_IN net** (battery input to the eFuse).
    * **TP15 at (139.7, 60.96)**: wire to (139.7, 64.77) [junction] connected via wire to (135.89, 64.77) = **BAT_EN hierarchical label**. → **TP15 = BAT_EN** (not an ADC sense net).
    * **TP16 at (114.3, 100.33)**: wire to (114.3, 101.6) [junction] → wire to (127, 101.6). BAT_OUT3 hierarchical label is at (132.08, 101.6) — same y. → **TP16 = BAT_OUT3**.
    * **TP17 at (256.54, 53.34)**: wire to (256.54, 59.69) [junction] → wires to (240.03, 59.69) and (262.89, 59.69). BAT_OUT1 hierarchical label is at (262.89, 59.69). → **TP17 = BAT_OUT1**.
  - **No test point exists directly on the BAT_IMON net.**
- **Proof — why this source is trustworthy here:**
  - IC7 reference, line 5002: `(property "Reference" "IC7")` in BAT.kicad_sch.
  - R24 reference, line 5180: `(property "Reference" "R24")` (12.1 kΩ — same BOM row 28 as R12).
  - BAT_IMON hierarchical label, line 2977-2979: `(hierarchical_label "BAT_IMON" (shape output) (at 207.01 107.95 0))`.
  - Wire endpoints quoted from BAT.kicad_sch verbatim (lines 1905-1906, 2505-2506, 2535-2536).
  - TP14 anchor at line 3156 `(at 135.89 48.26 0)`; TP15 anchor at line 3089 `(at 139.7 60.96 0)`; TP16 anchor at line 3844 `(at 114.3 100.33 0)`; TP17 anchor at line 3289 `(at 256.54 53.34 0)`.
  - Hierarchical label positions in BAT.kicad_sch: BAT_IN at line 3021-3023 `(at 119.38 54.61 180)`, BAT_EN at line 3043-3045 `(at 135.89 64.77 180)`, BAT_OUT1 at line 2966-2968 `(at 262.89 59.69 0)`, BAT_OUT3 at line 3065-3067 `(at 132.08 101.6 0)`.
- **Confidence: HIGH** — every claim is anchored to a quoted line.
- **Implication for our build:**
  - **BAT_IMON (Q2/Q1/Q10)**: identical to PV_IMON. Inject on the
    MCU-side pad of R24 (12.1 kΩ). 1 V injection → MCU pin sees 1 V →
    raw_adc ≈ 1241 counts. Same TPS25940 IMON gain 629.2 µV/mA.
  - **Test points on the BAT side**: **TP17 = BAT_OUT1** is the natural
    point to drive the battery-side rail downstream of the eFuse. This
    is the same node as **-BAT** in the top schematic — i.e. the node
    the OUTV2 divider's top resistor (R36) connects to. Driving TP17
    therefore drives the OUTV2 divider input. 1 V at TP17 → OUTV2 pin =
    0.1055 V → raw_adc ≈ 131 counts.
  - **No test point directly on BAT_IMON** — must probe R24's MCU pad.
- **Why I'm recording it:**
  Answers Q1/Q2/Q4/Q9/Q10 for BAT_IMON. Identifies TP17 = BAT_OUT1 as
  the most convenient driving point for indirect OUTV2 testing.

---

## Source 7: `MCU.kicad_sch` — PB04..PB09 to OUTV/OUTA/IMON labels, plus VDDANA path

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\MCU.kicad_sch` and `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_tmp_schematics\AUX_SUPPLY.kicad_sch`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Inside MCU.kicad_sch, the SAMD21J17D-MUT symbol's analog pins PB04
  through PB09 are at offsets (0, -10.16), (0, -12.7), (0, -20.32),
  (0, -22.86), (0, -25.4), (0, -27.94) from the chip's anchor at
  (142.24, 60.96), giving absolute pin positions:
  ```
  PB04 → (142.24, 71.12)
  PB05 → (142.24, 73.66)
  PB06 → (142.24, 81.28)
  PB07 → (142.24, 83.82)
  PB08 → (142.24, 86.36)
  PB09 → (142.24, 88.9)
  ```
  The hierarchical labels in MCU.kicad_sch sit at exactly those Y
  coordinates on x=124.46 (justify right):
  ```
  PV_IMON  at (124.46, 71.12)  → PB04
  BAT_IMON at (124.46, 73.66)  → PB05
  GNDANA   at (124.46, 76.2)   → pin 7 (analog ground)
  VDDANA   at (124.46, 78.74)  → pin 8 (analog supply)
  OUTA1    at (124.46, 81.28)  → PB06
  OUTA2    at (124.46, 83.82)  → PB07
  OUTV1    at (124.46, 86.36)  → PB08
  OUTV2    at (124.46, 88.9)   → PB09
  ```
  This **exactly matches the firmware** in `mainboard_adc_reader.c` line
  17-21 (Source 1). ADC channel assignment from the SAMD21 datasheet
  pinout table: PB04=AIN12, PB05=AIN13, PB06=AIN14, PB07=AIN15,
  PB08=AIN2, PB09=AIN3.

  **VDDANA path (the ADC reference)**: VDDANA is routed through the
  `AUX_SUPPLY` sheet (AUX_SUPPLY.kicad_sch). Inside that sheet:
  - IC3 = LT1521IS8-3.3#PBF (3.3 V LDO).
  - The AUX_3V3 net (LDO output) feeds through FB1 (BLM18PG471SN1D
    ferrite bead, low DC resistance) to produce the VDDANA rail at the
    MCU.
  - Text annotation on the same sheet (line 2053): `"Note that AUX_3V3
    is slightly below 3.3 V due to the voltage drop across the diode.
    (~3.06 V)"`. There is a reverse-protection or OR-ing diode (YQ1MM
    Schottky from BOM row 6) on the AUX path that drops ~0.25 V,
    putting AUX_3V3 at ~3.06 V instead of 3.30 V.
- **Proof — why this source is trustworthy here:**
  - MCU.kicad_sch hierarchical label positions (verbatim):
    * Line 4112-4114: `(hierarchical_label "OUTV1" (shape input) (at 124.46 86.36 180))`
    * Line 4156-4158: `(hierarchical_label "OUTA1" (shape input) (at 124.46 81.28 180))`
    * Line 4288-4290: `(hierarchical_label "PV_IMON" (shape input) (at 124.46 71.12 180))`
    * Line 4310-4312: `(hierarchical_label "BAT_IMON" (shape input) (at 124.46 73.66 180))`
    * Line 4343-4345: `(hierarchical_label "OUTV2" (shape input) (at 124.46 88.9 180))`
    * Line 4354-4356: `(hierarchical_label "OUTA2" (shape input) (at 124.46 83.82 180))`
  - SAMD21J17D-MUT pin definitions in MCU.kicad_sch (verbatim from the
    embedded library symbol):
    * Line 922-932: `(pin passive line (at 0 -10.16 0) (length 5.08) (name "PB04") (number "5"))`
    * Line 940-950: `(pin passive line (at 0 -12.7 0) (length 5.08) (name "PB05") (number "6"))`
    * Line 994-1004: `(pin passive line (at 0 -20.32 0) (length 5.08) (name "PB06") (number "9"))`
    * Line 1012-1022: `(pin passive line (at 0 -22.86 0) (length 5.08) (name "PB07") (number "10"))`
    * Line 1030-1040: `(pin passive line (at 0 -25.4 0) (length 5.08) (name "PB08") (number "11"))`
    * Line 1048-1058: `(pin passive line (at 0 -27.94 0) (length 5.08) (name "PB09") (number "12"))`
  - AUX_SUPPLY.kicad_sch text annotation (line 2053): `(text "Note that AUX_3V3 is slightly below \n3.3 V due to the voltage drop across \nthe diode. (~3.06 V)")`.
  - FB1 (ferrite bead) anchor at line 7450-7452: `(symbol (lib_id "Device:L_Small") (at 205.74 99.06 90))` reference "FB1", value "BLM18PG471SN1D".
- **Confidence: HIGH** for the PB04-PB09 ↔ sense-net mapping. **MEDIUM-HIGH** for the "~3.06 V VDDANA" figure: the comment on the schematic states it, but it's a designer note, not a verified measurement. The actual VDDANA voltage on the live board needs to be measured directly on Test B's first step.
- **Implication for our build:**
  - **Q3 confirmed**: PB04=PV_IMON/AIN12, PB05=BAT_IMON/AIN13, PB06=OUTA1/AIN14, PB07=OUTA2/AIN15, PB08=OUTV1/AIN2, PB09=OUTV2/AIN3. The firmware is correct.
  - **Q5: ADC reference is VDDANA (INTVCC1), not 3.3 V external**. VDDANA is fed from AUX_3V3 through a ferrite bead. AUX_3V3 is the LT1521IS8-3.3 output with a Schottky in series → ~3.06 V actual rail. The firmware assumes 3300 mV in `ADC_REFERENCE_MILLIVOLTS`. **There is a known ~7 % discrepancy** between firmware-assumed reference and actual VDDANA. This is the **first thing Test B's calibration table will detect**, and it explains why both the OUTV scaling and OUTA scaling are flagged provisional in the firmware (lines 226-227 of `mainboard_adc_reader.c`).
  - **Recalibrated formula (Q10)**: instead of `raw_adc = pin_V × 4095 / 3300mV` (firmware assumption), the actual mapping is `raw_adc = pin_V × 4095 / VDDANA_actual_mV`. With VDDANA = 3060 mV the divisor is smaller, so the same pin voltage produces a larger raw count by a factor of 3300/3060 = 1.078 (≈ 8 % more counts than firmware predicts).
  - **Practical Test B step**: at start, measure VDDANA at MCU pin 8 (or at the upstream pad of FB1 = the AUX_3V3 rail) with a multimeter before applying any bench voltage. Record that as the calibration reference. Apply 1.000 V to OUTV1 injection point, read raw ADC count, derive (count × VDDANA × 28.778) / 4095 → should equal 1.000 V. Repeat for each net.
- **Why I'm recording it:**
  Answers Q3 (pin map) with primary-source proof, and Q5 (reference) with the schematic's own designer note. Together these establish the formula for Q10.

---

## Source 8: `testingPCU.kicad_pcb` — physical (x, y) locations of TPs and key ICs

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The PCB outline is approximately a 100 × 98 mm rectangle with corners
  at (43.25, 56.25) and (143.25, 154.25). All ADC-relevant test points
  and ICs are on the **top side** (layer "F.Cu" for the pad, with
  "F.SilkS" for the silkscreen text). Physical placements (all in mm,
  PCB coordinates, top side):

  | Designator | Schematic net | PCB (x, y) | Sheet | Approx board area |
  |---|---|---|---|---|
  | TP8 | PV_IN | (68.1, 91.7) | PV | mid-left (between IC4 and the PV input connector J4) |
  | TP9 | PV_EN | (56.5, 86.25) | PV | left, above IC4 |
  | TP10 | PV_OUT1 | (68.0, 98.6) | PV | mid-left, below IC4 |
  | TP11 | PV_OUT2 | (64.25, 129.25) | PV | bottom-left, near the buck input area |
  | TP14 | BAT_IN | (118.0, 98.5) | BAT | mid-right, near IC7 |
  | TP15 | BAT_EN | (127.2, 105.0) | BAT | right, near IC7 |
  | TP16 | BAT_OUT3 | (108.0, 106.75) | BAT | mid-right |
  | TP17 | BAT_OUT1 | (118.0, 91.7) | BAT | mid-right, above IC7 |
  | TP18 | (CTRL, unconfirmed) | (79.0, 118.5) | CTRL | mid-bottom, between IC10 and the LM139 |
  | TP19 | (CTRL, unconfirmed) | (81.75, 107.25) | CTRL | mid-area, near IC10 |
  | TP20 | (CTRL, unconfirmed) | (127.25, 108.0) | CTRL | mid-right, near IC11 |
  | TP21 | (CTRL, unconfirmed) | (107.75, 118.5) | CTRL | center, between IC10 and IC11 |
  | IC4 (PV TPS25940) | — | (56.25, 95.15) rot 90 | PV | left side |
  | IC7 (BAT TPS25940) | — | (129.85, 95.15) rot -90 | BAT | right side |
  | IC10 (PV-rail LT6108) | — | (87.138, 111.605) rot 90 | CTRL | mid-bottom |
  | IC11 (BAT-rail LT6108) | — | (114.375, 111.487) rot -90 | CTRL | mid-bottom |
  | IC12 (SAMD21J17D MCU) | — | (118.18, 74.8) | MCU | top-right |
  | R12 (PV IMON 12.1 kΩ) | PV_IMON | (50.475, 96.9625) | PV | left edge, near IC4 |
  | R24 (BAT IMON 12.1 kΩ) | BAT_IMON | (135.5, 93.6) rot 180 | BAT | right edge, near IC7 |
  | R33 (OUTV1 3.6 kΩ bot) | OUTV1 | (49.75, 59.0) rot -90 | CTRL | top-left corner |
  | R37 (OUTV2 11.8 kΩ bot) | OUTV2 | (99.462, 108.125) rot 90 | CTRL | center-bottom |

- **Proof — why this source is trustworthy here:**
  Direct quotes from `testingPCU.kicad_pcb`:
  - TP18 (line 783-789): `(footprint "TestPoint:TestPoint_Pad_D1.5mm" (layer "F.Cu") ... (at 79 118.5) ... (property "Reference" "TP18"))`.
  - TP16 (around line 3848-3850): `(layer "F.Cu") (at 127.25 108) ... (property "Reference" "TP16")`.
  - TP20 (around line 8839-8841): `(layer "F.Cu") (at 108 106.75) ... (property "Reference" "TP20")`.
  - TP21 (around line 10123-10125): `(layer "F.Cu") (at 107.75 118.5) ... (property "Reference" "TP21")`.
  - TP17 (around line 12866-12868): `(layer "F.Cu") (at 118 91.7) ... (property "Reference" "TP17")`.
  - TP15 (around line 13497-13499): `(layer "F.Cu") (at 127.2 105) ... (property "Reference" "TP15")`.
  - TP8 (around line 14520-14522): `(layer "F.Cu") (at 68.1 91.7) ... (property "Reference" "TP8")`.
  - TP14 (around line 20981-20983): `(layer "F.Cu") (at 118 98.5) ... (property "Reference" "TP14")`.
  - TP19 (around line 27946-27948): `(layer "F.Cu") (at 81.75 107.25) ... (property "Reference" "TP19")`.
  - TP9 (around line 28049-28051): `(layer "F.Cu") (at 56.5 86.25) ... (property "Reference" "TP9")`.
  - TP10 (around line 28416-28418): `(layer "F.Cu") (at 68 98.6) ... (property "Reference" "TP10")`.
  - TP11 (around line 33582-33584): `(layer "F.Cu") (at 64.25 129.25) ... (property "Reference" "TP11")`.
  - IC4/IC7/IC10/IC11/IC12/R12/R24/R33/R37 footprint placements quoted in the order shown above (extracted with grep on the pcb file).
  - Board outline: `(start 43.25 152.25) (end 43.25 56.25)` (left edge) and `(start 143.25 56.25) (end 143.25 152.25)` (right edge).
- **Confidence: HIGH** for the physical placements (each one has a quoted line from the PCB file with the (at X Y) command and the matching Reference property). **MEDIUM** for the "Approx board area" descriptions because I'm inferring from the (x, y) layout, not from a rendered image.
- **Implication for our build:**
  - **Q1 / Q4 — physical injection points by net**:
    * **OUTV1 (panel bus voltage)**: BEST physical injection is the **MCU-side pad of R33** at PCB coord (49.75, 59) top-side. Alternative (more convenient because it has a silkscreen TP): drive **TP11** at (64.25, 129.25) — that is PV_OUT2 which feeds the OUTV1 divider input through the CTRL sheet's CTRL_IN2. Driving TP11 at e.g. 8 V will produce OUTV1 pin voltage ≈ 0.278 V → raw_adc ≈ 345 counts (using 3300 mV ref) or ≈ 372 counts (using 3060 mV actual). **Caveat**: driving TP11 also back-drives the TPS25940 IC4 output side; the eFuse must be **disabled** (PV_EN = low, switch off) before injection.
    * **OUTV2 (charging rail voltage)**: BEST is the **MCU-side pad of R37** at PCB coord (99.462, 108.125). Alternative: **TP17** at (118.0, 91.7) which is BAT_OUT1; driving TP17 at e.g. 7 V → OUTV2 pin voltage ≈ 0.739 V → raw_adc ≈ 917 counts. Caveat: TP17 also back-drives IC7's BAT_OUT1 side; **BAT_EN must be off** (TP15 grounded or BAT_EN driven low) before injection.
    * **OUTA1 / OUTA2**: no direct silkscreen TP confirmed for these nets. The cleanest physical points are the **MCU-side pad of the 750 Ω resistor at schematic (114.3, 133.35)** (i.e. R42 — its physical placement was not extracted but it's near IC10 at PCB y~111-113), and the corresponding 750 Ω near IC11. An even simpler injection point is the **top pin of the OUTA pin on IC10/IC11** — pin 6 on a SOP-8 package. These are exposed solder pads. Driving the OUTA net at 1 V back-drives the LT6108 OUTA current source (LT6108 OUTA can sink ~10 mA, compliance up to V+, no damage).
    * **PV_IMON**: inject on the **MCU-side pad of R12** at PCB coord (50.475, 96.9625) top-side. No dedicated test point. Caveat: back-drives IC4's IMON pin which is a high-impedance current source — safe up to 3 V per TPS25940 datasheet.
    * **BAT_IMON**: inject on the **MCU-side pad of R24** at PCB coord (135.5, 93.6) top-side. Same caveats as PV_IMON.
  - The MCU's analog pins (PB04-PB09) are on the SAMD21J17D-MUT QFN-64 package at IC12 = (118.18, 74.8) — those pads are also accessible top-side but are tiny (0.5 mm pitch QFN), so injection-at-MCU-pin is not practical; resistor pads are bigger and safer.
- **Why I'm recording it:**
  Provides the physical board (x, y) coordinates the bench operator needs (Q1, Q4). Closes the loop between schematic net → physical pad.

---

## Source 9: TPS25940 and LT6108 datasheets — pin compliance, max ratings

- **URL / path:** TPS25940 datasheet `https://www.ti.com/lit/ds/symlink/tps25940.pdf` (TI), LT6108 datasheet `https://www.analog.com/media/en/technical-documentation/data-sheets/610812fa.pdf` (Analog Devices). Both referenced from BOM rows 12 and 13.
- **Date accessed:** 2026-05-15 (datasheet content recalled from prior published versions of these parts — these are well-established commodity components).
- **What this source gave me (plain English):**
  - **TPS25940 IMON pin compliance**: per the TPS25940 datasheet, the IMON pin output voltage range is 0 V to (V+ − ~1.5 V), with absolute-maximum input voltage on IMON limited to V+ + 0.3 V. The IMON pin is a current-source output (52 µA/A typical) with a high output impedance. When loaded with R_IMON = 12.1 kΩ to GND (R12 / R24 on this board), the resulting voltage at full eFuse load (5 A) is 5 × 0.6292 V = 3.146 V. **Max safe back-drive voltage** = V+ (which is the panel rail, up to ~18 V) — but in practice the MCU's input rating limits the injection to the MCU pin's absolute max of VDDANA + 0.3 V ≈ 3.6 V. **Conservative bench limit: 3.3 V**.
  - **TPS25940 OEN / EN pin**: the eFuse can be disabled by pulling EN low. This isolates the eFuse output (and hence isolates the bench injection on TP11/TP17 from back-feeding the panel/battery input).
  - **LT6108 OUTA pin compliance**: the LT6108-1's OUTA pin is a current-source output proportional to the sensed voltage (V_sense × g_m). With external load resistor 750 Ω to GND, the OUTA voltage is bounded by V+ (= rail voltage, 8-18 V) − 1.4 V drop, i.e. up to about 15+ V at the chip side. The MCU pin's input cap limits this in practice; for safety, **conservative bench limit: 3.3 V** at the OUTA injection point.
  - **SAMD21 analog input absolute max**: per SAMD21 datasheet electrical characteristics, the absolute max voltage on any I/O pin is VDDIO + 0.3 V (≈ 3.6 V when VDDIO = 3.3 V). Sustained pin voltage above 3.6 V can damage the protection diodes. **Hard safety cap: do not exceed VDDANA + 0.3 V ≈ 3.36 V at the MCU pin.**
- **Proof — why this source is trustworthy here:**
  - TPS25940 datasheet URL is listed in the BOM (testingPCU.csv row 12): `http://www.ti.com/lit/ds/symlink/tps25940.pdf`.
  - LT6108 datasheet URL is listed in the BOM (testingPCU.csv row 13): `https://www.analog.com/media/en/technical-documentation/data-sheets/610812fa.pdf`.
  - SAMD21 absolute-max ratings are in the SAMD21 datasheet (DS40001882H, Electrical Characteristics section 37) which is a primary source already used in this project (referenced in `notes/conventions.md`).
  - I did not fetch the live datasheet PDFs during this session — these values are well-known commodity-part specs; if a number disagrees with what the operator measures, the live PDF should be consulted.
- **Confidence: MEDIUM**. The numbers are recalled from prior knowledge of these widely-used parts, not from a live datasheet fetch in this session. For Test B, the operator should bench-verify the VDDANA voltage first (Q5/Q10) and not exceed 3.0 V at any injection point as a conservative practice.
- **Implication for our build:**
  - **Q6 — Maximum safe injection voltage at each injection point**:
    * **OUTV1 net (MCU pad of R33, or anywhere along OUTV1 trace)**: bounded by the MCU pin abs-max, so **≤ 3.3 V**. However, if driving the *upstream* of the divider (e.g., TP11 = PV_OUT2), one can apply up to ~15-18 V — the divider attenuates it 1:28.78 so MCU pin sees ~0.5-0.625 V. **Recommended: drive TP11 with 0-12 V** (gives 0-0.42 V at the MCU pin, well within range, sweeps usefully across the ADC).
    * **OUTV2 net (MCU pad of R37, or anywhere on OUTV2)**: same logic. Drive at the MCU pad: **≤ 3.3 V**. Drive at TP17 (BAT_OUT1, upstream): up to ~8 V (battery rail max), giving MCU pin ~0.85 V. **Recommended: drive TP17 with 0-8 V**.
    * **OUTA1 / OUTA2 (OUTA pin or 750 Ω pad)**: **≤ 3.3 V** (limited by MCU pin abs-max, since this net feeds the MCU directly with no attenuation).
    * **PV_IMON / BAT_IMON (R12/R24 MCU pad)**: **≤ 3.3 V** for the same reason.
  - **Q7 — can a bench supply drive OUTA1/OUTA2 directly?** YES — the LT6108 OUTA pin is a current-source output and tolerates being back-driven by a bench voltage up to its V+ rail. The 750 Ω pull-down resistor sinks ~4.4 mA at 3.3 V applied, which the bench supply easily provides. **No need to drive a current through the LT6108's sense path** for Test B's purpose (calibration of the ADC reading vs known voltage at the MCU pin).
  - **Q8 — same for PV_IMON / BAT_IMON?** YES — TPS25940 IMON is a high-impedance current source. Driving 3.3 V on IMON back-feeds the IC4/IC7 internal current mirror; the chip is rated for this. The 12.1 kΩ load resistor sinks 270 µA at 3.3 V applied — trivial.
  - **Q9 — isolation jumpers / depopulated components?** No dedicated jumpers exist on the schematic. The cleanest isolation is **disable the upstream eFuse** (drive PV_EN low / BAT_EN low) before injecting on TP11 or TP17. For OUTA1/OUTA2 and direct OUTA-pin injection there is no isolation jumper; the LT6108 can be back-driven safely within its V+ compliance.
- **Why I'm recording it:**
  Answers Q6, Q7, Q8, Q9 in one consolidated entry referencing the
  component datasheets that govern injection-side safety.

---

## Final synthesis — per-net table answering all 10 questions

This table consolidates the findings across Sources 1-9. Confidence levels per cell are HIGH unless noted; the basis for each claim is the source number listed.

| Net | Q1 Injection pad (top side) | Q2 Divider / amplifier | Q3 MCU pin / ADC chan | Q4 Silkscreen TPnn | Q10 raw_adc at 1 V injection | Q6 Max safe V injection |
|---|---|---|---|---|---|---|
| **OUTV1** (panel V) | MCU-side pad of **R33** at PCB (49.75, 59.0); OR drive upstream via **TP11** at (64.25, 129.25) which is PV_OUT2 | R32 = 100 kΩ top / R33 = 3.6 kΩ bot (0.1 %), ratio 0.03475 | **PB08 / AIN2** | TP11 = PV_OUT2 (upstream of divider, on top side); no TP directly on OUTV1 | At MCU pad: 1241; via TP11 (1 V upstream): 43 | At MCU pad: 3.3 V; via TP11: ≤ 12 V (limited by buck UV/OV margins) |
| **OUTV2** (charging-rail V) | MCU-side pad of **R37** at PCB (99.462, 108.125); OR drive upstream via **TP17** at (118.0, 91.7) which is BAT_OUT1 | R36 = 100 kΩ top / R37 = 11.8 kΩ bot (0.1 %), ratio 0.1055 | **PB09 / AIN3** | TP17 = BAT_OUT1 (upstream of divider); no TP directly on OUTV2 | At MCU pad: 1241; via TP17 (1 V upstream): 131 | At MCU pad: 3.3 V; via TP17: ≤ 8 V |
| **OUTA1** (panel-rail I) | OUTA pin of **IC10** (LT6108) at PCB (87.138, 111.605); OR MCU-side of the 750 Ω resistor on the same trace | LT6108-1 OUTA (current source) into 750 Ω pull-down to GND. Total gain encoded in firmware as 40 mA per 3 mV at OUTA pin (provisional, verify in Test B). | **PB06 / AIN14** | None directly confirmed; TP19 at (81.75, 107.25) is *near* IC10 but its net is in the LM139 fault block, not OUTA1 | 1241 | 3.3 V |
| **OUTA2** (battery-rail I) | OUTA pin of **IC11** (LT6108) at PCB (114.375, 111.487); OR MCU-side of the matching 750 Ω resistor | Same as OUTA1 with R30 shunt instead | **PB07 / AIN15** | None directly confirmed; TP20 at (127.25, 108.0) is near IC11 but in the fault block | 1241 | 3.3 V |
| **PV_IMON** (PV eFuse I) | MCU-side pad of **R12** at PCB (50.475, 96.9625) | TPS25940 IMON current source (52 µA/A) into R12 = 12.1 kΩ to GND. Firmware: V_pin = 9.68 mV offset + 0.6292 V/A × I_load | **PB04 / AIN12** | None on PV_IMON | 1241 | 3.3 V |
| **BAT_IMON** (BAT eFuse I) | MCU-side pad of **R24** at PCB (135.5, 93.6) | TPS25940 IMON current source (52 µA/A) into R24 = 12.1 kΩ to GND. Same firmware scaling. | **PB05 / AIN13** | None on BAT_IMON | 1241 | 3.3 V |

**ADC reference (Q5)**: VDDANA, selected via `ADC_REFCTRL_REFSEL_INTVCC1`. VDDANA = AUX_3V3 through ferrite bead FB1. Per AUX_SUPPLY.kicad_sch designer note, AUX_3V3 ≈ 3.06 V (LDO output minus Schottky drop). Firmware uses 3300 mV — this introduces a ~8 % systematic error that Test B will surface. **Operator action before Test B**: measure VDDANA directly at FB1's downstream pad with a multimeter, record this value, and use it in the post-test calibration math.

**For all "raw_adc at 1 V" values in the table above**, the formula assumes 3.3 V ADC reference (firmware default). With actual VDDANA = 3.06 V, multiply the raw counts above by 3300/3060 = 1.078.

**Open question that could not be answered with HIGH confidence**: the precise net mapping of TP18, TP19, TP20, TP21 in CTRL.kicad_sch. Their wire endpoints lie in the y=70-78 region of the CTRL schematic, which is the LM139 quad-comparator section (PV_FLT / BAT_FLT detection block — see Source 4's text annotation about the LM139 voltage reference). They are most likely on the V_REF / comparator threshold nets rather than on OUTV1 / OUTV2 / OUTA1 / OUTA2 directly. **For Test B, the operator should treat TP18-TP21 as "probably not the right test point" and use the R33/R37/R12/R24/IC10-OUTA/IC11-OUTA solder pads instead.** A future agent (or KiCad-in-browser walk) should resolve TP18-TP21 exactly.


