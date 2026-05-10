# Research Log — Agent E: EPS PCU testing board power topology and connector pinouts

Purpose: Determine how the PCU testing board V4.1 (CHESS-mission/eps_pcu_eng,
branch `main`) gets power, where AUX_3V3 actually comes from, what every
connector and header on the board carries (with pin-by-pin mapping for
J1 SWD and J3 UART specifically), and what the safe-startup state of the
buck converter and any load-switching MOSFETs is when the MCU is in reset.
The downstream decision is the exact electrical procedure for first power-on
and first SWD flash session — i.e. how to power only the MCU subsection
without energising the gate driver, the battery, or the solar input, so the
team does not damage the board on its first power-up.

Ground rules:
- Read the schematic files in the GitHub repo CHESS-mission/eps_pcu_eng
  (gh CLI is authenticated). The relevant sub-sheets are:
  AUX_SUPPLY.kicad_sch, BAT.kicad_sch, BUCK.kicad_sch, CTRL.kicad_sch,
  MCU.kicad_sch, PV.kicad_sch, testingPCU.kicad_sch.
- Quote exact (label …), (symbol …), and (wire …) S-expressions as proof.
- Every source gets its own dated entry below, logged before moving on.
- Today is 2026-04-26.

---

## Source 1: testingPCU.kicad_sch — top-level sheet inventory

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_sch` — local cache `C:\temp\eps\testingPCU.kicad_sch` (5841 lines)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Top-level schematic instantiates six hierarchical sheets and places six external screw terminals plus a sense resistor on the top sheet:

  Sheet links (lines 4848 → 5587):
  - `(property "Sheetname" "PV"   ...)` `(property "Sheetfile" "PV.kicad_sch" ...)`
  - `(property "Sheetname" "BAT"  ...)` `(property "Sheetfile" "BAT.kicad_sch" ...)`
  - `(property "Sheetname" "CTRL" ...)` `(property "Sheetfile" "CTRL.kicad_sch" ...)`
  - `(property "Sheetname" "AUX_SUPPLY" ...)` `(property "Sheetfile" "AUX_SUPPLY.kicad_sch" ...)`
  - `(property "Sheetname" "BUCK" ...)` `(property "Sheetfile" "BUCK.kicad_sch" ...)`
  - `(property "Sheetname" "MCU"  ...)` `(property "Sheetfile" "MCU.kicad_sch" ...)`

  Connectors placed on the top sheet (every one of them is a Phoenix Contact MKDS-1 screw terminal, NOT a 0.1″ header):
  - `J4` `(lib_id "Connector:Screw_Terminal_01x02") (at 34.29 128.27 0) (mirror y)` — Phoenix `1727010`, 2-way, 3.81 mm pitch (line 4069+)
  - `J5` `(lib_id "Connector:Screw_Terminal_01x04") (at 39.37 60.96 0) (mirror y)` — Phoenix `1727036`, 4-way (line 3322+)
  - `J6` `(lib_id "Connector:Screw_Terminal_01x03") (at 166.37 214.63 90) (mirror x)` — Phoenix `1727023`, 3-way (line 3569+)
  - `J7` `(lib_id "Connector:Screw_Terminal_01x03") (at 246.38 214.63 90) (mirror x)` — Phoenix `1727023`, 3-way (line 3759+)
  - `J8` `(lib_id "Connector:Screw_Terminal_01x02") (at 351.79 157.48 0)` — Phoenix `1727010`, 2-way (line 3447+)
  - `J9` `(lib_id "Connector:Screw_Terminal_01x02") (at 351.79 132.08 0)` — Phoenix `1727010`, 2-way (line 3200+)
  - Also a 2 mΩ current-sense shunt `R49` between J6 (PV) and the PV sub-sheet (at 166.37 199.39).
- **Confidence: HIGH**
  Reading `(lib_id "Connector:Screw_Terminal_01x0N")` and `(property "Manufacturer_Part_Number" "1727010"|"1727023"|"1727036")` directly from the schematic. There is zero ambiguity: every external connector on the top sheet is a screw terminal, not a header. The only headers (J1 SWD pads + J3 UART) live INSIDE the MCU sub-sheet.
- **Implication for our build:**
  All power and load wires get screwed in with a flat-blade screwdriver — no crimp connectors needed for J4–J9. The MCU sub-sheet contains the only programming/UART access. We have to read each of the 6 sub-sheets, plus the local labels around J4/J5/J6/J7/J8/J9, to identify which terminal is +PV, which is +BAT, etc.
- **Why I'm recording it:**
  Establishes the connector inventory for question B6 and frames the rest of the search.

---

## Source 2: testingPCU.kicad_sch — pin-by-pin nets for J4, J5, J6, J7, J8, J9

- **URL / path:** `https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_sch` — local cache `C:\temp\eps\testingPCU.kicad_sch`
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**

  Sub-sheet hierarchical-pin definitions seen on the top sheet (lines 4866 → 5826):
  - PV sub-sheet placed at top sheet `(at 109.22 188.5184)`, pins:
    `PV_IN input  (at 109.22 196.85 180)`, `PV_OUT1 output (at 139.7 196.85 0)`, `PV_OUT2 output (at 139.7 204.47 0)`, `PV_EN input (at 109.22 204.47 180)`, `AUX_3V3 input (at 124.46 189.23 90)`.
  - BAT sub-sheet placed at top sheet `(at 276.86 189.23)`, pins:
    `BAT_IN input (at 276.86 196.85 180)`, `BAT_OUT1 output (at 307.34 195.58 0)`, `BAT_OUT2 (307.34 203.2)`, `BAT_OUT3 output (at 307.34 210.82 0)`, `BAT_EN input (at 276.86 204.47 180)`, `AUX_3V3 input (at 292.1 189.23 90)`.
  - AUX_SUPPLY sub-sheet placed at top sheet `(at 109.22 40.64)`, pins:
    `AUX_IN input (at 109.22 48.26 180)`, `PDU_3V3 input (at 109.22 55.88 180)`, `PDU_5V input (at 109.22 63.5 180)`, `PDU_12V input (at 109.22 71.12 180)`, `~{AUX_SHDN} input (at 109.22 78.74 180)`, `AUX_3V3 output (at 116.84 93.98 270)`, `AUX_5V output (at 124.46 93.98 270)`, `AUX_12V output (at 132.08 93.98 270)`, `VDDANA output (at 139.7 48.26 0)`, `GNDANA output (at 139.7 55.88 0)`, `VDDIO output (at 139.7 63.5 0)`, `VDDIN output (at 139.7 71.12 0)`, `VDDCORE output (at 139.7 78.74 0)`.
  - BUCK sub-sheet placed at top sheet `(at 193.04 196.85)`, pins:
    `BUCK_IN input (at 193.04 204.47 180)`, `BUCK_HS_IN input (at 193.04 212.09 180)`, `BUCK_LS_IN input (at 193.04 219.71 180)`, `AUX_12V input (at 207.01 196.85 90)`, `BUCK_OUT output (at 223.52 204.47 0)`.

  Pin-by-pin tracing on the top sheet (every step is a literal `(wire (pts (xy ...) (xy ...)))` S-expression in the file):

  - **J4 (Phoenix 1727010, 2-way) at `(at 34.29 128.27 0) (mirror y)` — PV solar input:**
    Pin 1 abs (39.37, 128.27) — wire `(xy 39.37 128.27) (xy 41.91 128.27)` joins `power:PWR_FLAG (at 41.91 128.27)` and continues `(xy 41.91 128.27) (xy 52.07 128.27)` to `(label "PV_RAW" (at 52.07 128.27 180))`.
    Pin 2 abs (39.37, 130.81) — wire `(xy 39.37 130.81) (xy 55.88 130.81)` to `power:PWR_FLAG (at 55.88 130.81)` then `(xy 55.88 134.62) (xy 55.88 130.81)` to `power:GND (at 55.88 134.62)`.
    → **J4 = SOLAR PANEL INPUT: pin 1 = PV_RAW (+), pin 2 = GND (−)**.

  - **J5 (Phoenix 1727036, 4-way) at `(at 39.37 60.96 0) (mirror y)` — PDU power input from external bench supply:**
    Pin 1 abs (44.45, 58.42) — wire `(xy 44.45 58.42) (xy 48.26 58.42)` then `(xy 48.26 58.42) (xy 58.42 58.42)` → label `PDU_3V3` (at 58.42 58.42 180).
    Pin 2 abs (44.45, 60.96) — wire `(xy 44.45 60.96) (xy 60.96 60.96)` → continues to label `PDU_5V (at 71.12 60.96 180)`.
    Pin 3 abs (44.45, 63.5) — wire `(xy 44.45 63.5) (xy 73.66 63.5)` → label `PDU_12V (at 83.82 63.5 180)`.
    Pin 4 abs (44.45, 66.04) — wire `(xy 49.53 66.04) (xy 44.45 66.04)` then `(xy 49.53 69.85) (xy 49.53 66.04)` → `power:GND (at 49.53 69.85)`.
    → **J5 = BENCH SUPPLY INPUT: pin 1 = PDU_3V3, pin 2 = PDU_5V, pin 3 = PDU_12V, pin 4 = GND**. The names PDU_3V3 / PDU_5V / PDU_12V are the rails the on-orbit OBC / PDU board would supply; on the bench, they are external bench-supply rails fed into AUX_SUPPLY.

  - **J6 (Phoenix 1727023, 3-way) at `(at 166.37 214.63 90) (mirror x)` — PV downstream side / +PV terminal with current shunt R49:**
    After applying rotation 90° + mirror x to the symbol-local pin offsets `(-5.08, +2.54), (-5.08, 0), (-5.08, -2.54)`, the three pins are at world positions
    pin 1 (168.91, 209.55), pin 2 (166.37, 209.55), pin 3 (163.83, 209.55).
    Pin 1 — wire `(xy 168.91 209.55) (xy 173.99 209.55)` → `(xy 173.99 204.47) (xy 173.99 209.55)` → `(xy 173.99 199.39) (xy 173.99 204.47)` → `(xy 173.99 199.39) (xy 168.91 199.39)` lands on R49 right pin at (168.91, 199.39).
    R49 sense resistor `(symbol (lib_id "Device:R_Small_US") (at 166.37 199.39 90) (mirror x))`, value `2 mΩ` (Rohm PMR25HZPJV2L0, 1210 SMD, AEC-Q200).
    Pin 2 — wire `(xy 166.37 203.2) (xy 166.37 209.55)` then `(xy 168.91 203.2) (xy 166.37 203.2)` and `(xy 168.91 203.2) (xy 168.91 204.47)` → `power:GND (at 168.91 204.47)`.
    Pin 3 — wire `(xy 163.83 209.55) (xy 158.75 209.55)` → `(xy 158.75 209.55) (xy 158.75 204.47)` → `(xy 158.75 199.39) (xy 158.75 204.47)` → `(xy 158.75 199.39) (xy 163.83 199.39)` to R49 left pin at (163.83, 199.39). The same junction (158.75, 204.47) ties to the PV sub-sheet's `PV_OUT2` pin via `(xy 139.7 204.47) (xy 158.75 204.47)`.
    R49 left side also reaches the PV sub-sheet output `+PV` net (PV_OUT1), via `(xy 139.7 196.85) (xy 151.13 196.85)` at label `+PV`. Note the +PV label and PV_OUT2 are on the SAME side of R49.
    → **J6 = PV-LOAD test connector: pin 1 = PV through R49 (Kelvin-sensed +PV out), pin 2 = GND, pin 3 = PV_OUT2 (= +PV before/at the shunt; current is sensed across R49). For a normal hook-up the user clips +PV to pin 1 (or 3) and GND to pin 2.**

  - **J7 (Phoenix 1727023, 3-way) at `(at 246.38 214.63 90) (mirror x)` — battery connection terminal with charge-current shunt R50:**
    Same pin geometry as J6: pin 1 (248.92, 209.55), pin 2 (246.38, 209.55), pin 3 (243.84, 209.55).
    Pin 1 — wire `(xy 248.92 209.55) (xy 254 209.55)` → `(xy 254 204.47) (xy 254 209.55)` → `(xy 254 199.39) (xy 254 204.47)` → `(xy 254 199.39) (xy 248.92 199.39)` lands on R50 right pin at (248.92, 199.39). Same node continues `(xy 261.62 204.47) (xy 254 204.47)` → `(xy 261.62 196.85) (xy 261.62 204.47)` → `(xy 276.86 196.85) (xy 261.62 196.85)` to BAT sub-sheet pin `BAT_IN`.
    R50 — `(symbol (lib_id "Device:R_Small_US") (at 246.38 199.39 90) (mirror x))`, value `2 mΩ` (R50, also Rohm PMR25HZPJV2L0).
    Pin 2 — wire `(xy 246.38 203.2) (xy 246.38 209.55)` then `(xy 248.92 203.2) (xy 246.38 203.2)` and `(xy 248.92 203.2) (xy 248.92 204.47)` → `power:GND (at 248.92 204.47)`.
    Pin 3 — wire `(xy 243.84 209.55) (xy 238.76 209.55)` → `(xy 238.76 204.47) (xy 238.76 209.55)` → `(xy 238.76 199.39) (xy 238.76 204.47)` → `(xy 238.76 199.39) (xy 243.84 199.39)` lands on R50 left pin at (243.84, 199.39). Same node continues `(xy 223.52 204.47) (xy 238.76 204.47)` to BUCK sub-sheet pin `BUCK_OUT (at 223.52 204.47)`.
    → **J7 = BATTERY TERMINAL with charge-current Kelvin shunt: pin 1 = +BAT (raw battery / BAT_IN; this is what physically connects to the battery's + terminal), pin 2 = GND, pin 3 = BUCK_OUT (i.e. the buck-converter output node, which through R50 charges the battery on pin 1).**

  - **J8 (Phoenix 1727010, 2-way) at `(at 351.79 157.48 0)` — `-BAT` switched output:**
    Pin 1 abs (346.71, 157.48) — wire `(xy 346.71 157.48) (xy 339.09 157.48)` → `(label "-BAT" (at 339.09 157.48 0))`. The same `-BAT` net is BAT sub-sheet pin `BAT_OUT3 (at 307.34 210.82 0)` via wire `(xy 307.34 210.82) (xy 320.04 210.82)` and label `-BAT (at 320.04 210.82 180)`.
    Pin 2 abs (346.71, 160.02) — wire `(xy 346.71 160.02) (xy 339.09 160.02)` → `(xy 339.09 160.02) (xy 339.09 161.29)` → `power:GND (at 339.09 161.29)`.
    → **J8: pin 1 = -BAT (the battery negative terminal, fed from BAT sub-sheet output BAT_OUT3 — i.e. it is downstream of a switching/protection device on the BAT page, NOT the same point as the GND screw on J7), pin 2 = GND.**

  - **J9 (Phoenix 1727010, 2-way) at `(at 351.79 132.08 0)` — `VBAT` switched output:**
    Pin 1 abs (346.71, 132.08) — wire `(xy 346.71 132.08) (xy 339.09 132.08)` → `(label "VBAT" (at 339.09 132.08 0))`. The `VBAT` net is BAT sub-sheet pin `BAT_OUT1 (at 307.34 195.58 0)` via wire `(xy 307.34 195.58) (xy 320.04 195.58)` and label `VBAT (at 320.04 195.58 180)`.
    Pin 2 abs (346.71, 134.62) — wire `(xy 339.09 134.62) (xy 346.71 134.62)` → `(xy 339.09 134.62) (xy 339.09 135.89)` → `power:GND (at 339.09 135.89)`.
    → **J9: pin 1 = VBAT (load-switched, fused +BAT output for downstream loads, fed from BAT_OUT1), pin 2 = GND.**

  Confirmed shunts on the top sheet:
  - `R49 = 2 mΩ` between PV sub-sheet PV_OUT1/+PV and J6 pin 1 (Kelvin sense across PV current).
  - `R50 = 2 mΩ` between BUCK_OUT and J7 pin 1 / BAT sub-sheet BAT_IN (Kelvin sense across battery charge current).
- **Confidence: HIGH**
  Every claim is grounded in a literal wire S-expression and the matching connector / sub-sheet symbol coordinates. The transformation rule (rotation + mirror) was verified against the wire endpoints — the pins land exactly where the schematic wires terminate.
- **Implication for our build:**
  External screw terminals (six of them, all Phoenix Contact MKDS-1 with a 3.81 mm screw pitch) carry HIGH POWER nets — there is NO 3.3 V test point and NO USB connector on the top sheet. To power JUST the MCU subsection without firing up the buck or attaching battery / solar, the only sensible terminal to feed is **J5 pin 1 = PDU_3V3** with **J5 pin 4 = GND**. PDU_3V3 enters the AUX_SUPPLY sub-sheet, where it is converted into AUX_3V3 (the actual MCU rail). DO NOT touch J4 (PV input), J7 (battery + charge shunt), J9 (VBAT load output), or J8 (-BAT load output) for the first power-up. J6 is a PV-stage test connector and must be left disconnected too.
- **Why I'm recording it:**
  Resolves question B6 (full connector inventory + every pin's net) for the top-level sheet. The remaining headers J1 (SWD) and J3 (UART) live in the MCU sub-sheet; AUX_3V3 generation lives in AUX_SUPPLY.

---
