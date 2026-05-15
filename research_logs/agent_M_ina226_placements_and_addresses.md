# Research Log — Agent M: INA226 placements and addresses on PCU V4.1

Purpose: Identify which INA226 chip is panel-side and which is
battery-side on the PCU V4.1 board, the actual I²C address of each
(set by A0/A1 strap pins), the shunt resistor value of each, the I²C
bus they sit on, and the I²C probe points. The downstream decision is
the verification procedure for Test G (INA226 sensor chip verification)
in `src-pds/how_to_test.md`.

Ground rules:
- Prefer official primary sources (the PCB repo schematics, the BOM,
  component datasheets) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-15.

---

## Source 1: testingPCU.csv BOM — INA226 part and shunt resistor rows

- **URL / path:** https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.csv (downloaded via `gh api` to /tmp/eps_pcu/testingPCU.csv)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The BOM groups both INA226 chips on one row (qty 2) and both shunt
  resistors on one row (qty 2). Both shunts are the same value and same
  part. Verbatim row 45 (INA226 row):
  `"IC5,IC8",INA226AIDGSR,PULSE_Library:SOP50P490X110-10N,https://componentsearchengine.com/Datasheets/1/INA226AIDGSR.pdf,"36-V, Bi-Directional, Ultra-High Accuracy, Low-/High-Side, I2C Out Current/Power Monitor w/ Alert",2,,,Texas Instruments,INA226AIDGSR,595-INA226AIDGSR,,,Excluded from BOM,`
  Verbatim row 42 (shunt row):
  `"R49,R50",2mΩ,Resistor_SMD:R_1210_3225Metric,https://fscdn.rohm.com/en/products/databook/datasheet/passive/resistor/chip_resistor/pmr25-e.pdf,Current Sense Resistors - SMD 1210 2mOhm 5% AEC-Q200,2,,,ROHM Semiconductor,PMR25HZPJV2L0,755-PMR25HZPJV2L0,,,,`
  The "Excluded from BOM" flag is a KiCad export-only field (matches
  most other ICs on this board including the EPC2152 buck driver that
  we know is populated); it does NOT mean DNP. The dedicated "DNP" CSV
  column on row 45 is empty, and the schematic symbol property `(dnp no)`
  is set explicitly on both IC5 and IC8 placements (Sources 3 and 4).
- **Proof — why this source is trustworthy here:**
  Row 45 of testingPCU.csv quoted verbatim above lists Reference =
  `IC5,IC8`, Value = `INA226AIDGSR`, Qty = `2`, DNP column = empty,
  Manufacturer_Part_Number = `INA226AIDGSR`. Row 42 quoted verbatim
  lists Reference = `R49,R50`, Value = `2mΩ`, Qty = `2`, Manufacturer_Part_Number
  = `PMR25HZPJV2L0` (a ROHM 2 mΩ ±5% 1210 metal-strip current-sense
  resistor). The DNP column on row 42 is also empty.
- **Confidence: HIGH**
  Primary source (the board's own BOM). The part number, value, package
  and quantity all match the schematic placements and the source-code
  guesses.
- **Implication for our build:**
  Firmware can keep `2000` microohms (= 2 mΩ) as `PANEL_RAIL_SHUNT_RESISTANCE_IN_MICROOHMS`
  and `BATTERY_RAIL_SHUNT_RESISTANCE_IN_MICROOHMS`. Both INA226 chips
  are present on the PCB. The ±5% tolerance class means the worst-case
  measured-current error from the shunt alone is ±5%; this is the
  dominant error budget term and should be quoted in the test report.
- **Why I'm recording it:**
  Confirms the shunt value (question 4) and confirms IC5 and IC8 are
  the two INA226 chips that exist on the V4.1 board (questions 1, 2, 9).

## Source 2: INA226 symbol pin definition in PV.kicad_sch

- **URL / path:** https://github.com/CHESS-mission/eps_pcu_eng/blob/main/PV.kicad_sch (lines 494–688)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The embedded library definition gives the offset of each INA226 pin
  from the symbol's anchor (`at` point). Verbatim pin offsets:
  `pin 1  "A1"    (at 0 0 0)`        — left side, top
  `pin 2  "A0"    (at 0 -2.54 0)`    — left side
  `pin 3  "ALERT" (at 0 -5.08 0)`    — left side
  `pin 4  "SDA"   (at 0 -7.62 0)`    — left side
  `pin 5  "SCL"   (at 0 -10.16 0)`   — left side, bottom
  `pin 10 "IN+"   (at 30.48 0 180)`     — right side, top
  `pin 9  "IN-"   (at 30.48 -2.54 180)` — right side
  `pin 8  "VBUS"  (at 30.48 -5.08 180)` — right side
  `pin 7  "GND"   (at 30.48 -7.62 180)` — right side
  `pin 6  "VS"    (at 30.48 -10.16 180)`— right side, bottom
  Body rectangle is (5.08, 2.54) to (25.4, -12.7) so the connection
  endpoints sit on x = 0 (left) or x = 30.48 (right) and extend inward.
- **Proof — why this source is trustworthy here:**
  PV.kicad_sch line 495 `(symbol "INA226AIDGSR_1_1"` block; lines
  507–687 contain the ten `(pin … (at … …))` blocks quoted above. The
  identical block also appears in BAT.kicad_sch starting line 495.
- **Confidence: HIGH**
  Primary source (the schematic's own embedded symbol). Pin number /
  pin name pairing is unambiguous and matches the TI INA226 datasheet
  10-pin VSSOP pinout.
- **Implication for our build:**
  Each instance of IC5 / IC8 places this symbol at an anchor point.
  Applying the documented Y-flip rule (`abs_x = anchor_x + offset_x`,
  `abs_y = anchor_y − offset_y`) gives the absolute (x, y) of every
  pin on the schematic page, which is what subsequent wire-tracing
  needs to identify each strap, shunt, bus-voltage and I²C connection.
- **Why I'm recording it:**
  Feeds the address-strap trace for questions 3, 7, 8.

## Source 3: IC5 anchor placement in PV.kicad_sch — panel-side INA226

- **URL / path:** https://github.com/CHESS-mission/eps_pcu_eng/blob/main/PV.kicad_sch (lines 3585–3735)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Verbatim symbol placement header (lines 3585–3597):
  ```
  (symbol
      (lib_id "PULSE_Library:INA226AIDGSR")
      (at 64.77 153.67 0)
      (unit 1)
      (exclude_from_sim no)
      (in_bom no)
      (on_board yes)
      (dnp no)
      (fields_autoplaced yes)
      (uuid "1e5a9de6-345d-4743-a548-9e7737cf1218")
      (property "Reference" "IC5" …)
  ```
  IC5 sits on the PV (photovoltaic / solar panel input) schematic sheet
  at anchor (64.77, 153.67), rotation 0, `on_board yes`, `dnp no`.
  Applying the Y-flip rule to Source 2's offsets gives:
  - Pin 1 A1     → abs (64.77, 153.67)
  - Pin 2 A0     → abs (64.77, 156.21)
  - Pin 3 ALERT  → abs (64.77, 158.75)
  - Pin 4 SDA    → abs (64.77, 161.29)
  - Pin 5 SCL    → abs (64.77, 163.83)
  - Pin 6 VS     → abs (95.25, 163.83)
  - Pin 7 GND    → abs (95.25, 161.29)
  - Pin 8 VBUS   → abs (95.25, 158.75)
  - Pin 9 IN−    → abs (95.25, 156.21)
  - Pin 10 IN+   → abs (95.25, 153.67)
- **Proof — why this source is trustworthy here:**
  Exact quote from the file with line numbers. The `(dnp no)` and
  `(on_board yes)` properties are explicit in the file.
- **Confidence: HIGH**
  Primary source. Anchor coordinate is taken verbatim; pin offsets
  come from Source 2.
- **Implication for our build:**
  Confirms the firmware comment that IC5 is the panel-side INA226
  (answers question 1). Gives the absolute pin coordinates needed to
  trace the strap and net wires.
- **Why I'm recording it:**
  Feeds questions 1, 3, 7, 8, 9, 11.

## Source 4: IC5 strap (A0, A1) and pin connectivity in PV.kicad_sch

- **URL / path:** https://github.com/CHESS-mission/eps_pcu_eng/blob/main/PV.kicad_sch (wires at lines 1893, 2083, 2193, 2233, 2343, 2363, 2433, 2553, 2623, 2693, 1903, 1823, 2203, 1743, junctions at lines 1464, 1530, 1494, 1602)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Tracing the wires from each IC5 pin endpoint:
  - **Pin 1 A1** at (64.77, 153.67): wire `(xy 58.42 153.67) (xy 64.77 153.67)` (line 1893) goes left to a junction at (58.42, 156.21) (line 1530) where it meets pin 2.
  - **Pin 2 A0** at (64.77, 156.21): wire `(xy 58.42 156.21) (xy 64.77 156.21)` (line 2083) reaches the same junction. A vertical wire `(xy 58.42 153.67) (xy 58.42 156.21)` (line 2693) shorts A1 to A0.
  - From the (58.42, 156.21) junction a vertical wire `(xy 58.42 156.21) (xy 58.42 168.91)` (line 2193) drops to a second junction at (58.42, 168.91) (line 1464).
  - From that junction `(xy 58.42 168.91) (xy 95.25 168.91)` (line 1903) reaches the column where pin 6 VS lives: pin 6 wire `(xy 95.25 163.83) (xy 95.25 168.91)` (line 2433).
  - The same (58.42, 168.91) junction also extends left `(xy 45.72 168.91) (xy 58.42 168.91)` (line 2343), terminating at hierarchical label `AUX_3V3` at (45.72, 168.91, 180), file lines 2864–2873 verbatim:
    ```
    (hierarchical_label "AUX_3V3"
        (shape input)
        (at 45.72 168.91 180)
    ```
  Therefore **A0 = A1 = VS (pin 6) = AUX_3V3** for IC5.
  - **Pin 3 ALERT** at (64.77, 158.75): `(no_connect (at 64.77 158.75) ...)` line 1607–1610 — explicitly not connected.
  - **Pin 4 SDA** at (64.77, 161.29): wire `(xy 45.72 161.29) (xy 64.77 161.29)` line 2233 → hierarchical label `SDA` at (45.72, 161.29, 180) (lines 2950–2961).
  - **Pin 5 SCL** at (64.77, 163.83): wire `(xy 45.72 163.83) (xy 64.77 163.83)` line 1743 → hierarchical label `SCL` at (45.72, 163.83, 180) (lines 2875–2884).
  - **Pin 7 GND** at (95.25, 161.29): wire `(xy 95.25 161.29) (xy 107.95 161.29)` line 2203 → (107.95, 168.91) → `power:GND` (the chip's ground reference).
  - **Pin 8 VBUS** at (95.25, 158.75): wire `(xy 95.25 158.75) (xy 109.22 158.75)` line 2553 → local label `PV_OUT+` at (109.22, 158.75, 180) (lines 2841–2850).
  - **Pin 9 IN−** at (95.25, 156.21): wire goes to (121.92, 156.21) → through filter resistor R19 (10 Ω, lines 4722–4775) → to local label `PV_OUT-`.
  - **Pin 10 IN+** at (95.25, 153.67): wire goes up to (100.33, 143.51), right to (109.22, 143.51) → through filter resistor R20 (10 Ω, lines 4962+) → to local label `PV_OUT+`.
  The shunt R49 (2 mΩ) is on the top-level sheet between top-level nets `PV_OUT2` (panel/eFuse side) and `BUCK_IN` (buck-converter side). Inside the PV sheet, the local nets `PV_OUT+` and `PV_OUT-` straddle a filtering R-C network feeding IC5's IN+/IN− pins; `PV_OUT+` is the upstream (eFuse-output) side and is also where VBUS senses.
- **Proof — why this source is trustworthy here:**
  All quotes above are direct from PV.kicad_sch with line numbers. The hierarchical label `AUX_3V3` and the no_connect on ALERT pin are unambiguous schematic primitives.
- **Confidence: HIGH**
  Primary source, line-numbered wire trace from pin to terminating label.
- **Implication for our build:**
  Per INA226 datasheet Table 6-2 (Source 5), A1 = VS, A0 = VS → 7-bit
  slave address = `1000101` = **0x45**. The firmware currently configures
  IC5 at 0x40 (`PANEL_INA226_SEVEN_BIT_ADDRESS` in
  `src/drivers/ina226_panel_on_mainboard.c`). This is a guess and is
  WRONG. Firmware must be updated to **0x45**. Until that change is
  made, the i2c-scan diagnostic will list a device responding at 0x45
  but never at 0x40, and the panel-rail bus-voltage / current reads will
  fail.
  IC5's VBUS pin samples the panel-output rail (between the eFuse and
  the shunt), so the firmware's `panel_ina226_read_voltage_mv()` returns
  the voltage at that node, not the bare panel input. Filter resistors
  R19/R20 (10 Ω) sit between the shunt and the IN+/IN- pins per the
  TI datasheet's recommended input filter.
- **Why I'm recording it:**
  Feeds questions 3 (address), 7 (bus net), 8 (shunt net) for the
  panel-side INA226.

## Source 5: INA226 datasheet — Table 6-2 "Address Pins and Slave Addresses"

- **URL / path:** https://www.ti.com/lit/gpn/INA226 (PDF SBOS547B, June 2011 — Revised September 2024), section 6.5.5.1 "Serial Bus Address", page 18, Table 6-2.
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The 7-bit slave address is set by strapping pins A1 and A0, each
  independently tied to one of {GND, VS, SDA, SCL}. The full 16-row
  table (page 18) reads verbatim:
  ```
  A1   A0   SLAVE ADDRESS
  GND  GND  1000000   (0x40)
  GND  VS   1000001   (0x41)
  GND  SDA  1000010   (0x42)
  GND  SCL  1000011   (0x43)
  VS   GND  1000100   (0x44)
  VS   VS   1000101   (0x45)
  VS   SDA  1000110   (0x46)
  VS   SCL  1000111   (0x47)
  SDA  GND  1001000   (0x48)
  SDA  VS   1001001   (0x49)
  SDA  SDA  1001010   (0x4A)
  SDA  SCL  1001011   (0x4B)
  SCL  GND  1001100   (0x4C)
  SCL  VS   1001101   (0x4D)
  SCL  SDA  1001110   (0x4E)
  SCL  SCL  1001111   (0x4F)
  ```
- **Proof — why this source is trustworthy here:**
  Datasheet page 18, table SBOS547B Table 6-2, retrieved directly
  from TI's product page. The table caption in the datasheet reads
  "Table 6-2. Address Pins and Slave Addresses".
- **Confidence: HIGH**
  Primary source from the chip manufacturer.
- **Implication for our build:**
  Combined with Source 4 (IC5 has A1=VS, A0=VS) → IC5 address = **0x45**.
  Combined with Source 6 below (IC8 has A1=VS, A0=SDA) → IC8 address = **0x46**.
  Both differ from the firmware's current guesses (0x40 panel, 0x41 battery).
- **Why I'm recording it:**
  Decodes the strap configuration into the address numbers the firmware
  must use.

## Source 6: IC8 strap (A0, A1) and pin connectivity in BAT.kicad_sch

- **URL / path:** https://github.com/CHESS-mission/eps_pcu_eng/blob/main/BAT.kicad_sch (wires at lines 1696, 1826, 1896, 1946, 1976, 2056, 2066, 2226, 2246, 2266, 2286, 2296, 2306, 2376, 2426, 2446, 2486, 2546, 2686, 2696, 2756; junctions at lines 1473, 1575, 1605, 1611, 1629; labels at 2895, 2914, 2925, 2935, 2956, 3000)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  IC8 anchor (from Source confirmed via line 5292) is (67.31, 152.4),
  rotation 0. Tracing the wires from each pin endpoint:
  - **Pin 1 A1** at (67.31, 152.4): wire `(xy 60.96 152.4) (xy 67.31 152.4)` (line 1826), then vertical `(xy 60.96 152.4) (xy 60.96 167.64)` (line 2686). At (60.96, 167.64) there is a junction (line 1611) where it meets:
    - `(xy 60.96 167.64) (xy 97.79 167.64)` (line 2286) — reaches the IC8 VS pin column (pin 6 wire `(xy 97.79 162.56) (xy 97.79 167.64)` line 2296)
    - `(xy 48.26 167.64) (xy 60.96 167.64)` (line 1896) → hierarchical label `AUX_3V3` at (48.26, 167.64, 180) (lines 3056–3066 verbatim quote):
      ```
      (hierarchical_label "AUX_3V3"
          (shape input)
          (at 48.26 167.64 180)
      ```
    Therefore **A1 = VS (pin 6) = AUX_3V3** for IC8.
  - **Pin 2 A0** at (67.31, 154.94): wire `(xy 67.31 154.94) (xy 63.5 154.94)` (line 1696), then vertical `(xy 63.5 154.94) (xy 63.5 160.02)` (line 2426). At (63.5, 160.02) there is a junction (line 1575) where it meets:
    - `(xy 63.5 160.02) (xy 67.31 160.02)` (line 2546) — reaches the IC8 SDA pin (pin 4)
    - `(xy 48.26 160.02) (xy 63.5 160.02)` (line 1736) → hierarchical label `SDA` at (48.26, 160.02, 180) (lines 2955–2965).
    Therefore **A0 = SDA (pin 4)** for IC8.
  - **Pin 3 ALERT** at (67.31, 157.48): junction at (67.31, 157.48) (line 1641 reading `(at 67.31 157.48)`) but no wires found leading to it that I traced — I did not exhaustively trace ALERT. The IC5 case shows ALERT as `no_connect`; for IC8 it is most likely also unused but I am NOT certain without re-checking. (This pin is not relevant to Test G.)
  - **Pin 4 SDA** at (67.31, 160.02): tied to hierarchical label `SDA` at (48.26, 160.02) via the A0 strap junction (see A0 above).
  - **Pin 5 SCL** at (67.31, 162.56): wire `(xy 48.26 162.56) (xy 67.31 162.56)` (line 2376) → hierarchical label `SCL` at (48.26, 162.56, 180) (lines 3000–3010).
  - **Pin 6 VS** at (97.79, 162.56): tied to AUX_3V3 via the A1 strap chain.
  - **Pin 7 GND** at (97.79, 160.02): wire `(xy 97.79 160.02) (xy 110.49 160.02)` (line 1976) → (110.49, 167.64) → `power:GND` symbol below.
  - **Pin 8 VBUS** at (97.79, 157.48): wire `(xy 97.79 157.48) (xy 111.76 157.48)` (line 2696) → local label `BAT_IN+` at (111.76, 157.48, 180) (lines 2935–2945 verbatim):
    ```
    (label "BAT_IN+"
        (at 111.76 157.48 180)
    ```
  - **Pin 9 IN−** at (97.79, 154.94): wire chain → through a filter resistor at (124.46, 154.94) → local label `BAT_IN-` at (142.24, 154.94, 180).
  - **Pin 10 IN+** at (97.79, 152.4): wire to (102.87, 152.4) → up to (102.87, 142.24) → right to (111.76, 142.24) → through a filter resistor at (124.46, 142.24) → local label `BAT_IN+` at (142.24, 142.24, 180).
  The shunt R50 (2 mΩ) sits on the top-level sheet between the BUCK
  sheet pin `BUCK_OUT` and the BAT sheet pin `BAT_IN` (see Source 7).
  Inside the BAT sheet, the local nets `BAT_IN+` (= BUCK_OUT side) and
  `BAT_IN-` (= BAT_IN side) are the Kelvin sense pair around the shunt.
  IC8 VBUS samples `BAT_IN+` (the buck-output side).
- **Proof — why this source is trustworthy here:**
  All quotes are direct from BAT.kicad_sch with line numbers. The strap
  routing of A0 to SDA on the chip's own SDA pin (rather than a separate
  net) is unambiguous from the shared junction at (63.5, 160.02).
- **Confidence: HIGH for A0, A1, SDA, SCL, VS, GND, VBUS strap traces. MEDIUM for ALERT.**
  ALERT was not exhaustively traced; the test does not require it.
- **Implication for our build:**
  Per INA226 datasheet Table 6-2, A1 = VS, A0 = SDA → 7-bit slave
  address = `1000110` = **0x46**. The firmware currently configures
  IC8 at 0x41 (`BATTERY_INA226_SEVEN_BIT_ADDRESS` in
  `src/drivers/ina226_battery_on_mainboard.c`). This is a guess and is
  WRONG. Firmware must be updated to **0x46**.
  IC8's VBUS pin samples the buck-output side of R50, so
  `battery_ina226_read_voltage_mv()` actually returns the buck-output
  voltage (≈ battery voltage minus IR drop across R50, which at 2 mΩ
  and a few hundred mA is millivolt-scale). In quiescent state with no
  current flowing, the reading equals the battery rail voltage.
- **Why I'm recording it:**
  Feeds questions 3 (address), 7 (bus net), 8 (shunt net) for the
  battery-side INA226.

## Source 7: Top-level shunt placement R49 and R50 in testingPCU.kicad_sch

- **URL / path:** https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_sch (R49 at lines 3883–4000, R50 at lines 4712–4830; PV sub-sheet at 4832–4970; BAT sub-sheet at 4972–5070; BUCK sub-sheet at 5471–5550)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Both shunt resistors sit on the top-level (root) schematic page, NOT
  inside the PV or BAT sub-sheets. The R49 symbol is placed at anchor
  (166.37, 199.39) with rotation 90 and mirror x, `(dnp no)`, `(on_board yes)`:
  ```
  (symbol
      (lib_id "Device:R_Small_US")
      (at 166.37 199.39 90)
      (mirror x)
      …
      (dnp no)
      …
      (property "Reference" "R49" …)
      (property "Value" "2mΩ" …)
      (property "Manufacturer_Part_Number" "PMR25HZPJV2L0" …)
  ```
  R50 at anchor (246.38, 199.39) with the same orientation, `(dnp no)`.
  Tracing the wire endpoints on each shunt:
  - **R49 left pin** (163.83, 199.39) ←wire to (158.75, 199.39) → vertical to (158.75, 204.47) → horizontal `(xy 139.7 204.47) (xy 158.75 204.47)` (line 1727) reaches PV sub-sheet pin `PV_OUT2` at (139.7, 204.47).
  - **R49 right pin** (168.91, 199.39) ←wire to (173.99, 199.39) → vertical to (173.99, 204.47) → horizontal `(xy 173.99 204.47) (xy 193.04 204.47)` (line 2497) reaches BUCK sub-sheet pin `BUCK_IN` at (193.04, 204.47).
  - **R50 left pin** (243.84, 199.39) → through (238.76, 204.47) → `(xy 223.52 204.47) (xy 238.76 204.47)` (line 2507) reaches BUCK sub-sheet pin `BUCK_OUT` at (223.52, 204.47).
  - **R50 right pin** (248.92, 199.39) → through (254, 204.47) → (261.62, 204.47) → (261.62, 196.85) → BAT sub-sheet pin `BAT_IN` at (276.86, 196.85).
- **Proof — why this source is trustworthy here:**
  Direct schematic quotes with line numbers. The hierarchical sheet
  pin coordinates were verified against the sheet symbol blocks (PV
  at lines 4832, BAT at 4972, BUCK at 5471).
- **Confidence: HIGH**
  Primary source.
- **Implication for our build:**
  - **R49 (panel-side shunt) carries the panel current** from the
    PV/eFuse output into the buck-converter input. IC5 measures the
    current flowing INTO the buck stage from the panel; positive
    current = panel sourcing.
  - **R50 (battery-side shunt) carries the battery current** between
    the buck-converter output and the battery rail. IC8 measures the
    current flowing INTO the battery from the buck output; positive
    current = battery charging.
  - For the bench test, the user can verify the panel-side sensor by
    forcing a known current with a bench supply on the PV input
    terminal block, and verify the battery-side sensor by setting a
    known buck duty cycle that pushes a known current into a
    dummy-load on the BAT output.
- **Why I'm recording it:**
  Feeds questions 4 (shunt value, already in Source 1), 7 (which net
  each INA226 measures), 8 (where the shunt sits in the power path).

## Source 8: I²C pull-up resistors R47, R48 and SDA/SCL header J2 in MCU.kicad_sch

- **URL / path:** https://github.com/CHESS-mission/eps_pcu_eng/blob/main/MCU.kicad_sch (pull-up resistor R47 at lines 6343–6398, R48 at lines 4432–4488; J2 connector at lines 5640–5715; SDA/SCL wires at lines 3074, 3194, 3204, 3604, 3714; hierarchical labels lines 4264, 4321)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Two 10 kΩ pull-up resistors sit on the MCU sheet between SDA/SCL and
  AUX_3V3:
  - R47 at anchor (66.04, 105.41, 180), `(dnp no)`, value `10KΩ`, footprint `R_0805`. Pin 1 at (66.04, 107.95) goes via wire to junction (66.04, 114.3) which is the SCL net. Pin 2 at (66.04, 102.87) goes up to (66.04, 99.06) where it joins the AUX_3V3 hierarchical label at (53.34, 99.06, 180).

    Actually correction: re-reading lines 3194 + 3204, **SCL is at y=116.84 (label at (86.36, 116.84)) and SDA is at y=114.3 (label at (86.36, 114.3))** — I had the pairing reversed. Verifying: line 3194 wire `(xy 66.04 114.3) (xy 86.36 114.3)` terminates at label at (86.36, 114.3); line 3204 wire `(xy 76.2 116.84) (xy 86.36 116.84)` terminates at label at (86.36, 116.84). Reading the labels (lines 4072–4090):
    ```
    (label "SDA"  (at 86.36 114.3 0)  …)
    (label "SCL"  (at 86.36 116.84 0) …)
    ```
    So **R47 (at x=66.04) pulls up SDA**, and **R48 (at x=76.2) pulls up SCL**. Both are 10 kΩ tied to AUX_3V3.
  - R48 at anchor (76.2, 105.41, 180), `(dnp no)`, value `10KΩ`, footprint `R_0805`.
  Connector **J2** (4-pin 2.54 mm pin header, lib_id `Connector:Conn_01x04_Pin`, footprint `Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical`) is placed at MCU.kicad_sch anchor (67.31, 74.93). Wires connect its pin connection points (72.39, 72.39), (72.39, 74.93), (72.39, 77.47), (72.39, 80.01) to labels HEATER_SW, POWER_SW, SCL, SDA respectively. So the **board header J2 carries on its 4 pins: pin1=HEATER_SW, pin2=POWER_SW, pin3=SCL, pin4=SDA**. This is the cleanest external probe for the I²C bus.
  Hierarchical labels `SDA` (at 52.07, 114.3) and `SCL` (at 52.07, 116.84) export the bus from the MCU sheet to the PV and BAT sub-sheets.
- **Proof — why this source is trustworthy here:**
  Direct line-numbered quotes from MCU.kicad_sch. BOM row 29 verbatim:
  `"R13,R14,R25,R26,R40,R43,R47,R48",10KΩ,Resistor_SMD:R_0805_2012Metric,...,Thin Film Resistors - SMD 1/8 Wat 10K Ohm 0.1% 0805 AEC-Q200,8,...,YAGEO,RP0805BRD0710KL,...`
  confirms R47 and R48 are 10 kΩ ±0.1% thin-film 0805 parts. BOM row 49 verbatim:
  `J2,Conn_01x06_Pin,Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical,~,"Generic connector, single row, 01x04, script generated",1,...`
  confirms J2 is a 4-pin 2.54 mm header (the BOM Value field
  "Conn_01x06_Pin" is a stale label; the lib_id, the footprint, the
  qty=1 and the description all say 1x04).
- **Confidence: HIGH**
  Primary sources cross-checked. The SDA/SCL ↔ R47/R48 mapping was
  corrected mid-entry; the final mapping is verified by reading the
  label files directly.
- **Implication for our build:**
  - **Pull-up bias**: AUX_3V3 / 10 kΩ = 0.33 mA per line, fine for the
    INA226 chips and the cable lengths on this board. Bus capacitance
    is dominated by traces + 2 INA226 input pins; standard-mode 100 kHz
    has no issue.
  - **I²C bus probe points**: the primary probe is **J2 pins 3 (SCL)
    and 4 (SDA)** at the right edge of the board near the top. Pin 1
    of J2 is HEATER_SW so the pinout is HEATER_SW / POWER_SW / SCL / SDA
    from pin 1 to pin 4. The probe must reference board GND (use any
    GND test point or J3 or the GND silkscreen pad). Avoid driving the
    bus from a separate I²C tool while the MCU firmware is also running
    — disable the MCU's I²C peripheral first.
  - **Pull-up probe**: R47/R48 are 0805 SMD on the top side of the
    board next to the MCU. Their solder pads provide an alternative
    SDA / SCL probe location.
- **Why I'm recording it:**
  Feeds question 6 (pull-up resistor values), question 10 (probe
  points). Updates the bench-test wiring: connect the I²C analyser /
  oscilloscope to J2 pin 3 (SCL) and pin 4 (SDA) with GND on the board.

## Source 9: PCB physical placements in testingPCU.kicad_pcb

- **URL / path:** https://github.com/CHESS-mission/eps_pcu_eng/blob/main/testingPCU.kicad_pcb (IC5 at lines 24479–24494, IC8 at lines 3948–3960, R49 at lines 6748–6760, R50 at lines 33944–33960, J2 at lines 22473–22500; Edge.Cuts lines 35955–36050)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Board outline is a rounded rectangle from (43.25, 54.25) to (143.25, 154.25) in PCB millimetres, i.e., the board is 100 × 100 mm. KiCad's PCB Y axis grows downward, so y=54.25 is the top edge and y=154.25 is the bottom edge; x=43.25 is the left edge and x=143.25 is the right edge.
  Component placements, all on layer `F.Cu` (the top / front side):
  - **IC5** at `(at 49.8 114.9)` rotation 0, layer `F.Cu` (line 24481–24483 verbatim):
    ```
    (footprint "PULSE_Library:SOP50P490X110-10N"
        (layer "F.Cu")
        (uuid "c5db1bde-5745-4bbf-b446-1a0050ac6bf8")
        (at 49.8 114.9)
    ```
    → 6.5 mm in from the left edge, 39.4 mm down from the top edge — **lower-left quadrant of the top side**.
  - **IC8** at `(at 136.95 118.5 180)`, layer `F.Cu` (line 3948–3953 verbatim):
    ```
    (footprint "PULSE_Library:SOP50P490X110-10N"
        (layer "F.Cu")
        (uuid "257bfcac-a1ae-4ce2-9a21-a59642a69a2c")
        (at 136.95 118.5 180)
    ```
    → 6.3 mm in from the right edge, 43.4 mm below the top edge — **lower-right quadrant of the top side**. Rotation 180° (silkscreen text orientation flipped).
  - **R49** at `(at 75.8 129.2)`, layer `F.Cu` — between IC5 and the centre, near the bottom. 1210 size SMD (about 3.2 × 2.5 mm). Silkscreen `R49` text placed 2.28 mm above the component centre.
  - **R50** at `(at 110.5375 129.2)`, layer `F.Cu` — between centre and IC8 along the same y. Same package and silkscreen format.
  - **J2** at `(at 136.5 63.13)`, layer `F.Cu`, pad nets (from the same footprint block):
    - pad 1 (rect / square pad, marks pin 1 on silkscreen) at offset (0, 0), net `/MCU/HEATER_SW`
    - pad 2 at offset (0, 2.54), net `/MCU/POWER_SW`
    - pad 3 at offset (0, 5.08), net `/SCL`
    - pad 4 at offset (0, 7.62), net `/SDA`
    → 6.75 mm in from the right edge, 8.88 mm below the top edge — **upper-right corner of the top side**. The four pins run downward (toward higher y, the bottom edge) with pin 1 at the TOP.
  No `Reference` property indicates a `B.Cu` layer for any of these
  parts, and no TestPoint footprint sits on the SDA or SCL nets on
  any sheet I checked.
- **Proof — why this source is trustworthy here:**
  Direct quotes of the `(footprint … (at x y [rot]))` blocks with the
  line numbers. The `(layer "F.Cu")` line establishes "top side". The
  pad-to-net mapping inside the J2 footprint block (lines 22580–22620
  area) was already quoted in the discussion.
- **Confidence: HIGH**
  Primary source (the PCB file itself).
- **Implication for our build:**
  The user can identify each chip by its silkscreen designator (`IC5`,
  `IC8`) on the top side of the board:
  - **IC5 (panel-side INA226)** is the SOP-10 sitting at the lower-left of the top side, close to the left-edge panel terminal block (J6 or J7 — see agent_E log). The shunt R49 is the larger 1210 SMD just below-right of it.
  - **IC8 (battery-side INA226)** is the SOP-10 sitting at the lower-right of the top side, close to the right-edge battery terminal block. The shunt R50 is the 1210 SMD just below-left of it.
  - **J2 (the I²C header)** is the 4-pin through-hole header in the upper-right corner of the top side. With the board oriented so silkscreen reads upright, pin 1 (HEATER_SW) is at the top, pin 4 (SDA) is at the bottom. A keyed I²C lead can be made by soldering a 4-pin female header strip onto J2 and breaking out only pin 3 (SCL), pin 4 (SDA), plus any nearby GND test point as reference.
- **Why I'm recording it:**
  Feeds questions 10 (probe-point physical location) and 11 (chip
  physical location).

## Source 10: I²C driver source — confirms SERCOM3 on PA22/PA23 mux C at 100 kHz

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\src\drivers\i2c_master_sercom3_pa22_pa23_on_mainboard.h` (lines 1–38)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Verbatim header comment of the firmware's only I²C master driver:
  ```
   * Layer 3 of the sensor abstraction. Bare I²C master on SERCOM3, pads
   * PA22 (SDA) and PA23 (SCL), at 100 kHz. Knows nothing about INA226 or
   * any other sensor.
   *
   * Why SERCOM3:
   *   - SERCOM0 is the OBC UART (PA10 / PA11).
   *   - SERCOM5 is historically reserved for the dev-board debug UART
   *     (PA22 mux D), so we leave it available even on the mainboard build.
   *   - SERCOM3 PAD[0] is on PA22 mux C, PAD[1] on PA23 mux C — the I²C
   *     bus mapping the schematic actually uses.
  ```
- **Proof — why this source is trustworthy here:**
  This is the actual firmware source file. Cross-checks against the
  pin map in `docs/mainboard_pinout_pcu_v4_1.md` which lists PA22 as
  the SDA pin (proof: label `(at 208.28 73.66 180)` in MCU.kicad_sch).
  The mux assignment (SERCOM3 PAD[0]/PAD[1] on PA22/PA23 = mux C) is
  also confirmed in the SAMD21J17D pinmux table (this is well-known
  SAMD21 silicon — PA22 is SERCOM3/PAD[0] mux C OR SERCOM5/PAD[0] mux D).
- **Confidence: HIGH**
  Firmware source code + datasheet-consistent mux mapping.
- **Implication for our build:**
  Confirms question 5: the INA226 chips sit on **SERCOM3** (PA22/PA23,
  mux C), driven at 100 kHz. No change needed.
- **Why I'm recording it:**
  Closes question 5.

---

# Final answer table

For convenience, the per-chip answers below collect the findings from
the entries above. Each row cites the source numbers that back it.

| Question | IC5 (panel side) | IC8 (battery side) | Source(s) |
|---|---|---|---|
| Q1/Q2 Which IC | **IC5** = panel | **IC8** = battery | S1 (BOM), S3 (IC5 in PV.kicad_sch), S6 (IC8 in BAT.kicad_sch) |
| Q3 I²C 7-bit address | **0x45** (A1=VS, A0=VS) | **0x46** (A1=VS, A0=SDA) | S4, S6 (strap traces) + S5 (datasheet table) |
| Q4 Shunt value | **R49 = 2 mΩ ±5%** (ROHM PMR25HZPJV2L0, 1210) | **R50 = 2 mΩ ±5%** (same part) | S1 (BOM row 42), S7 (schematic placement) |
| Q5 I²C bus | **SERCOM3** PA22 (SDA) / PA23 (SCL) mux C, 100 kHz | same | S10 (driver source), `docs/mainboard_pinout_pcu_v4_1.md` |
| Q6 Pull-ups | **R47 = 10 kΩ** on SDA, **R48 = 10 kΩ** on SCL, both to AUX_3V3 | same (one shared bus) | S8 (MCU schematic + BOM row 29) |
| Q7 Bus-voltage net | VBUS pin 8 → local label `PV_OUT+` (eFuse-output / shunt-upstream side); equals panel rail voltage downstream of the TPS25940 eFuse | VBUS pin 8 → local label `BAT_IN+` (buck-output / shunt-upstream side); equals buck output voltage | S4, S6 |
| Q8 Shunt sense net | IN+ pin 10 → `PV_OUT+`; IN− pin 9 → `PV_OUT-`. Both via 10 Ω input filters R19/R20 inside PV.kicad_sch. The shunt itself is **R49** on the top-level page, between top-level nets PV_OUT2 (panel side) and BUCK_IN (load side). | IN+ pin 10 → `BAT_IN+`; IN− pin 9 → `BAT_IN-`. The shunt is **R50** on the top level, between top-level nets BUCK_OUT (source) and BAT_IN (load). | S4, S6, S7 |
| Q9 DNP / populated | `(dnp no)` and `(on_board yes)` in the symbol block, plus BOM row 45 with empty DNP column → **populated** | same → **populated** | S1, S3, S6 |
| Q10 I²C probe point | Pin header **J2 pin 3 (SCL), pin 4 (SDA)**, at PCB (136.5, 63.13), upper-right corner top side. Pin-1 silkscreen rectangle marks pin 1 (HEATER_SW); SDA is the FOURTH pin from that end. Alternate probe pads: R47/R48 0805 pads near the MCU. | same — there is one I²C bus shared by both chips | S8, S9 |
| Q11 Chip physical location | PCB (49.8, 114.9), layer `F.Cu` = top side, lower-left quadrant. SOP-10 package. Silkscreen says `IC5`. | PCB (136.95, 118.5), layer `F.Cu` = top side, lower-right quadrant, rotation 180°. SOP-10. Silkscreen says `IC8`. | S9 |

---

# Bench-test wiring summary for Test G

1. **Power the board** through the normal panel-input terminal block
   (left edge) with a bench supply set to a battery-charging-relevant
   voltage. Make sure the auxiliary 3.3 V rail (AUX_3V3) is up — both
   INA226s and the pull-ups need it.

2. **Connect the I²C analyser / logic-bus probe** to **J2** in the
   upper-right corner of the top side of the board:
   - J2 pin 3 → **SCL**
   - J2 pin 4 → **SDA**
   - J2 pin 1 (HEATER_SW) and pin 2 (POWER_SW) are unrelated digital lines — leave them alone
   - Tie analyser GND to a board GND point (any of the test-point pads on the top side, or the GND pin of the J3 UART header).
   - With the MCU's I²C peripheral disabled, an external master can
     scan; otherwise observe traffic only.

3. **Update the firmware constants before flashing** for the test:
   - `PANEL_INA226_SEVEN_BIT_ADDRESS` in `src/drivers/ina226_panel_on_mainboard.c`: **0x40 → 0x45**.
   - `BATTERY_INA226_SEVEN_BIT_ADDRESS` in `src/drivers/ina226_battery_on_mainboard.c`: **0x41 → 0x46**.
   - Both shunt constants (`*_RAIL_SHUNT_RESISTANCE_IN_MICROOHMS`)
     stay at **2000 µΩ** (2 mΩ); these match the populated R49/R50.

4. **MFG_ID / DIE_ID checks (datasheet Section 6.5.5):**
   Read register 0xFE and expect 0x5449 ("TI") from both chips. Read
   register 0xFF and expect 0x2260. Both chips share the bus, so do
   them sequentially with the firmware's chip-select-by-address logic.

5. **Known-voltage / known-current cross-check:**
   - With the panel input held at, say, 6.0 V on the bench supply,
     read IC5 BUS_V (register 0x02, LSB = 1.25 mV) and confirm it
     matches a multimeter measurement across R49's upstream pad and
     a board GND test point within ±1%.
   - Force a known current (≈ 1 A) through R49 by loading the panel
     rail with a dummy resistor on the buck input side. Compute the
     expected shunt drop (V_shunt = 1 A × 2 mΩ = 2 mV = 800 LSBs at
     2.5 µV/LSB) and compare with IC5 register 0x01. Cross-check the
     calibrated CURRENT register (0x04) with the firmware's current_LSB
     of ~152 µA/bit (= 5 A / 32768).
   - Repeat the same procedure for IC8 with R50, applied between the
     buck output and a bench-supply emulation of the battery.

# Flagged uncertainties

- **IC8 ALERT pin (Pin 3)**: not exhaustively traced. Symbolic
  evidence and the IC5 precedent suggest it is `no_connect`, but I
  did NOT find an explicit `(no_connect (at 67.31 157.48) …)` in
  BAT.kicad_sch in the lines I scanned. Test G does not use ALERT.

- **Exact silkscreen text "IC5" and "IC8"** on the physical board: the
  PCB file marks the `Reference` property as rendered on `F.SilkS`
  layer, so the silkscreen WILL show "IC5" and "IC8" next to each
  chip — unless the silkscreen was modified between the schematic
  revision in this repo's `main` branch and the actual board the user
  has on the bench. If the silkscreen is illegible, the chip can be
  identified by package (SOP-10 small body) plus its position relative
  to the corresponding shunt (R49 next to IC5 on the left half, R50
  next to IC8 on the right half).

- **PV_OUT+ vs PV_OUT2** internal-net naming: the local labels
  `PV_OUT+` / `PV_OUT-` inside PV.kicad_sch and the hierarchical
  labels `PV_OUT1` / `PV_OUT2` exported to the top level are distinct
  named nets that may or may not be electrically the same node. The
  shunt-sense pair IC5 sees (PV_OUT+ / PV_OUT-) is electrically
  centred on R49, with the upstream side equal to PV_OUT2 and the
  downstream side equal to BUCK_IN, but my trace did not connect the
  two naming layers directly. The test does not require this exact
  identification; for Test G it is enough that the user knows IC5
  reads the rail upstream of R49.



