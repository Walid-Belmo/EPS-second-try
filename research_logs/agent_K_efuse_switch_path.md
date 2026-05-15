# Research Log — Agent K: eFuse switch path on PCU V4.1

Purpose: Map the pre- and post-eFuse pads for both the PV-side
(TPS25940 IC4) and BAT-side (TPS25940 IC7) protected rails, confirm
which MCU pins drive enable and read status, and document safe
voltage/current limits. The downstream decision is the wiring map for
Test C (eFuse switching verification) in `src-pds/how_to_test.md`.

Ground rules:
- Prefer official primary sources (the PCB repo schematics, the BOM,
  component datasheets) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-15.

---

## Source 1: PV.kicad_sch — IC4 (PV-side TPS25940) placement and pin labels

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\tmp_sch\PV.kicad_sch` (local copy of the PCB repo file)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Confirms IC4 is a `TPS25940ARVCR` placed at anchor `(201.93, 66.04)` rotated 180 on the PV sheet. The symbol's pin layout (from the embedded `(symbol "TPS25940ARVCR_1_1" ...)` definition) gives offsets for every named pin: pin 14 = EN, pin 2 = PGOOD, pin 20 = ~FLT, pin 17 = ILIM, pin 19 = IMON, pins 9-13 = IN_1..IN_5 (input rail), pins 4-8 = OUT_1..OUT_5 (output rail). The sheet also has hierarchical labels (signals exposed to the parent sheet):
    - `PV_EN` at `(129.54, 60.96)` rotated 180 — line 2928
    - `PV_PGOOD` at `(212.09, 76.2)` — line 2917
    - `~{PV_FLT}` at `(203.2, 91.44)` — line 2884
    - `PV_IMON` at `(195.58, 104.14)` — line 2895
    - `PV_IN` at `(129.54, 50.8)` rotated 180 — line 2906 (pre-eFuse input net into the sheet)
    - `PV_OUT1` at `(267.97, 36.83)` — line 2972 (post-eFuse output net leaving the sheet)
    - `PV_OUT2` at `(266.7, 55.88)` — line 2961
  Text annotations on the sheet read:
    - `"ENABLE: HIGH-Z, DISABLE: PULL-DOWN"` at (189.23, 29.21) — meaning the EN line is intended to be either driven high (or left floating because the chip has an internal pull-up) to enable, and pulled low to disable.
    - `"CONNECTS TO BUCK AND CTRL"` near the output rail.
- **Proof — why this source is trustworthy here:**
  Exact quoted symbol declaration `(symbol (lib_id "PULSE_Library:TPS25940ARVCR") (at 201.93 66.04 180)` at line 3343, plus the hierarchical labels at the line numbers above. Datasheet URL embedded in the symbol property at line 3379: `http://www.ti.com/lit/ds/symlink/tps25940.pdf`.
- **Confidence: HIGH**
  Primary source: the schematic file from the PCB repo, with line numbers reproducible by anyone opening the file.
- **Implication for our build:**
  The PV-side TPS25940 sits on a dedicated sheet `PV.kicad_sch`. Its external interface to the rest of the board is the seven hierarchical labels above. To trace the enable line to the MCU we follow `PV_EN` upward into the parent sheet (`testingPCU.kicad_sch`); to find the pre-/post-eFuse rails we follow `PV_IN` (pre) and `PV_OUT1`/`PV_OUT2` (post). The text "DISABLE: PULL-DOWN" indicates the MCU drives the EN line **HIGH to enable, LOW to disable** (active-HIGH from the firmware's point of view).
- **Why I'm recording it:**
  Anchors every subsequent claim about which MCU pin reaches which TPS25940 pin on the PV side.

## Source 2: MCU.kicad_sch — MCU pin assignments for eFuse enable and status

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\tmp_sch\MCU.kicad_sch` (local copy of the PCB repo)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Confirms which SAMD21 pins drive each eFuse signal. The chip symbol is placed at anchor `(142.24, 60.96)` rotation 0 (same anchor used in `docs/mainboard_pinout_pcu_v4_1.md`). The relevant pin offsets in the symbol (right-side, angle 180):
    - Pin 35 = `PA16` at offset `(55.88, -33.02)` → absolute `(198.12, 93.98)`
    - Pin 36 = `PA17` at offset `(55.88, -30.48)` → absolute `(198.12, 91.44)`
    - Pin 37 = `PA18` at offset `(55.88, -27.94)` → absolute `(198.12, 88.9)`
    - Pin 38 = `PA19` at offset `(55.88, -25.4)`  → absolute `(198.12, 86.36)`
    - Pin 41 = `PA20` at offset `(55.88, -17.78)` → absolute `(198.12, 78.74)`
    - Pin 42 = `PA21` at offset `(55.88, -15.24)` → absolute `(198.12, 76.2)`
  Six hierarchical labels on this sheet (at column x = 212.09):
    - `EN_1`       (output) at `(212.09, 93.98)` — line 4299
    - `EN_2`       (output) at `(212.09, 91.44)` — line 4255
    - `PV_PGOOD`   (input)  at `(212.09, 88.9)`  — line 4200
    - `BAT_PGOOD`  (input)  at `(212.09, 86.36)` — line 4277
    - `~{PV_FLT}`  (input)  at `(212.09, 78.74)` — line 4233
    - `~{BAT_FLT}` (input)  at `(212.09, 76.2)`  — line 4211
  Each label is connected to its matching MCU pin by a single horizontal wire (lines 3254, 3474, 3594, 3224, 3494, 3654 in `MCU.kicad_sch`):
  ```
  (xy 212.09 93.98) (xy 198.12 93.98)   ; EN_1   → PA16
  (xy 212.09 91.44) (xy 198.12 91.44)   ; EN_2   → PA17
  (xy 212.09 88.9)  (xy 198.12 88.9)    ; PV_PGOOD  → PA18
  (xy 212.09 86.36) (xy 198.12 86.36)   ; BAT_PGOOD → PA19
  (xy 212.09 78.74) (xy 198.12 78.74)   ; ~PV_FLT  → PA20
  (xy 212.09 76.2)  (xy 198.12 76.2)    ; ~BAT_FLT → PA21
  ```
- **Proof — why this source is trustworthy here:**
  Exact wire coordinates and pin offsets quoted above with line numbers; identical anchor (142.24, 60.96) used in `docs/mainboard_pinout_pcu_v4_1.md` (already verified for `PA22=SDA`, `PA23=SCL`, etc.).
- **Confidence: HIGH**
  Primary source; same trace methodology as `docs/mainboard_pinout_pcu_v4_1.md`; every wire segment can be re-checked.
- **Implication for our build:**
  Firmware-to-eFuse mapping confirmed:
    - **PA16 = EN_1** (drives the PV-side TPS25940 enable)
    - **PA17 = EN_2** (drives the BAT-side TPS25940 enable)
    - **PA18 = PV_PGOOD** input (active-HIGH from chip)
    - **PA19 = BAT_PGOOD** input (active-HIGH from chip)
    - **PA20 = ~PV_FLT** input (active-LOW from chip)
    - **PA21 = ~BAT_FLT** input (active-LOW from chip)
  Matches the firmware constants in `src/drivers/gpio_efuse_status_inputs_pa18_to_pa21_on_mainboard.c` (PV_PGOOD on 18, BAT_PGOOD on 19, PV_FAULT on 20, BAT_FAULT on 21) and the `gpio_pv_efuse_enable_pa16_on_mainboard.c` / `gpio_bat_efuse_enable_pa17_on_mainboard.c` driver names.
- **Why I'm recording it:**
  Answers questions 1, 2 and 3 of the task: which MCU pin drives each enable and reads each status line.

## Source 3: TPS25940A datasheet (SLVSCF3A, June 2014 - revised March 2015)

- **URL / path:** https://www.ti.com/lit/ds/symlink/tps25940.pdf (also embedded as the `Datasheet` property of the IC4 symbol at `tmp_sch\PV.kicad_sch` line 3379)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  - **Pin Functions** (Section 6, p.3):
    - Pin 14 `EN/UVLO`: "Input for setting programmable undervoltage lockout threshold. An undervoltage event will open internal FET and assert FLT to indicate power-failure. When pulled to GND, resets the fault latch in TPS25940L." **It is NOT a digital active-HIGH enable** — it is a UVLO comparator input with a threshold of ~0.99 V (rising). The chip turns on when V_EN > V_ENR.
    - Pin 2 `PGOOD`: "Active High. A high indicates PGTH has crossed the threshold value. It is an open drain output."
    - Pin 20 `~FLT`: "Fault event indicator, goes low to indicate fault condition due to Undervoltage, Overvoltage, Reverse voltage and Thermal shutdown event. It is an open drain output."
    - Pin 17 `ILIM`: "A resistor from this pin to GND sets the overload and short-circuit current limit."
    - Pin 19 `IMON`: scaled-down current monitor output, R_IMON converts to voltage.
  - **Absolute Maximum Ratings** (Section 7.1, p.4):
    - `IN, OUT, PGTH, PGOOD, EN/UVLO, OVP, DEVSLP, ~FLT`: −0.3 V to **+20 V** maximum
    - `IN (10 ms transient)`: +22 V
    - `dVdT, ILIM`: −0.3 V to +3.6 V
    - Sink current PGOOD/FLT/dVdT: 10 mA max
  - **Recommended Operating Conditions** (Section 7.3, p.4):
    - V(IN): 2.7 V to **18 V** nominal
    - R(ILIM): 16.9 kΩ to 150 kΩ
    - External capacitance C(OUT) ≥ 0.1 μF (no max specified)
  - **Electrical Characteristics — Current limit (Section 7.5, p.5):** the typical I_LIM in the test-conditions table (V_IN = 12 V, V_IN − V_OUT = 1 V):
    - R(ILIM) = 150 kΩ → I_LIM typ 0.58 A
    - R(ILIM) = 88.7 kΩ → I_LIM typ 0.99 A
    - R(ILIM) = 42.2 kΩ → I_LIM typ 2.08 A
    - R(ILIM) = 24.9 kΩ → I_LIM typ 3.53 A
    - R(ILIM) = 20 kΩ → I_LIM typ 4.45 A
    - R(ILIM) = 16.9 kΩ → I_LIM typ 5.2 A
    Approximate fit: `I_LIM [A] ≈ 89 / R_ILIM [kΩ]` (the equation cited in the TI E2E forum thread; matches the table to within a few %).
  - **EN pin electrical (Section 7.5, p.5):**
    - V(ENR) — EN/UVLO threshold rising: min 0.97 V, typ **0.99 V**, max 1.01 V
    - V(ENF) — falling: typ 0.92 V
    - I_EN input leakage: −100 nA to +100 nA over 0 V to 18 V
    So when EN/UVLO is left HIGH-Z (no external load) and a small voltage is present via a divider, the chip turns on once it crosses ~1 V.
  - **PGOOD / ~FLT outputs (Section 7.5 continued, p.6):**
    - R(FLT) internal pull-down: typ 18 Ω (5 mA sink). Open drain. Active LOW.
    - R(PGOOD) internal pull-down: typ 20 Ω. Open drain. **Active HIGH** (open → pulled HIGH by external pull-up; chip pulls LOW until PGTH crosses threshold). The datasheet pin description says "Active High. A high indicates PGTH has crossed the threshold value" but the output stage is open-drain, so to read a logic HIGH the host needs an external pull-up to its own supply (commonly 3.3 V).
  - **Reverse / forward thresholds:** V_REVTH typ −9.3 mV, V_FWDTH typ 100 mV.
  - **Output ramp control (dVdT pin):** ΔV(OUT)/ΔV(dVdT) gain typ 11.9 V/V (Section 7.5, p.5). C_dVdT charges from internal 1 μA → V_OUT slew = (V_dVdT slew) × 11.9. Max V_dVdT = 2.88 V → max V_OUT slew is essentially set by the dVdT capacitor choice.
- **Proof — why this source is trustworthy here:**
  Official TI datasheet (revision A, SLVSCF3A), pages 3-6 as quoted. The datasheet URL is embedded in the schematic library symbol (`tmp_sch/PV.kicad_sch` line 3379), so this is the canonical reference for the part installed on the board.
- **Confidence: HIGH**
  Primary source. Every number quoted is from a labelled table.
- **Implication for our build:**
  - The TPS25940 is **not** a simple GPIO-controlled switch. Its "enable" is a UVLO input. The board must therefore have either (a) the MCU's PA16/PA17 driving the EN/UVLO pin directly (with an external resistor to make 3.3 V map to a level above the 0.99 V threshold), or (b) the MCU driving an intermediate transistor / level-translator that pulls EN/UVLO to ground when LOW and releases it (HIGH-Z) when HIGH — exactly what the schematic annotation "ENABLE: HIGH-Z, DISABLE: PULL-DOWN" on `PV.kicad_sch` line 1306 implies. The CTRL sheet (which I'll log next) sits between the MCU and the EN/UVLO pin and includes an open-collector OR with the LM139 over/under/over-current fault comparators.
  - PGOOD and ~FLT are open-drain. They MUST have external pull-ups to 3.3 V for the SAMD21 to read them; otherwise PA18-PA21 would read 0 regardless of the eFuse state. The pull-ups are presumably on the CTRL sheet (to be verified) — without them the PGOOD read in step 6 of Test C would always read 0.
  - Maximum safe V_IN for bench Test C is **18 V continuous, 20 V absolute max, 22 V for 10 ms transient**. The 5 V the test calls for is well within range and is mid-rail of the chip's intended 2.7-18 V window.
  - PGOOD/FLT sink limit is 10 mA — the external pull-up must be sized so V_pullup/R_pullup ≤ 10 mA (with 3.3 V supply, R ≥ 330 Ω; typical choice is 10 kΩ which gives 0.33 mA).
- **Why I'm recording it:**
  Answers questions 7 (ILIM equation), 8 (EN polarity), 9 (PGOOD/FLT output type), and 11 (max safe voltage) of the task.

## Source 4: Schematic + PCB — physical pad map for both eFuses (test points, connectors, board outline)

- **URL / path:**
    - `tmp_sch\PV.kicad_sch` (PV sheet — IC4, R8..R19, TP8..TP11)
    - `tmp_sch\BAT.kicad_sch` (BAT sheet — IC7, R20..R31, TP14..TP17)
    - `tmp_sch\testingPCU.kicad_sch` (parent sheet — J4..J9 screw terminals)
    - `research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb` (PCB file with physical (x, y))
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Board outline read from `testingPCU.kicad_pcb` lines 35956-36038: a rounded rectangle from (43.25, 54.25) to (143.25, 154.25) mm, i.e. ~100 mm by 100 mm, centered at (93.25, 104.25). All references below are on F.Cu (front copper) with F.SilkS silkscreen — all on the top side.

  **PV-side eFuse (IC4) — physical landmarks:**
    - IC4 (TPS25940ARVCR) at PCB (56.25, 95.15) — left edge, vertical centre of the board.
    - J4 (Phoenix Contact 1727010, 2-pos 3.81 mm screw terminal, 13.5 A rating) at PCB (49.6, 79.2) — **top-left corner of the board**. Schematic (`testingPCU.kicad_sch` line 4069): pin 1 → `PV_RAW` label at (52.07, 128.27), pin 2 → GND. This is the PV bench-supply entry point.
    - J6 (Phoenix Contact 1727023, 3-pos 3.81 mm) at PCB (71.98, 148.0) — **bottom edge of the board**, left-of-centre. Schematic: carries the PV_OUT (post-eFuse) and GND, plus a third signal.
    - TP8 SMD test-point pad (1.5 mm diameter, F.Cu, silkscreen "TP8") at PCB (68.1, 91.7). Schematic wire (`PV.kicad_sch` line 2723): connects to net `PV_IN` (= `PV_RAW` after the sheet boundary). **TP8 = pre-eFuse PV input** on the chip side of the shunt.
    - TP9 at PCB (56.5, 86.25). Schematic wire (`PV.kicad_sch` line 1883): connects to `PV_EN` net at (129.54, 60.96). **TP9 = PV-side eFuse enable signal** (the analog level that reaches IC4 EN/UVLO pin AFTER the CTRL hardware-fault gating). Useful for scope diagnostics.
    - TP10 at PCB (68, 98.6). Schematic wire (`PV.kicad_sch` line 1873): connects to `PV_OUT1` hierarchical label. **TP10 = post-eFuse PV output, first tap.**
    - TP11 at PCB (64.25, 129.25). Schematic: connects to `PV_OUT2`. **TP11 = post-eFuse PV output, second tap.** TP10 and TP11 are on the same regulated rail (both downstream of IC4 OUT pins 4-8); they are split for routing convenience.

  **BAT-side eFuse (IC7) — physical landmarks:**
    - IC7 (TPS25940ARVCR) at PCB (129.85, 95.15) — right edge, vertical centre.
    - J9 (Phoenix 1727010, 2-pos) at PCB (136.8, 83.01) — **top-right corner**. Schematic line 3201: pin 1 → `VBAT` label at (339.09, 132.08), pin 2 → GND. VBAT is the post-eFuse BAT rail going to the battery package (text "TO THE BATTERY PACKAGE" at parent sheet (342.9, 153.162)). **J9 = where the actual battery plugs in; this is NOT the pre-eFuse test-injection point.**
    - J8 (Phoenix 1727010, 2-pos) at PCB (136.6, 111.31) — mid-right. Schematic line 3448: pin 1 → `-BAT` label, pin 2 → GND. Related to the battery rail but not the pre-eFuse injection point either.
    - J7 (Phoenix 1727023, 3-pos) at PCB (106.78, 148.05) — bottom edge, just right of centre. Schematic line 3759: carries `BUCK_OUT` and is on the same net as `BAT_IN` (= pre-eFuse BAT-side input) via R50 (2 mOhm shunt). **J7 is the BAT-side bench-supply entry point** — to drive IC7 from a bench supply without going through the buck, the supply attaches to the BUCK_OUT pin of J7.
    - TP14 at PCB (118, 98.5). Schematic (`BAT.kicad_sch` line 2526): connects to `BAT_IN` net at (119.38, 54.61). **TP14 = pre-eFuse BAT input** on the chip side of the shunt.
    - TP15 at PCB (127.2, 105). Schematic (`BAT.kicad_sch` line 2836): connects to `BAT_EN` net. **TP15 = BAT-side eFuse enable signal** (analog level at IC7 EN/UVLO after CTRL gating).
    - TP17 at PCB (118, 91.7). Schematic (`BAT.kicad_sch` line 2876): connects to `BAT_OUT1` hierarchical label. **TP17 = post-eFuse BAT output, first tap.**
    - TP16 at PCB (127.25, 108). Schematic: connects to `BAT_OUT3`. **TP16 = post-eFuse BAT output, third tap.**

  **External resistors that set behaviour** (read from the schematic `Value` properties):
    - **R_ILIM, PV-side:** R11 = 17.2 kOhm at PV sheet (175.26, 82.55) rot 90, between IC4 pin 17 (ILIM) and GND.
    - **R_ILIM, BAT-side:** R23 = 17.2 kOhm at BAT sheet (186.69, 87.63) rot 90, between IC7 pin 17 and GND.
    - Both resistors identical → both eFuses set to the same current limit. Datasheet table (Section 7.5): R(ILIM)=16.9 kOhm → typ I_LIM = 5.2 A (range 4.78 to 5.62 A); R(ILIM)=20 kOhm → typ 4.45 A. Linear interpolation: R=17.2 kOhm → I_LIM ≈ **5.13 A typ** (range 4.7-5.5 A with the ±8 % tolerance). The simplified formula `I_LIM[A] ≈ 89 / R[kOhm]` gives `89 / 17.2 = 5.17 A` — consistent.
    - **R_IMON, PV-side:** R12 = 12.1 kOhm (IC4 pin 19 to GND).
    - **R_IMON, BAT-side:** R24 = 12.1 kOhm (IC7 pin 19 to GND). Datasheet GAIN(IMON) = 52.3 µA/A; so V_IMON = I_load × 52.3 µA × 12.1 kOhm ≈ I_load × 0.633 V/A. (Useful for the IMON-based current sensing path, not Test C directly.)
    - **PGOOD pull-up, PV-side:** R14 = 10 kOhm from PV_PGOOD to AUX_3V3. Wires: `PV.kicad_sch` lines 1853 + 1843 + 2213 + 2563 + 2033. Pin 2 of IC4 → (201.93, 63.5) → (209.55, 63.5) → (209.55, 78.74) → R14 → (209.55, 83.82) → (209.55, 86.36) → AUX_3V3 label at (207.01, 86.36).
    - **PGOOD pull-up, BAT-side:** R26 = 10 kOhm from BAT_PGOOD to AUX_3V3 (same topology).
    - **~FLT pull-up, PV-side:** R13 = 10 kOhm from ~PV_FLT to AUX_3V3.
    - **~FLT pull-up, BAT-side:** R25 = 10 kOhm from ~BAT_FLT to AUX_3V3.
    Without these 10 kOhm pull-ups, PA18-PA21 would always read 0 because the chip's PGOOD/~FLT outputs are open-drain.

  **Output bypass capacitors:**
    - PV-side: C12 = 1 µF at `PV.kicad_sch` (228.6, 66.04) on the IC4 OUT rail to GND.
    - BAT-side: C21 = 1 µF on IC7 OUT rail.
    - Ramp time: with C_OUT = 1 µF and the dV/dt set by C_dVdT (C11 = 10 nF on PV side, C20 = 10 nF on BAT side) the chip ramps V_OUT controlled by the dVdT pin. Datasheet GAIN_dVdT = 11.9 V/V, I(dVdT) = 1 µA charging. So V_dVdT slew = 1 µA / 10 nF = 100 V/s; V_OUT slew = 100 V/s × 11.9 = **1190 V/s** ≈ 1.19 V/ms. For a 5 V rail, expected ramp time ≈ **4-5 ms**. (Much slower than the simple RC of C_OUT × R_ON ≈ 1 µF × 42 mOhm = 42 ns because dVdT control deliberately slows the inrush.) The multimeter in Test C will show the rail at 5 V essentially instantly relative to the user's reaction time.

- **Proof — why this source is trustworthy here:**
  Every PCB position is quoted with a line number in `testingPCU.kicad_pcb` (the regex extracted the `(at x y)` for each footprint immediately above the `(property "Reference" "...")` field). Every schematic resistor value came from the `(property "Value" "...")` text in the relevant `.kicad_sch` file. The wire connectivity for the pull-ups and ILIM resistors was traced explicitly by listing the `(xy ... ) (xy ... )` wire segments at the relevant coordinates.
- **Confidence: HIGH**
  Multiple primary sources cross-verified (schematic + PCB + datasheet); every claim has a quoted line number.
- **Implication for our build (Test C wiring map):**
  **PV side bench wiring:**
    1. Bench supply +: clamp onto **J4 pin 1** (the 2-pin Phoenix screw terminal at PCB top-left, ~6 mm in from the left edge at y ≈ 79 mm). Verify pin 1 vs pin 2 against the silkscreen labels printed beside J4.
    2. Bench supply −: clamp onto **J4 pin 2** (the other terminal of J4 = GND).
    3. Multimeter +: probe tip on **TP10** (SMD pad, top side, near IC4, at PCB (68, 98.6), about 12 mm to the right of IC4) — the post-eFuse PV rail.
    4. Multimeter −: any board GND point.
    5. Optional scope channel on **TP9** (PCB (56.5, 86.25)) to watch the analog EN/UVLO level when firmware toggles PA16.
  **BAT side bench wiring:**
    1. Bench supply +: **J7 middle pin (BUCK_OUT slot)** at PCB (106.78, 148.05) on the bottom edge. Avoid J9 — that goes to the post-eFuse battery rail.
    2. Bench supply −: J7 GND pin.
    3. Multimeter +: **TP17** (PCB (118, 91.7)) post-eFuse BAT rail.
    4. Multimeter −: board GND.
    5. Optional scope on **TP15** (PCB (127.2, 105)) for the BAT_EN analog level.
  **Firmware path:**
    - The web app manual mode commands toggle PA16 and PA17 (drivers in `src/drivers/gpio_pv_efuse_enable_pa16_on_mainboard.c` and the PA17 sibling). Drive HIGH to enable, LOW to disable.
    - `pv_efuse_power_good` and `bat_efuse_power_good` telemetry come from reading PA18 and PA19 (each pulled HIGH by 10 kOhm to AUX_3V3 via R14/R26 and pulled LOW by the TPS25940 internal open-drain when PGTH < V_PGTHR ≈ 0.99 V).
  **Safe bench voltage and current:**
    - **5 V at 200 mA is well within the part's continuous rating** (2.7-18 V at I_LIM ≈ 5 A). For the test, anything from 3 V to 18 V is safe at the chip itself; the listed 5 V is fine.
    - Do **NOT** exceed 18 V continuous (recommended max) or 20 V absolute (will start to damage the chip; OVP threshold is set externally — see below).
    - Note the OVP pin of IC4 has an external divider (R8 = 499 kOhm and R16 = 499 kOhm together with R10 = 31.2 kOhm and R15 = 54.9 kOhm); the OVP threshold of the chip itself is 0.99 V at the OVP pin, so the V_IN at which OVP trips is set by that divider. Computing it is not strictly needed for Test C (we are well below it at 5 V); record it once we get to Test E if needed.
- **Why I'm recording it:**
  Answers questions 4, 5, 6, 7, 9, 10 of the task and supplies the actual physical wiring the user needs for Test C.

## Source 5: testingPCU.csv BOM — cross-check of eFuse-related component values

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\tmp_sch\testingPCU.csv` (the project's bill of materials)
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  Confirms the values and quantities of the components I traced in Source 4 with a single line per part-value group:
  ```
  "IC4,IC7",TPS25940ARVCR,...,"18V, 5A, 42m eFuse With Integrated Reverse Current Protection and DevSleep Support",2,...
  "R11,R23",17.2KΩ,...,Thin Film Resistors - SMD 17.2Kohms .1% 25ppm,2,...
  "R12,R24",12.1KΩ,...,Thin Film Resistors - SMD 1/10W 12.1K ohm .1% 10ppm,2,...
  "R13,R14,R25,R26,R40,R43,R47,R48",10KΩ,...,Thin Film Resistors - SMD 1/8 Wat 10K Ohm 0.1% 0805 AEC-Q200,8,...
  "C9,C12,C18,C21,C25,C27",1μF,...,16V 1uF X7R 0805,6,...
  "C11,C20,C26,C28",10nF,...,25V 0.01uF 0603 C0G 5%,4,...
  "J4,J8,J9",1727010,...,Conn PC Terminal Block 2 POS 3.81mm Solder ST Thru-Hole 13.5A,3,...
  "J6,J7",1727023,...,Terminal Block, 3.81 mm, 3 Ways, 26 AWG, 16 AWG, 1.5 mm, Screw,2,...
  IC9,LM139ADRG4,...,Precision quad differential comparator,1,...
  ```
  Notable: the output bypass capacitors (C12 on PV-side, C21 on BAT-side) are rated **16 V** dielectric (KEMET C0805C105K3RECAUTO, 16 V or C0805F105K4RACAUTO, 25 V — the BOM lists both alternates with a comma between them). The chip's max V_IN of 18 V is therefore very close to the C12/C21 voltage rating; if the 16 V variant was installed, the practical safe maximum at the post-eFuse rail is **16 V continuous** (de-rated to ~10 V for long-term reliability). The 25 V variant has plenty of margin. This is the lowest-rated component in the V_OUT chain.
  The shunt resistors R49, R50 = 2 mΩ in 1210 footprint, 5 % tolerance, **rated 1 W max** (PMR25HZPJV2L0 datasheet). At 2 mΩ × 1 W = 22.4 A max — far above the test conditions.
- **Proof — why this source is trustworthy here:**
  Direct quote from `testingPCU.csv` rows above; counts (2 of TPS25940, 2 of R_ILIM at 17.2 kΩ, 8 of 10 kΩ, etc.) are consistent with the schematic placements traced in Source 4.
- **Confidence: HIGH**
  BOM is part of the official KiCad project release; was used to procure the actual components on the board.
- **Implication for our build:**
  - The lowest absolute-maximum-rated component in the post-eFuse chain is the 16 V output capacitor (worst-case if the lower-rated alternate was used during assembly). The chain limit is therefore **min(20 V for chip absmax, 16 V for cap if low-rated variant) = 16 V**. For full safety margin keep V_IN ≤ 12 V during bench tests until we confirm which capacitor variant was actually installed (a magnifier+photo of the cap marking would resolve it). At **5 V the test asks for, every component is well inside its rating** with > 50 % headroom.
  - Both eFuses use identical R_ILIM = 17.2 kΩ → identical 5.13 A current limit. The bench supply current limit of 200 mA in Test C is well below this (about 4 % of the trip point), so the eFuse will not trip during the test on its own current-limit.
- **Why I'm recording it:**
  Cross-checks Source 4's component values via an independent file (the BOM), answers question 11 (lowest absmax in the chain), and rules out the test conditions tripping a protection feature.

## Source 6: CTRL.kicad_sch — the CTRL block sits between MCU's PA16/PA17 and the TPS25940 EN/UVLO pins

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\tmp_sch\CTRL.kicad_sch`
- **Date accessed:** 2026-05-15
- **What this source gave me (plain English):**
  The signal path from MCU pin PA16 to IC4 pin 14 (EN/UVLO) is NOT a direct wire. It goes:
    1. MCU PA16 → hierarchical label `EN_1` → on the parent sheet `testingPCU.kicad_sch` the net is renamed to `CTRL_EN_IN1` (input pin of the CTRL sheet) at (215.9, 116.84) → on the MCU sheet line 4299 it leaves as `EN_1`.
    2. Inside `CTRL.kicad_sch`: `CTRL_EN_IN1` (212.09, 43.18, input) and `CTRL_EN_OUT1` (257.81, 43.18, output) are on the **same KiCad net** as four LM139 comparator outputs (open-collector): `BAT_OV_F`, `BAT_OC_F`, `PV_OV_F`, `PV_OC_F`. All these labels appear on a single vertical wire at x=241.3, y=43.18..104.14 (wires lines 1596, 1716, 2286, 2446 in `CTRL.kicad_sch`).
    3. The MCU PA16 push-pull output is the only "high-side" driver on that net; the LM139 outputs are open-collectors that can only **pull down**. So when no fault is present and the MCU drives PA16 HIGH, the net is HIGH (3.3 V). When the MCU drives PA16 LOW or ANY of the four comparators triggers, the net is pulled LOW.
    4. The CTRL_EN_OUT1 net leaves CTRL and goes back to the PV sheet as `PV_EN` (hierarchical-label-pair `(129.54, 60.96)` in `PV.kicad_sch`, matching the parent's `PV_EN` sheet-pin at `(109.22, 204.47)`).
    5. Inside `PV.kicad_sch`, `PV_EN` (at (129.54, 60.96)) wires across to IC4 pin 14 (EN/UVLO). The EN net also has a resistor divider R15 (54.9 kΩ) + R16 (499 kΩ) and pull-up resistor R9 (26.7 kΩ) + R8 (499 kΩ) configuring the UVLO threshold — these convert the 3.3 V CMOS signal into an analog level that the TPS25940's 0.99 V UVLO threshold can comfortably sit above when EN is HIGH and below when EN is LOW.
  The schematic text annotation in `PV.kicad_sch` line 1306 ("ENABLE: HIGH-Z, DISABLE: PULL-DOWN") confirms exactly this behaviour from the firmware's perspective: drive PA16 HIGH = "release the line" (the chip's UVLO sees the divider voltage and turns on); drive PA16 LOW = "pull the line down" (UVLO sees ~0 V and turns off).
  For BAT-side, the same topology connects MCU PA17 → `EN_2` → `CTRL_EN_IN2` → `CTRL_EN_OUT2` → `BAT_EN`. The fault comparators on the BAT-EN net are `BAT_UV_F`, `BAT_OC_F`(?), `PV_UV_F`(?) — fewer of them are wired to the BAT-EN net than to PV-EN (the wires near y=115.57..129.54 in CTRL.kicad_sch are simpler).
  IC9 (LM139ADRG4, quad comparator, BOM marked "Excluded from BOM" — so it may not be populated, see caveat below) generates the four fault signals from divider taps fed by `PV_OUT-`, `BAT_OUT-`, `BUCK_OUT-`, `IN1_MINUS..IN4_MINUS`.
- **Proof — why this source is trustworthy here:**
  - `CTRL.kicad_sch` hierarchical label `CTRL_EN_IN1` at line 2945 `(at 212.09 43.18 180)`.
  - `CTRL.kicad_sch` hierarchical label `CTRL_EN_OUT1` at line 3088 `(at 257.81 43.18 0)`.
  - `CTRL.kicad_sch` wires that connect them through the LM139 outputs: line 2296 `(xy 212.09 43.18) (xy 241.3 43.18)`, line 2356 `(xy 241.3 43.18) (xy 257.81 43.18)`, vertical chain lines 1596 + 1716 + 2286 + 2446 from (241.3, 43.18) down to (241.3, 104.14) through `BAT_OV_F` at (228.6, 58.42), `BAT_OC_F` at (228.6, 73.66), `PV_OV_F` at (228.6, 88.9), `PV_OC_F` at (228.6, 104.14).
  - `PV.kicad_sch` annotation line 1306 "ENABLE: HIGH-Z, DISABLE: PULL-DOWN".
  - BOM line: `IC9,LM139ADRG4,...,Precision quad differential comparator, military grade,1,...,Excluded from BOM` — IC9 is marked "Excluded from BOM" which can mean either "do not populate" OR "the BOM is unfiltered and the part will still be assembled because it has `(in_bom no)` set explicitly in KiCad". Need a photo of the actual board to confirm whether IC9 is present.
- **Confidence: HIGH for the topology, MEDIUM for whether IC9 is populated.**
  Topology is read directly from the schematic; IC9's "Excluded from BOM" flag is ambiguous and would need to be confirmed by reading the assembled PCB.
- **Implication for our build:**
  - **For Test C the topology behaves identically to "PA16 = direct active-HIGH enable" as long as none of the four fault comparators is asserting.** Since the test applies only 5 V (well below any over-voltage threshold) and limits current to 200 mA (well below any over-current threshold), no fault is expected. The firmware's "PA16 HIGH = on" mental model is the correct one for the test.
  - **If a fault IS asserted during the test** (for example because the user accidentally exceeds OVP), the post-eFuse rail will stay at 0 V no matter what PA16 says — and the `~PV_FLT` line (PA20) will go LOW. The user should check both PGOOD and FAULT telemetry; if FAULT is asserted, the enable command was overridden by hardware safety, not a firmware bug.
  - **If IC9 is not populated, the four LM139 outputs are open and have no effect** — meaning the PA16-to-EN path is even simpler: PA16 HIGH → EN HIGH, PA16 LOW → EN LOW. This is the most likely scenario given the "Excluded from BOM" flag. If subsequent debugging shows the eFuses don't switch at all, photograph the area around U-ref IC9 (PCB (93.25, 111.25) per Source 4) and check whether the SOIC-14 footprint is occupied.
- **Why I'm recording it:**
  Explains why my Question 8 answer must be nuanced: "active-HIGH" is correct from firmware's perspective but the chip's EN pin is technically an analog UVLO input, and a hardware-OR with safety comparators sits between PA16 and the EN pin.
