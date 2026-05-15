# How to Test the PCU Mainboard

This file lists every physical-world test we need to run on the real PCU
V4.1 board, ranked by priority. The goal is to collect enough evidence to
back the claim, in the semester report, that **every firmware capability
was verified on real hardware**.

For each test we list: what it proves, the equipment, the wiring, the
exact steps, what to record, what pictures to keep, and which figure or
table it becomes in the report.

---

## Common assumptions for every test

These are true for every test below unless overridden in the test itself.

- The mainboard is flashed with the latest `BOARD=mainboard-pds` firmware.
- The ESP32 bridge is wired to J3 (TX2 → PA11, RX2 → PA10, GND → GND).
- The local web app is running on the laptop and `Connect` succeeds.
- The bench DC supply, oscilloscope, multimeter, board, ESP32, and laptop
  all share a common ground. Never run a test with floating grounds.
- Set the supply current limit **before** turning the supply output on.
  Safe defaults: 200 mA for low-power tests, 500 mA for buck tests.
- PWM stays disarmed (`pwm-disarm`) at every test boundary. Re-arm only
  inside the buck/MPPT tests, then disarm at the end.

---

## Priority ranking — do them in this order

| Order | Test | Needs power resistors? | Estimated time |
|---|---|---|---|
| 1 | Test A — PWM waveform sign-off | No | 30 min |
| 2 | Test B — ADC calibration vs known voltage | No | 30–60 min |
| 3 | Test C — eFuse switching on real board | No | 30 min |
| 4 | Test D — State machine on real sensor source | No | 60 min |
| 5 | Test E — MPPT against a fake solar source ⭐ | **Yes** | 90 min |
| 6 | Test F — Buck transfer curve | **Yes** | 60 min |
| 7 | Test G — INA226 sensor chip verification | **Yes** | 45 min |
| 8 | Test H — CHIPS communication stress | No | 30 min |
| 9 | Test I — Watchdog timer | No | 15 min |

Test E is the headline. Everything before it is foundation that backs
Test E up.

---

## Test A — PWM waveform sign-off

### What it proves
The buck-driver PWM signals on PA12 and PA13 are correctly shaped: 300
kilohertz frequency, opposite phase to each other, with a small both-off
gap ("dead time") of roughly 100 nanoseconds at every duty cycle. This
gap is what prevents the two power transistors from being on at the same
time and short-circuiting the supply.

### Why it matters for the report
Required before any buck-energizing test. We already have one edge
verified with a 100 ns gap; we need the symmetric edge, plus evidence
that the gap holds across the duty range.

### Equipment
- Oscilloscope with two probes.

### Wiring
- Probe 1 tip on PA12 (high-side PWM), ground clip to board GND.
- Probe 2 tip on PA13 (low-side PWM), ground clip to board GND.

### Steps
1. Web app: Manual mode → set PWM duty = 13107 (about 20%) → arm PWM.
2. Scope: zoom into the PA12-falling / PA13-rising edge. Place cursors on
   both edges, measure delta time (this is the dead time). Save
   screenshot.
3. Scope: zoom into the PA12-rising / PA13-falling edge (the one we
   haven't checked yet). Same cursor measurement. Save screenshot.
4. Zoom out: capture one screenshot showing multiple full periods of both
   signals together. Use cursors to measure the period (should be 3.33
   microseconds = 1 / 300 kilohertz).
5. Web app: change duty to 32768 (50%). Repeat steps 2–3.
6. Web app: change duty to 52428 (80%). Repeat steps 2–3.
7. Web app: `pwm-disarm`.

### What to record
A small table: duty cycle, dead time at edge 1 (ns), dead time at edge 2
(ns), measured frequency (kHz).

### Pictures you should take
- Scope screenshot at each transition × each duty = 6 screenshots.
- One wide-view screenshot showing the complementary shape over several
  periods.
- One photo of the bench setup (probes on the board).

### In the report
- Chapter 4. Figure: the wide-view scope shot. Caption explains the
  complementary shape and labels the dead-time gap.
- Inline table: dead time vs duty.
- One sentence: "the gap remained ≥ X ns across the tested duty range,
  satisfying the EPC2152 shoot-through margin."

### Bench setup (from PCB research)

**Probe target:** **J10** — a 2-pin through-hole header dedicated to PWM probing, in the buck cluster near the bottom-centre of the board, top side, at PCB (91.225, 148.25).
- Pin 1 (square pad) = PWM_H (PA12)
- Pin 2 (round pad) = PWM_L (PA13)
- Pin spacing: 2.54 mm

**Important caveat:** the BOM marks J10 as "Excluded from BOM", meaning the header pins are likely not pre-soldered. The plated through-holes are there. Either solder a 0.1" pin header into J10, or use fine-tip probe hooks directly in the bare holes.

**Ground clip:** the J6 middle screw (silkscreen "GND"), ~13 mm west of J10. There is no dedicated GND test point in the buck cluster.

**Backup probe points if J10 is unusable:** IC6 (EPC2152) pins 3 (HSIN) and 4 (LSIN) — small SMD pads (0.32 mm) on the west edge of the chip. Trickier than J10 but valid. Note: the older `docs/mainboard_pinout_pcu_v4_1.md` calls this chip "U1" — the actual silkscreen reference is **IC6**.

**Safety:** during Test A the buck input rail (BUCK_IN) is OFF, so the EPC2152 switching node never sees high voltage. Common scope ground is fine.

---

## Test B — ADC calibration vs known voltage

### What it proves
Each analog sensing input on the board reads the correct voltage. We
apply known voltages from the bench supply, read what the firmware
reports, and compare. This gives us calibration coefficients (or
confirms the existing ones).

### Why it matters for the report
Every voltage-related claim downstream (battery monitoring, panel
voltage, MPPT input) only holds if the ADC chain is accurate.
"Calibrated against a known reference" is the standard answer.

### Equipment
- DC bench supply with a multimeter to verify its output.
- The board, powered as usual.

### Wiring
- Disconnect anything that would energize the buck. We are only feeding
  the sense networks.
- Supply +output → the sense net under test (one of OUTV1, OUTV2,
  PV_IMON, BAT_IMON depending on the iteration).
- Supply ground → board ground.
- Multimeter across the supply output, set to DC volts.

### Steps
1. Web app: Off mode. Sensor source = real board hardware (or use a
   `set_sensor_source` command if implemented; otherwise flash a build
   with `set_sensor_source(PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE)` at
   boot).
2. Stream telemetry at 200 ms period.
3. For each sense net of interest (start with OUTV1):
   - Set supply to 0 V. Wait. Record raw ADC count and converted mV from
     the stream. Record multimeter reading.
   - Step supply to 1 V, 2 V, 4 V, 6 V, 8 V. Record each.
4. Repeat the sweep for OUTV2.
5. Optional: same procedure on OUTA1, OUTA2 if a way to feed them a known
   "current-proxy voltage" exists (the LT6108 outputs an amplified
   voltage proportional to current).

### What to record
For each sense net, a table:

| Supply voltage (V, multimeter) | Raw ADC count | Reported mV | Error (%) |

### Pictures you should take
- One photo of the wiring (supply, multimeter, board, jumper wires).
- One screenshot of the web app telemetry view at one of the steps.

### In the report
- Chapter 4. Two figures: the calibration table for OUTV1 and OUTV2.
- One short paragraph: "ADC linearity was verified across the operating
  range with worst-case error of X%."

### Bench setup (from PCB research)

**MCU pin map confirmed:** OUTV1 → PB08 (AIN2), OUTV2 → PB09 (AIN3), OUTA1 → PB06 (AIN14), OUTA2 → PB07 (AIN15), PV_IMON → PB04 (AIN12), BAT_IMON → PB05 (AIN13).

**Divider ratios from the BOM:**
- OUTV1: 100 kΩ top (R32) / 3.6 kΩ bottom (R33). Net : MCU = 1 : 28.78. 1 V at the OUTV1 net → 34.75 mV at the MCU pin.
- OUTV2: 100 kΩ top (R36) / 11.8 kΩ bottom (R37). Net : MCU = 1 : 9.48. 1 V → 105.5 mV.
- OUTA1, OUTA2, PV_IMON, BAT_IMON: **no divider** — the LT6108 OUTA pin and the TPS25940 IMON pin drive the MCU pin 1 : 1 through a 750 Ω-to-GND load resistor.

**Injection points (top side; no dedicated TPs sit on these nets directly):**
- OUTV1 → TP11 (upstream of the divider, after the eFuse), or the bottom pad of R33 at PCB (49.75, 59).
- OUTV2 → TP17 (upstream of the divider), or R37 at PCB (99.462, 108.125).
- OUTA1 → IC10 (LT6108) OUTA pin directly.
- OUTA2 → IC11 (LT6108) OUTA pin directly.
- PV_IMON → R12 at PCB (50.475, 96.96).
- BAT_IMON → R24 at PCB (135.5, 93.6).

**Expected ADC count at 1.000 V applied** (assuming the firmware's 3300 mV reference, 12-bit conversion):
- OUTV1: ~43 counts (post-divider).
- OUTV2: ~131 counts (post-divider).
- OUTA1, OUTA2, PV_IMON, BAT_IMON: ~1241 counts (1 : 1).

**⚠️ Systematic error this test will measure:** the AUX_3V3 rail that feeds the ADC reference is actually ~3.06 V (not 3.3 V) because of a Schottky drop in the AUX path. The firmware still assumes 3300 mV, so every reading reads ~8 % low. **Use a multimeter on AUX_3V3 during the test and record the actual value** — this is precisely how we measure the true reference. Update the firmware constant `ADC_REFERENCE_MILLIVOLTS` after the test, not before.

**Max safe injection voltage:** ≤ 3.3 V at any pad that feeds the MCU pin directly (OUTA pins, IMON pads, R33, R37); ≤ 12 V at TP11; ≤ 8 V at TP17.

**Before injecting at TP11 or TP17:** disable both eFuses (`set_manual_pv off`, `set_manual_bat off`) so the injection doesn't back-feed upstream rails.

---

## Test C — eFuse switching on real board

### What it proves
The two GPIO output pins (PA16 for the solar-panel switch, PA17 for the
battery switch) actually toggle the corresponding power-distribution
switches on the board. So far this is only proven on the dev board,
where the pins aren't even physically connected.

### Why it matters for the report
Without this, the firmware's `enter_manual` + `set_manual_pv` +
`set_manual_bat` commands are just "trust me, the bits change in RAM."
We need physical evidence that the rails actually move.

### Equipment
- Bench supply, multimeter.

### Wiring
- Supply +output → PV input net on the board (the "before-eFuse" side of
  the PV path). Use a current limit of 200 mA.
- Supply ground → board ground.
- Multimeter → the "after-eFuse" rail on the PV side.

### Steps
1. Web app: enter manual mode. Set PV switch = off, BAT switch = off,
   PWM = 0. Disarm PWM.
2. Apply 5 V from the supply. Multimeter on the rail should read 0 V (or
   leakage close to zero).
3. Web app: PV switch on. Multimeter should now read ~5 V (within the
   eFuse drop, typically a few hundred mV).
4. Web app: PV switch off. Multimeter back to 0.
5. Move multimeter to the battery rail. Repeat with the BAT switch.
6. Check the PGOOD readback in telemetry (`pv_efuse_power_good`,
   `bat_efuse_power_good`) — it should reflect the actual rail state,
   not just the requested state.

### What to record
Two tables (one per switch): commanded state, multimeter reading,
PGOOD readback.

### Pictures you should take
- One photo of the bench setup.
- One screenshot of the web app showing the switch toggle and the
  matching telemetry.

### In the report
- Chapter 4. Short subsection: "Switching of the protected rails was
  verified on the real PCU board." Two-row results table.

### Bench setup (from PCB research)

**MCU pin map confirmed:**
- PA16 → drives PV eFuse enable (TPS25940 IC4)
- PA17 → drives BAT eFuse enable (TPS25940 IC7)
- PA18 → reads PV_PGOOD; PA19 → reads BAT_PGOOD
- PA20 → reads ~PV_FLT (active-LOW); PA21 → reads ~BAT_FLT (active-LOW)

**Enable polarity:** drive HIGH to enable, LOW to disable. The schematic annotation reads "ENABLE: HIGH-Z, DISABLE: PULL-DOWN".

**PV side wiring:**
- Pre-eFuse (bench supply input): **J4 pin 1**, silkscreen "PV_RAW", top side, top-left at PCB (49.6, 79.2).
- Post-eFuse (multimeter): **TP10** at PCB (68, 98.6) or **TP11** at PCB (64.25, 129.25), top side.

**BAT side wiring:**
- Pre-eFuse: **J7 middle screw**, silkscreen "BUCK_OUT", PCB (107.2, 142.75), top side. (Yes — the battery-side eFuse takes the buck output as its input.)
- Post-eFuse: **TP17** at PCB (118, 91.7) or **TP16** at PCB (127.25, 108), top side.

**PGOOD / FLT electrical:** open-drain outputs with 10 kΩ pull-ups (R13, R14, R25, R26) to AUX_3V3. PGOOD is active-HIGH, FLT is active-LOW. They read cleanly through the SAMD21 GPIO.

**Current limit:** each eFuse is configured with R_ILIM = 17.2 kΩ → I_LIM ≈ 5.13 A typical. The 200 mA test current is far below this.

**Output ramp:** ~4–5 ms after EN transitions HIGH, governed by the dVdT slew capacitor. A multimeter sees this as instant. **Action item for the test:** check whether the firmware's eFuse driver reads PGOOD immediately after asserting enable. If yes, it'll see "not good yet" briefly — add a 10 ms settle delay.

**Max safe bench voltage:** 18 V continuous (TPS25940 recommended top), 20 V absolute max. The 5 V test is well inside the safe window.

**Caveat:** the BOM marks LM139 fault comparator IC9 as "Excluded from BOM". Verify it is physically populated before relying on the FLT outputs for anything beyond Test C.

---

## Test D — State machine on real sensor source

### What it proves
The PCU state machine selects the correct mode (MPPT_CHARGE, CV_FLOAT,
SA_LOAD_FOLLOW, BATTERY_DISCHARGE, SAFE_MODE) when it reads from real
sensors instead of injected values.

### Why it matters for the report
The 12 web-page scenarios already prove the state machine works against
injected numbers. This test proves the same logic works when the numbers
come from the real ADC. It is the integration link between Test B (ADC
calibration) and the existing state-machine demo.

### Equipment
- Bench supply, multimeter.

### Wiring
- Supply → OUTV1 (panel voltage sensor) **and** OUTV2 (battery rail
  voltage sensor). Use a small switch or just move the wire between the
  two for the manual cases.
- All other ADC inputs left at 0 V (the firmware will read zero
  current).

### Steps
1. Web app: switch sensor source to real board hardware.
2. Apply the voltage combinations from `state-transitions.md`:
   - Panel low, battery normal → expect BATTERY_DISCHARGE.
   - Panel high, battery low-normal → expect MPPT_CHARGE.
   - Panel high, battery near full → expect CV_FLOAT.
   - Panel high, battery full → expect SA_LOAD_FOLLOW.
   - Battery very low → expect SAFE_MODE.
3. For each case, read the reported PCU mode from telemetry. Hold for
   several seconds to make sure it is stable.

### What to record
Table: panel voltage applied, battery voltage applied, expected mode,
observed mode, pass/fail.

### Pictures you should take
- Photo of the bench setup.
- One screenshot of the web app's State page (or the telemetry view)
  showing the reported mode for at least two cases.

### In the report
- Chapter 4. Subsection comparing the 12-scenario web-page test (which
  used injected values) to this hardware-fed equivalent. Table of the
  five mode cases above.

### Bench setup (from PCB research)

**This test reuses every injection point and divider ratio from Test B.** See "Test B → Bench setup" above for the per-net wiring.

**Required firmware change before the test can run:** the sensor source toggle from "injected" → "real board hardware" is currently a code-only call to `set_sensor_source(PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE)`. Two ways to enable it:
- (Quick) Flash a build with that line wired into the boot sequence.
- (Cleaner) Implement the `BOARD_SET_SENSOR_SOURCE` CHIPS command (command ID 0x3D is already reserved) plus a web-page button.

Pick whichever is faster.

**Disable both eFuses** before injection so the upstream rails stay clean while you drive the sense nets.

**Mode-transition voltages:** the specific thresholds live in `state-transitions.md`. Hold each input combination steady for 5+ seconds before reading the reported PCU mode via telemetry.

---

## Test E — MPPT against a fake solar source ⭐ headline

### What it proves
The Incremental Conductance MPPT algorithm, running on the real SAMD21
chip, finds and tracks the maximum power point of a real (if simplified)
power source connected to the panel input. The maximum power point is
the voltage at which the source delivers the most power; MPPT exists to
sit on that point automatically.

### Why it matters for the report
This is the single most compelling figure in the whole report. It proves
the entire firmware chain end-to-end: ADC reads → algorithm decides →
PWM changes → real buck converter responds → new current flows → new
sensor readings → algorithm reacts. One test, one plot, every layer
exercised.

### Equipment
- Bench DC supply.
- One power resistor in series with the supply, used as the "panel"
  source resistance. Suggested: 47 Ω, at least 5 W.
- One power resistor as the output-side load. Suggested: 22 Ω, at
  least 5 W.
- Oscilloscope (optional, only if we want to record the PWM during the
  run).
- Multimeter.

### How the fake solar source works
A real solar panel does not output a fixed voltage. Its voltage drops as
you draw more current. A bench supply with a resistor in series imitates
this behavior in a simplified way:

```
supply (18 V) --- 47 Ω resistor --- buck input (V_panel)
```

When the buck draws no current, V_panel = 18 V. When the buck draws lots
of current, the resistor drops voltage and V_panel falls. The point of
maximum power transferred to the buck is exactly at V_panel = supply / 2
= 9 V. The algorithm should converge there.

Numerical expectation for these values:
- Open-circuit voltage (the maximum panel voltage): 18 V.
- Short-circuit current (the maximum panel current): 18 V / 47 Ω = 0.383 A.
- Maximum power point voltage (V_mpp): 9 V.
- Maximum power point current (I_mpp): 0.191 A.
- Maximum power (P_mpp): 1.72 W.

### Wiring
- Supply +output → one end of the 47 Ω resistor.
- Other end of 47 Ω → board panel input.
- Supply ground → board ground.
- Board battery output → 22 Ω power resistor → board ground.
- Multimeter on the supply output to monitor V_panel.
- Scope probes on PA12 / PA13 if recording PWM (optional).

### Steps
1. Set the supply to 18 V, current limit 500 mA. **Output off** at first.
2. Web app: Off mode. Confirm PWM is disarmed.
3. Turn supply output on. Confirm V_panel reads 18 V on multimeter.
4. Web app: start MPPT mode against real board sensors (not the ESP32
   model). Stream telemetry at 100 ms period.
5. Watch the duty cycle, V_panel, I_panel, and P_panel readings move.
   They should converge with V_panel approaching 9 V and P_panel
   approaching 1.7 W.
6. Hold for at least 30 seconds after convergence to capture steady
   state.
7. Change the supply voltage to 12 V mid-run. The new V_mpp is 6 V;
   MPPT should re-converge.
8. Set supply back to 18 V. Verify it tracks back.
9. Web app: stop MPPT, `pwm-disarm`.

### What to record
- Continuous stream of V_panel, I_panel, P_panel, duty cycle, time
  stamps for the whole run. The web app already produces this — save the
  CSV.
- Multimeter reading of V_panel at steady state for cross-check.

### Pictures you should take
- One photo of the bench setup (supply + series resistor + board + load
  resistor + scope).
- Web-app screenshot showing the convergence plot.
- A plot generated offline (Excel, matplotlib, anything) showing time on
  x-axis, V_panel + P_panel on y-axis, marking the expected MPP and the
  observed steady-state.
- One scope screenshot of the PWM during MPPT operation (optional but
  good).

### In the report
- Chapter 4. This is **the** centerpiece figure: the convergence plot.
- A short narrative: "MPPT converged to within X mV of the theoretical
  9 V maximum-power-point voltage in Y seconds. After a step change in
  the source from 18 V to 12 V, the algorithm re-converged within Z
  seconds."
- A separate small figure for the step response if the data shows it
  cleanly.

### Bench setup (from PCB research)

**Panel input connector:** **J4** (silkscreen "PV_RAW" + "GND"), 2-pin Phoenix terminal, top side, top-left at PCB (49.6, 79.2). Pin 1 = +PV, pin 2 = GND.

**Battery output connector:** **J7** (silkscreen "BUCK_OUT", "GND", "+BAT"), 3-pin Phoenix terminal, top side, bottom-centre at PCB (106.78, 148.05). Connect the 22 Ω load between the **middle screw ("BUCK_OUT")** and **"GND"** — that taps the buck output before the R50 charge shunt.

**V_panel probe:** **TP12** at PCB (81.25, 124.5), top side. Multimeter across TP12 and any GND screw.

**V_battery probe:** **TP13** at PCB (106.05, 124.5), top side. Multimeter across TP13 and GND.

**Panel current:** read from telemetry. The firmware already reports `PV_IMON`. The current path is panel → TPS25940 IC4 eFuse → R17 (10 mΩ shunt) → IC4 IMON pin → 12.1 kΩ R12 → ADC pin PB04.

**Limits when feeding through J4 (goes through the PV eFuse):**
- Max supply voltage: **17 V** (the eFuse trips OV at 17.67 V per the schematic author's design).
- Max supply current: 5 A (eFuse hardware limit — far above the ~0.4 A short-circuit of the fake-solar source).

**Limits when bypassing the eFuse (feeding TP12 directly, or J6's "BUCK_IN" screw):**
- Max V_in: **≤ 20 V** (C15 input cap 25 V rated, 80 % derate).
- Max I_in: ~7 A (R17 shunt rating).

**V_out hard ceiling: 14 V.** C17 output cap is 16 V rated; derate to 14 V. The firmware must not allow any duty / V_in combination that pushes V_out above this. Worth a duty-clamp check.

**Expected MPP for 18 V supply + 47 Ω series:** V_oc = 18 V, V_mpp = 9 V, I_mpp = 0.191 A, P_mpp = 1.72 W. MPPT should converge V_panel → ~9 V at steady state.

---

## Test F — Buck transfer curve

### What it proves
The buck converter, when given a fixed PWM duty cycle, produces an
output voltage that follows the buck equation **V_out ≈ duty × V_in**.
This characterizes the buck as a standalone block, separate from MPPT.

### Why it matters for the report
Before claiming MPPT works, we want one figure showing the buck plant
itself behaves correctly. If Test E fails, Test F tells us whether the
problem is in the buck or in the algorithm.

### Equipment
- Bench supply.
- Two power resistors (same as Test E will work: 47 Ω for series, 22 Ω
  for load — but no series resistor needed here if you want to push V_in
  closer to the supply value).
- Two multimeters, or one multimeter and the scope.

### Wiring
- Supply +output → board panel input directly (no series resistor for
  this test). Current limit 500 mA.
- Board battery output → 22 Ω power resistor → board ground.
- Multimeter 1 on V_in (supply side).
- Multimeter 2 on V_out (battery side).

### Steps
1. Supply at 12 V, output off. PWM disarmed.
2. Web app: fixed-PWM mode, duty = 10000 (about 15%). Arm PWM.
3. Supply on. Wait for steady state.
4. Record V_in (multimeter 1), V_out (multimeter 2), supply current
   reading.
5. Disarm PWM, change duty to 20000 (about 30%), arm again. Record.
6. Repeat for duty values 30000, 40000, 50000, 55000.
7. Disarm PWM, supply off.

### What to record
Table: duty cycle (raw value and percent), V_in, V_out, V_out / V_in,
expected duty/65535.

### Pictures you should take
- One photo of the setup.
- A plot of duty/65535 (x-axis) vs V_out/V_in (y-axis). It should sit
  near the diagonal y = x line.

### In the report
- Chapter 4, just before the MPPT section. Figure: the duty-vs-ratio
  plot.
- One sentence: "The buck converter follows the ideal step-down
  relationship within X% across the tested duty range."

### Bench setup (from PCB research)

**Bypass the PV eFuse for this test.** The open-loop duty sweep would otherwise trip the eFuse current limit at low duty. Feed the supply directly to one of:
- **TP12** at PCB (81.25, 124.5), top side, or
- The **J6 screw labelled "BUCK_IN"** at silkscreen (80.2, 142.75).

Both are the same net (BUCK_IN, post-eFuse).

**V_in probe:** TP12 (or wherever you connected the supply).

**V_out probe:** **TP13** at PCB (106.05, 124.5), top side.

**Load:** 22 Ω power resistor between J7's "BUCK_OUT" screw and J7's "GND" screw.

**Inductor (so you can identify the buck cluster):** **L2**, 2.2 µH, 10 A Vishay IHLP2525EZER2R2M01. Sits at PCB (99.3, 136), between IC6 (EPC2152) and C17 (47 µF output bulk cap). Body ~7 × 7 mm — easy to spot.

**Limits when bypassing the eFuse:**
- Max V_in via TP12: **≤ 20 V** (C15 25 V cap, 80 % derate).
- Max V_out: **≤ 14 V** (C17 16 V cap, 87 % derate).
- Max current: ~7 A (R17 shunt) — far above this test's draw.

**Expected behaviour:** V_out / V_in ≈ duty / 65535. Plot duty/65535 vs V_out/V_in — should sit near the y = x line. Deviation at very low or very high duty is normal (discontinuous-conduction-mode edge effects and minimum on-time).

---

## Test G — INA226 sensor chip verification

### What it proves
The two INA226 chips on the board (digital current and voltage sensors
that talk over the I²C bus, where I²C is a two-wire serial communication
protocol) are correctly addressed, return their expected
identification numbers, and produce readings that match a multimeter.

### Why it matters for the report
The sensor abstraction has three drivers per current measurement
(INA226, LT6108, TPS25940 IMON). The INA226 is the primary one for
mainboard. The report's "sensor stack works on real hardware" claim
relies on this.

### Equipment
- Bench supply (low-current setting).
- Multimeter.
- A power resistor in series with one of the rails to create a known
  current flow that the INA226 can measure across its shunt resistor.
  Suggested: 22 Ω, 5 W.

### Wiring
- Same general layout as Test E, but we will only run the rail at low
  current and read the INA226 directly via the firmware's I²C path.

### Steps
1. Web app: send a one-shot I²C bus scan (firmware command if available,
   otherwise add one temporarily). Expected: two devices acknowledge,
   likely at addresses 0x40 and 0x41.
2. For each INA226 address, read register 0xFE (manufacturer ID).
   Expected value: 0x5449 (this is the ASCII letters "TI"). Then read
   register 0xFF (die ID). Expected value: 0x2260.
3. Apply a known voltage to the chip's bus-voltage input via the supply.
   Read the bus-voltage register, compare to multimeter.
4. Force a known current through the shunt by adding the series
   resistor. Read the current register, compare to (supply voltage /
   total resistance).

### What to record
- The bus-scan output.
- For each chip: manufacturer ID, die ID, bus voltage reading vs
  multimeter at two or three operating points, current reading vs
  expected at two or three operating points.

### Pictures you should take
- Web-app or terminal screenshot of the bus scan and the ID reads.
- One photo of the bench setup.

### In the report
- Chapter 3 (architecture) for context, Chapter 4 (results) for the
  numbers. A small table of measured vs expected values for one of the
  chips.

### Bench setup (from PCB research)

**🔴 Fix the firmware before running this test.** The current I²C address constants in the source code are wrong:
- `src/drivers/ina226_panel_on_mainboard.{c,h}`: `0x40` → should be **`0x45`**
- `src/drivers/ina226_battery_on_mainboard.{c,h}`: `0x41` → should be **`0x46`**

The A0 and A1 strap pins on both chips wire to V+ on the schematic, giving 0x45 (panel) and 0x46 (battery). The bus scan finds nothing at 0x40 / 0x41 because the chips don't answer there. **This fix is applied separately, before testing.**

**Chip locations on the physical board:**
- IC5 (panel-side) at PCB (49.8, 114.9), top side, lower-left.
- IC8 (battery-side) at PCB (136.95, 118.5), top side, lower-right.

**Shunt resistors (firmware constants are correct, 2 mΩ each):**
- R49 (panel) at PCB (75.8, 129.2)
- R50 (battery) at PCB (110.54, 129.2)

**I²C bus:** SERCOM3 on PA22 (SDA) / PA23 (SCL), peripheral mux C, 100 kHz. Pull-ups R47 = R48 = 10 kΩ to AUX_3V3.

**Probe access for SDA / SCL:** header **J2**, 4-pin, top-right corner at PCB (136.5, 63.13), top side.
- J2 pin 1 = HEATER_SW
- J2 pin 2 = POWER_SW
- J2 pin 3 = SCL
- J2 pin 4 = SDA

⚠️ **J2 pins 1 and 2 drive a heater MOSFET and a power-switch enable.** Don't probe them blindly. Use pins 3 / 4 only.

**What each chip measures:**
- IC5 (panel): bus voltage at PV_OUT+ (upstream of R49). Current path = panel → buck input.
- IC8 (battery): bus voltage at BAT_IN+ (upstream of R50). Current path = buck output → battery.

**Expected register reads after the address fix:**
- Manufacturer ID (register 0xFE) = **0x5449** (ASCII "TI").
- Die ID (register 0xFF) = **0x2260**.

**Error budget:** dominated by the ±5 % shunt tolerance — expect current readings within ±5 % of the multimeter at moderate currents.

---

## Test H — CHIPS communication stress

### What it proves
The board's command-and-telemetry link tolerates sustained traffic,
rejects corrupted frames cleanly, and handles command retransmission
correctly. CHIPS is the framing protocol that wraps every message
between the laptop and the board with a checksum and a sequence number.

### Why it matters for the report
The link is the foundation under every other test. We want a paragraph
in the report saying "communication was exercised at maximum streaming
rate for X minutes with zero data loss."

### Equipment
- Just the laptop and the existing wired setup.

### Steps
1. Web app: stream telemetry at the fastest period the firmware supports
   (try 50 ms).
2. Let it run for 10 minutes uninterrupted.
3. The web app should record received-frame count and any
   parse/CRC errors. Save the totals.
4. Bad-frame injection: from a small Python script (or by editing the
   ESP32 bridge temporarily), send a CHIPS frame with the CRC byte
   deliberately flipped. The firmware should reject it and increment its
   error counter (telemetry exposes this).
5. Duplicate-sequence test: send the same `get_values` command with the
   same sequence number twice in a row. The firmware should reply twice
   but only "execute" once (this is the mission requirement for
   idempotent transactions).

### What to record
Total received frames, errors during the 10-minute run; bad-CRC counter
before and after the injection; duplicate-sequence behavior log.

### Pictures you should take
- One screenshot of the web-app stats after the 10-minute run.

### In the report
- Chapter 4, short subsection. One table, one paragraph.

### Bench setup (from PCB research)

No PCB-side info needed. Pure firmware / communications test.

---

## Test I — Watchdog timer

### What it proves
The board's internal watchdog (a self-reset mechanism that triggers if
the main loop stops servicing it) actually fires when the main loop
stalls, and does not fire during normal operation. This is the standard
recovery mechanism for radiation-induced firmware hangs.

### Why it matters for the report
Required by the mission requirement MCU-11 (watchdog for fault
recovery). One paragraph + a screenshot is enough.

### Equipment
- Just the laptop and the wired setup.

### Steps
1. Normal-operation check: leave the firmware running for 10 minutes
   while streaming. Observe no resets (uptime in telemetry keeps
   climbing).
2. Stall test: flash a temporary build that includes a `while (1) ;`
   inside the command-handler path, triggered by a specific test command
   only. Send the command. The board should hang for a few seconds and
   then reset (uptime returns to zero, then climbs again).
3. Revert the temporary build.

### What to record
The uptime trace around the stall event: pre-stall value, hang duration,
post-reset value.

### Pictures you should take
- One screenshot of the web-app telemetry showing the uptime resetting
  to zero.

### In the report
- Chapter 4, short paragraph. One small figure of the uptime trace.

### Bench setup (from PCB research)

No PCB-side info needed. Pure firmware / watchdog test.

---

## Known firmware issues to verify or fix before / during testing

Captured from the PCB research before bench work begins. Two need code changes; two are documentation or behaviour checks.

**🔴 INA226 I²C addresses (Test G blocker).**
- `src/drivers/ina226_panel_on_mainboard.{c,h}`: `0x40` → **`0x45`**
- `src/drivers/ina226_battery_on_mainboard.{c,h}`: `0x41` → **`0x46`**

The strap pins (A0, A1) on both chips wire to V+ on the schematic. The actual addresses are 0x45 (IC5 panel) and 0x46 (IC8 battery). Without this fix, the Test G bus scan returns nothing. Two-line firmware change.

**🟡 ADC reference voltage is ~3.06 V, not 3.3 V (affects every Test B reading).**

The firmware constant `ADC_REFERENCE_MILLIVOLTS = 3300u` assumes AUX_3V3 = 3.3 V. The actual rail sits at ~3.06 V because a Schottky diode in the AUX path drops ~0.24 V — the schematic itself annotates this. Every ADC-derived millivolt is therefore ~8 % low. Test B is the test that measures the exact value: clip a multimeter on AUX_3V3 during the sweep and record what it reads. **Update the firmware constant after Test B**, not before — the test data tells you the right number.

**🟡 LT6108 OUTA and TPS25940 IMON scaling are flagged provisional in the firmware.**

`mainboard_adc_reader.c` already tags every reading with `_OUTA_SCALING_PROVISIONAL` and `_IMON_SCALING_PROVISIONAL` because the scaling constants have not been validated against the actual silkscreen resistor values. Test B's calibration tables tell you what to put in their place. Update the constants after the bench session.

**🟡 eFuse enable → PGOOD settle time (~5 ms).**

The TPS25940 output ramps over 4–5 ms after EN transitions HIGH, governed by its dVdT slew cap. If the firmware's eFuse driver reads PGOOD immediately after asserting enable, it briefly sees "not good yet". Either confirm the driver already waits, or add a 10 ms settle delay. Test C will reveal whether this is a real problem.

**🟢 Doc fix: EPC2152 reference designator.**

`docs/mainboard_pinout_pcu_v4_1.md` calls the buck driver chip "U1". The actual silkscreen on the board reads **IC6**. Cosmetic, but worth fixing next time the doc is touched.

---

## Where to put the evidence

Create a folder `src-pds/test_evidence/`. Inside it, one subfolder per
test, named after the letter (e.g. `test_A_pwm/`, `test_B_adc/`, ...).
Inside each subfolder:

- All scope screenshots, photos, web-app screenshots.
- A `notes.md` with the recorded numbers and dates.
- The raw CSV from any streaming test.

Once a test is complete, append a one-line entry to a top-level
`test_evidence/index.md` with the date and the pass/fail result. That
file becomes the source for the report's "Results" tables in Chapter 4.

---

## Items still open before testing starts

- Source the power resistors (~47 Ω 5 W and ~22 Ω 5 W or similar) for
  Tests E, F, G.
- Confirm the bench supply's current-limit setting works (turn it down
  to 50 mA, short the leads, verify the supply clamps and does not just
  pump current).
- Verify the firmware has a `set_sensor_source` mechanism reachable from
  the host. If only a code-time switch exists, plan a one-line firmware
  edit before Test B, D, E.
- Decide whether to add a one-shot I²C bus-scan command before Test G,
  or do it via a temporary main.c change.
