# MPPT Simulation — Complete Technical Reference

This document explains everything about the MPPT (Maximum Power Point Tracking)
simulation for the CHESS Pathfinder 0 CubeSat. It is written so that someone
with no background in solar power, electronics, or embedded systems can read it
from top to bottom and understand every decision, every equation, and every line
of code.

If you are reviewing this work, this document is your map. Every claim is sourced.
Every hypothesis is labeled. Every parameter can be traced to a datasheet.

---

## Table of Contents

1. [What problem are we solving?](#1-what-problem-are-we-solving)
2. [What is a solar cell?](#2-what-is-a-solar-cell)
3. [The I-V curve: why solar panels are weird](#3-the-i-v-curve-why-solar-panels-are-weird)
4. [The Maximum Power Point](#4-the-maximum-power-point)
5. [How do you control where you operate on the curve?](#5-how-do-you-control-where-you-operate-on-the-curve)
6. [MPPT algorithms: two approaches](#6-mppt-algorithms-two-approaches)
7. [Why we chose Incremental Conductance](#7-why-we-chose-incremental-conductance)
8. [The algorithm in detail](#8-the-algorithm-in-detail)
9. [Integer-only implementation (no floating point)](#9-integer-only-implementation-no-floating-point)
10. [The physics model: simulating a solar panel](#10-the-physics-model-simulating-a-solar-panel)
11. [The buck converter model](#11-the-buck-converter-model)
12. [The closed-loop simulation](#12-the-closed-loop-simulation)
13. [ADC noise and the moving average filter](#13-adc-noise-and-the-moving-average-filter)
14. [Simulation results](#14-simulation-results)
15. [Bugs found during development](#15-bugs-found-during-development)
16. [Hypotheses and assumptions](#16-hypotheses-and-assumptions)
17. [How to build and run](#17-how-to-build-and-run)
18. [File inventory](#18-file-inventory)
19. [References](#19-references)

---

## 1. What problem are we solving?

The CHESS Pathfinder 0 is a CubeSat — a small satellite about the size of a
shoebox. It orbits Earth at 475 km altitude, completing one orbit every 94
minutes. For about 57 minutes of each orbit it is in sunlight, and for about
37 minutes it is in Earth's shadow (eclipse).

The satellite has four solar panels that generate electricity from sunlight.
This electricity must charge the batteries and power all the satellite's
systems (cameras, radio, computer, attitude control).

**The problem:** a solar panel does not automatically give you the maximum
possible power. Depending on how much current you draw from it, you get
different amounts of power. Draw too little current and you waste potential
power. Draw too much and the voltage collapses, also wasting power. There
is exactly one sweet spot — the **Maximum Power Point (MPP)** — where the
panel delivers the most watts.

**The solution:** the satellite's microcontroller (a SAMD21RT chip) runs an
algorithm that continuously adjusts how much current it draws from the panel,
hunting for that sweet spot. This algorithm is called **MPPT** (Maximum Power
Point Tracking).

**The purpose of this simulation:** before we put the algorithm on the real
chip, we need to prove it works. This simulation runs the exact same algorithm
code on a regular PC, feeding it simulated solar panel data, and verifies that
it converges to the correct MPP under various conditions (different temperatures,
different light levels, eclipse transitions).

---

## 2. What is a solar cell?

A solar cell is made of semiconductor material (in our case, Gallium Arsenide —
GaAs). When a photon (a particle of light) hits the semiconductor with enough
energy, it knocks an electron free. This free electron can flow through a wire
as electrical current.

The cell is essentially a **diode** — a one-way valve for electricity. In the
dark, it behaves like any other diode: it blocks current in one direction and
allows it in the other. But when light hits it, the photon-generated electrons
create a current that flows out of the cell. This current is called the
**photocurrent** (Iph).

### Our specific cells

The CHESS satellite uses **Azur Space 3G30C** cells. These are **triple-junction**
cells, meaning three layers of different materials are stacked:

| Layer | Material | Band gap | Absorbs |
|---|---|---|---|
| Top | GaInP | 1.9 eV | Blue/UV light |
| Middle | GaAs | 1.4 eV | Visible light |
| Bottom | Ge | 0.66 eV | Infrared light |

Each layer absorbs a different wavelength range, so together they capture more
of the solar spectrum than a single layer could. This gives an efficiency of
about 29% — meaning 29% of the incoming light energy is converted to electricity.

**Source:** Azur Space 3G30C datasheet, page 1.
https://www.azurspace.com/media/uploads/file_links/file/bdb_00010891-01-00_tj3g30-advanced_4x8.pdf

### Key numbers per cell (from the datasheet, page 2, "Electrical Data" table)

Look for the column marked "AM0" (Air Mass Zero — meaning in space, no atmosphere):

| Parameter | Symbol | Value | Where on datasheet |
|---|---|---|---|
| Open-circuit voltage | Voc | 2669 mV (2.669 V) | Page 2, row "Voc typ" |
| Short-circuit current | Isc | 525 mA (0.525 A) | Page 2, row "Isc typ" |
| Voltage at max power | Vmpp | ~2371 mV | Page 2, row "Vmpp typ" |
| Current at max power | Impp | ~504 mA | Page 2, row "Impp typ" |
| Efficiency | eta | 29.1% | Page 2, row "eta typ" |

### Temperature coefficients (from the datasheet, page 2, "Temperature Coefficients")

| Parameter | Value | Meaning |
|---|---|---|
| dVoc/dT | -6.0 mV/°C | Voltage drops 6 mV for every 1°C temperature increase |
| dIsc/dT | +0.35 mA/°C | Current increases 0.35 mA per 1°C increase |

---

## 3. The I-V curve: why solar panels are weird

If you connect a variable resistor to a solar cell and slowly increase the
resistance from zero to infinity, you trace out the cell's **I-V curve** — the
relationship between voltage and current:

```
Current (A)
  |
  |___________
  |            \
  |             \
  |              \
  |               \
  |                \____
  |__________________________ Voltage (V)
  0         Vmpp        Voc
  Isc
```

At the left edge (V = 0, short circuit): maximum current flows (Isc = 0.525 A),
but voltage is zero, so **power = V × I = 0 watts**.

At the right edge (V = Voc, open circuit): maximum voltage (2.669 V), but no
current flows, so **power = 0 watts** again.

Somewhere in the middle, both voltage and current are substantial, and their
product (power) reaches a peak. This peak is the **Maximum Power Point**.

### Why does the curve have this shape?

The curve comes from the **single-diode equation** — a physics equation that
describes how current flows through a solar cell:

```
I = Iph - Isat × (exp((V + I×Rs) / (n×Vt)) - 1) - (V + I×Rs) / Rsh
```

In plain English, each term means:

- **Iph**: the photocurrent — the electrons knocked free by light. This is
  roughly constant regardless of voltage. More light = more Iph.

- **Isat × (exp(...) - 1)**: the diode current. As voltage increases, the diode
  "turns on" and starts consuming photocurrent internally. The exponential
  function makes this term negligible at low voltage but dominant near Voc.

- **(V + I×Rs) / Rsh**: leakage current through parasitic paths in the cell.
  Usually small.

- **I**: the current that actually flows out to the external circuit (what we use).

The equation says: the current you get = photocurrent minus what the diode
consumes minus what leaks. As voltage increases, the diode consumes more and
more, until at Voc it consumes everything and no current flows out.

---

## 4. The Maximum Power Point

Power = Voltage × Current. If we plot power versus voltage, we get a curve
with a single peak:

```
Power (W)
  |
  |        * ← MPP (Maximum Power Point)
  |       / \
  |      /   \
  |     /     \
  |    /       \
  |___/         \______
  |_________________________ Voltage (V)
  0     Vmpp           Voc
```

The peak is the **Maximum Power Point (MPP)**. For our array at 25°C with full
sunlight:

- **MPP voltage** ≈ 17.8 V (for the full array)
- **MPP current** ≈ 2.05 A
- **MPP power** ≈ 36.6 W

The MPP is not fixed — it shifts when conditions change:

- **Temperature increases** → Voc drops (strongly), Isc rises (slightly) →
  MPP shifts **left** (lower voltage) and **down** (less total power).

- **Irradiance decreases** (cloud, angle change, eclipse approach) →
  Isc drops proportionally → MPP shifts **down** and slightly **left**.

The algorithm must continuously track these shifts.

---

## 5. How do you control where you operate on the curve?

The solar panel's operating point is determined by the load it sees. If the
load draws lots of current, the panel operates at low voltage (left side of
the curve). If the load draws little current, the panel operates at high
voltage (right side).

Between the solar panel and the battery sits a **buck converter** — an
electronic circuit that steps voltage down. Think of it like a gear ratio:

```
Solar Panel ──→ [Buck Converter] ──→ Battery
  V_panel          D (duty cycle)       V_battery ≈ 7.4V
  ~18V                                  (fixed by chemistry)
```

The buck converter has a control parameter called the **duty cycle** (D) — the
fraction of time its internal switch is ON. The fundamental relationship is:

```
V_out = D × V_in
```

Since V_out = V_battery (roughly fixed at 7.4V for our 2S Li-ion pack):

```
V_panel = V_battery / D
```

**This is the key insight.** By changing D, the microcontroller controls what
voltage the solar panel operates at:

- **Increase D** → V_panel decreases → panel moves **left** on the I-V curve
  (lower voltage, higher current)

- **Decrease D** → V_panel increases → panel moves **right** on the I-V curve
  (higher voltage, lower current)

### Our specific converter

- **Chip:** EPC2152 GaN half-bridge (radiation-tested at CERN)
- **Topology:** Synchronous buck (step-down)
- **Switching frequency:** 300 kHz
- **Output:** 8V battery bus (range 6V–8.4V)
- **Inductor:** 33 μH
- **Output capacitors:** 4.7 μF + 10 μF + 10 μF

**Source:** CHESS mission document, section 3.4.2.2.1, page 96–97.

---

## 6. MPPT algorithms: two approaches

There are two main algorithms used in practice. Both work by measuring the
panel's voltage and current, computing power, and adjusting the duty cycle.

### Approach 1: Perturb and Observe (P&O)

The simplest approach. Each iteration:

1. Slightly change the duty cycle (the "perturbation")
2. Measure the new power
3. If power went up → keep going in the same direction
4. If power went down → reverse direction

```
Pseudocode:
  power_now = voltage × current
  if power_now > power_previous:
      keep same direction
  else:
      reverse direction
  apply one step in the chosen direction
  power_previous = power_now
```

**Pros:** Very simple. Only needs current power and previous power.
**Cons:** Always oscillates around the MPP (never settles exactly). Can be
confused by rapid irradiance changes.

### Approach 2: Incremental Conductance (IncCond)

A smarter approach. Instead of comparing power values, it computes the **slope**
of the power curve (dP/dV) and uses that to determine which side of the MPP
it is on.

The math: at the MPP, the derivative of power with respect to voltage is zero:

```
dP/dV = 0
```

Since P = V × I, expanding the derivative:

```
dP/dV = I + V × (dI/dV) = 0
```

Therefore at the MPP:

```
dI/dV = -I/V
```

The algorithm approximates dI/dV from consecutive measurements:

```
dI/dV ≈ ΔI / ΔV = (I_now - I_prev) / (V_now - V_prev)
```

And compares this with -I/V:

- **ΔI/ΔV > -I/V** → left of MPP → voltage should increase
- **ΔI/ΔV < -I/V** → right of MPP → voltage should decrease
- **ΔI/ΔV = -I/V** → at MPP → hold

**Pros:** Theoretically does not oscillate at steady state (stops when it
reaches the MPP). Better tracking under changing conditions.
**Cons:** More complex. Requires computing ΔI and ΔV (sensitive to noise).
Needs division (ΔI/ΔV), which is expensive on our processor.

---

## 7. Why we chose Incremental Conductance

The CHESS Pathfinder 0 mission document (section 3.4.2.2.2, page 97) specifies
Incremental Conductance:

> *"MPPT Control: The MCU continuously measures the panel's instantaneous current
> and voltage and their small variations. Using the Incremental Conductance
> algorithm, it compares these slopes to determine whether the operating point is
> to the left or right of the Maximum Power Point, and adjusts the PWM duty cycle
> accordingly."*

The project's own `docs/mppt_algorithm.md` recommends starting with P&O for
simplicity, but the mission document is authoritative. We implement IncCond.

---

## 8. The algorithm in detail

### The decision logic

Every N samples (N = 8 for noise averaging), the algorithm makes one decision:

```
1. Compute averaged voltage and current from the last 8 ADC readings
2. Compute ΔV = V_averaged - V_previous_averaged
3. Compute ΔI = I_averaged - I_previous_averaged
4. Compare ΔI/ΔV with -I/V to determine position relative to MPP
5. Adjust duty cycle by one step in the appropriate direction
```

### The cross-multiplication trick

The comparison `ΔI/ΔV > -I/V` requires division. Our microcontroller (Cortex-M0+)
has no hardware divider — division is emulated in software and costs ~40 clock
cycles. To avoid this, we cross-multiply:

Instead of comparing:
```
ΔI/ΔV  >  -I/V
```

We compare:
```
ΔI × V  >  -I × ΔV    (when ΔV > 0)
ΔI × V  <  -I × ΔV    (when ΔV < 0, inequality flips)
```

The maximum values are 4095 × 4095 = 16,769,025, which fits in a 32-bit
integer (max 2,147,483,647). No overflow possible.

### Sign convention for the buck converter

This is the trickiest part. The standard IncCond algorithm says "increase voltage"
or "decrease voltage". But we control **duty cycle**, not voltage directly. For
a buck converter:

- "Increase voltage" → **decrease** D (because V_panel = V_battery / D)
- "Decrease voltage" → **increase** D

Getting this wrong causes the algorithm to diverge (we found this bug during
development — see Section 15).

### The actual code

From `src/mppt_algorithm.c`:

```c
if (change_is_within_noise_threshold(delta_voltage)) {
    if (change_is_within_noise_threshold(delta_current)) {
        // ΔI ≈ 0 and ΔV ≈ 0: at or near MPP. Hold.
    } else if (delta_current > 0) {
        // Current increased at same voltage → irradiance increased.
        // MPP shifted to higher voltage.
        // For buck: decrease D to raise panel voltage (move right).
        duty_cycle = decrease_duty_cycle_by_one_step(duty_cycle);
    } else {
        // Current decreased at same voltage → irradiance decreased.
        // For buck: increase D to lower panel voltage (move left).
        duty_cycle = increase_duty_cycle_by_one_step(duty_cycle);
    }
} else {
    // Cross-multiply: ΔI × V vs -I × ΔV
    int32_t left_side =
        delta_current * (int32_t)averaged_voltage;
    int32_t right_side =
        -((int32_t)averaged_current) * delta_voltage;

    if (left_side == right_side) {
        // At MPP exactly. Hold.
    } else if (
        (delta_voltage > 0 && left_side > right_side)
        ||
        (delta_voltage < 0 && left_side < right_side)) {
        // LEFT of MPP (dP/dV > 0). Voltage should increase.
        // For buck: decrease D to raise panel voltage.
        duty_cycle = decrease_duty_cycle_by_one_step(duty_cycle);
    } else {
        // RIGHT of MPP (dP/dV < 0). Voltage should decrease.
        // For buck: increase D to lower panel voltage.
        duty_cycle = increase_duty_cycle_by_one_step(duty_cycle);
    }
}
```

---

## 9. Integer-only implementation (no floating point)

The SAMD21RT (Cortex-M0+) has **no hardware floating-point unit**. Floating-point
operations work but are emulated in software, costing 10–20 clock cycles each.
For a control loop running at 100 Hz this would be acceptable, but we chose
integer-only arithmetic for two reasons:

1. **Portability:** the algorithm code (`src/mppt_algorithm.c`) runs identically
   on the chip and in the simulation. No behavior differences between float
   rounding on x86 and software float on ARM.

2. **Determinism:** integer arithmetic produces exactly the same result on every
   platform. This makes testing definitive.

### How values are represented

| Quantity | Type | Range | Resolution |
|---|---|---|---|
| ADC voltage reading | `uint16_t` | 0–4095 | 12-bit, ~6 mV per count |
| ADC current reading | `uint16_t` | 0–4095 | 12-bit, ~0.7 mA per count |
| Duty cycle | `uint16_t` | 0–65535 | 0.0015% per count |
| Power (V×I) | `uint32_t` | 0–16,769,025 | fits in 32 bits |
| Deltas (ΔV, ΔI) | `int32_t` | -4095 to +4095 | signed for direction |
| Cross products | `int32_t` | ±16,769,025 | fits in 32 bits |

---

## 10. The physics model: simulating a solar panel

The simulation needs to answer: "If the panel voltage is X volts and the
temperature is T degrees and the irradiance is G%, what current does the
panel produce?"

We use the **five-parameter single-diode model**:

```
I = Iph - Isat × (exp((V + I×Rs) / (n×Ns×Vt)) - 1) - (V + I×Rs) / Rsh
```

Where:
- **Iph** = photocurrent (proportional to light intensity)
- **Isat** = diode saturation current (increases with temperature)
- **Rs** = series resistance (wiring, contacts)
- **Rsh** = shunt resistance (leakage paths)
- **n** = diode ideality factor (1.3 for our cells)
- **Ns** = number of cells in series (7 for 4P configuration)
- **Vt** = thermal voltage = kT/q (25.7 mV at 25°C)

### The equation is implicit — you can't solve it directly

Notice that I appears on both sides: on the left as the result, and on the
right inside the exponential (through the I×Rs term). This means you can't
just plug in V and compute I with a formula. You need an iterative solver.

We use **Newton-Raphson iteration**: start with an initial guess (I = Iph),
then repeatedly refine it:

```c
for (iteration = 0; iteration < 50; iteration++) {
    // f(I) = Iph - Isat*(exp(...) - 1) - (V+I*Rs)/Rsh - I
    // f'(I) = derivative of f with respect to I
    correction = f(I) / f'(I);
    I = I - correction;
    if (|correction| < 0.000001) break;  // converged
}
```

This typically converges in 5–10 iterations.

### Temperature dependence

Two things change with temperature:

**1. Photocurrent increases slightly:**
```
Iph(T) = Iph_ref × (G / G_ref) × (1 + dIsc/dT × (T - T_ref))
```
At 80°C: Iph ≈ 2.1 × 1.0 × (1 + 0.00035 × 52) ≈ 2.14 A (+1.8%)

**2. Saturation current increases dramatically:**
```
Isat(T) = Isat_ref × (T/T_ref)³ × exp(q×Eg/(n×k) × (1/T_ref - 1/T))
```
This exponential increase in Isat is the main reason Voc drops with
temperature. As Isat grows, the diode "turns on" at a lower voltage, pushing
Voc down by about 42 mV per degree C for the whole array.

### Fitting the model to our cells

The five parameters are derived from the datasheet:

| Parameter | Value (array level, 4P) | How derived |
|---|---|---|
| Iph_ref | 2.10 A | = 4 × Isc_cell = 4 × 0.525 A |
| Voc_ref | 18.68 V | = 7 × Voc_cell = 7 × 2.669 V |
| n | 1.3 | Typical for triple-junction GaAs |
| Rs | 0.035 Ω | 0.020 Ω/cell × 7 series / 4 parallel |
| Rsh | 1750 Ω | 1000 Ω/cell × 7 series / 4 parallel |
| Eg | 3.4 eV | Fitted to match dVoc/dT (see Section 15) |

**Implementation:** `tests/solar_panel_simulator.c`

---

## 11. The buck converter model

The converter is modeled as an ideal transformer:

```c
double panel_voltage = battery_voltage / duty_cycle_fraction;
```

Where `duty_cycle_fraction` = D_raw / 65535 (converting from the uint16_t
representation to a 0.0–1.0 fraction).

This is a simplification — a real converter has losses, inductor current
ripple, and a settling time after each duty cycle change. For algorithm
validation, the ideal model is sufficient because:

1. Converter efficiency is ~95% — the 5% loss shifts the MPP slightly but
   doesn't change the algorithm's behavior.
2. Settling time (~1 ms) is much shorter than the control period (~80 ms
   with 8-sample averaging at 100 Hz).
3. Inductor current ripple is smoothed by the output capacitors.

### ADC conversion

The simulation also models the ADC measurement chain. Real panel voltage
and current are converted to 12-bit ADC readings:

```c
voltage_adc = (panel_voltage / reference_voltage) × 4095
current_adc = (panel_current / reference_current) × 4095
```

With optional noise injection (±2% random variation) to simulate real
measurement uncertainty.

**Implementation:** `tests/buck_converter_model.c`

---

## 12. The closed-loop simulation

The simulation runs the complete control loop that would execute on the real
chip. Each iteration:

```
┌─────────────────────────────────────────────────────────┐
│ 1. Algorithm outputs duty cycle D                       │
│           │                                             │
│ 2. Buck model: V_panel = V_battery / D                  │
│           │                                             │
│ 3. Panel model: I_panel = f(V_panel, T, G)              │
│           │     (single-diode equation)                 │
│ 4. Add ADC noise (±2%), quantize to 12-bit              │
│           │                                             │
│ 5. Algorithm receives (V_adc, I_adc) as uint16_t        │
│           │     (IDENTICAL code to what runs on chip)    │
│ 6. Algorithm computes new D                             │
│           │                                             │
│           └──── loop ────────────────────────────────────│
└─────────────────────────────────────────────────────────┘
```

The algorithm code (`src/mppt_algorithm.c`) is **byte-for-byte identical** to
what will be flashed onto the SAMD21RT. It receives `uint16_t` ADC values and
returns a `uint16_t` duty cycle. It has no idea whether it's running on the
chip or in the simulation.

### Six test scenarios

| # | Scenario | What it tests |
|---|---|---|
| 1 | Steady state, 100% irradiance, 25°C | Basic convergence to MPP |
| 2 | Steady state, 50% irradiance, 25°C | Convergence at reduced power |
| 3 | Steady state, 100% irradiance, 80°C | Hot panel (Voc shifts down) |
| 4 | Step change: 100% → 50% irradiance at iteration 250 | Re-convergence after sudden change |
| 5 | Temperature ramp: 25°C → 80°C over 1000 iterations | Tracking a moving MPP |
| 6 | Eclipse entry: irradiance → 0% at iteration 250 | Graceful degradation |

**Pass criterion:** 90%+ of post-transient iterations produce power within
2% of the theoretical MPP.

**Implementation:** `tests/mppt_simulation_runner.c`

---

## 13. ADC noise and the moving average filter

### The problem

The ADC readings have noise — random variations of about ±2% of the reading.
At the MPP, a typical voltage ADC reading is ~2900 counts. Noise = ±2% ×
2900 ≈ ±58 counts.

When the algorithm changes the duty cycle by one step (328 counts = 0.5%),
the resulting voltage change is about 0.2V, which is ~36 ADC counts.

**The noise (±58 counts) is larger than the signal (36 counts).** The
algorithm cannot distinguish real changes from noise, and makes random
decisions.

### The solution: moving average

Instead of making a decision on every single ADC reading, the algorithm
collects 8 readings and averages them before making one decision. Averaging
N samples reduces noise by a factor of √N:

```
Noise after averaging = ±58 / √8 = ±20.5 counts
Signal = 36 counts
Signal-to-noise ratio = 36 / 20.5 = 1.76
```

This is sufficient for reliable decisions (>90% correct).

### Trade-off

Averaging 8 samples means the algorithm makes one decision every 8 control
periods. At 100 Hz sampling, this is one decision every 80 ms. This is fine
for solar panel tracking — irradiance changes on a timescale of seconds
(eclipse transitions) to minutes (orbital thermal cycling), not milliseconds.

### Implementation

From `src/mppt_algorithm.c`:

```c
// Accumulate samples
state->voltage_accumulator_for_averaging += voltage_raw_adc_reading;
state->current_accumulator_for_averaging += current_raw_adc_reading;
state->samples_collected_in_current_window += 1u;

// If not enough samples yet, return current duty cycle unchanged
if (state->samples_collected_in_current_window
    < MPPT_MOVING_AVERAGE_WINDOW_SIZE) {
    return state->current_duty_cycle_as_fraction_of_65535;
}

// Compute average
uint16_t averaged_voltage = (uint16_t)(
    state->voltage_accumulator_for_averaging
    / MPPT_MOVING_AVERAGE_WINDOW_SIZE);
```

---

## 14. Simulation results

All six scenarios pass with the final tuned parameters:

| Scenario | Description | Convergence | Verdict |
|---|---|---|---|
| 1 | 100% irradiance, 25°C | 100% within 2% | **PASS** |
| 2 | 50% irradiance, 25°C | 91% within 2% | **PASS** |
| 3 | 100% irradiance, 80°C | 99% within 2% | **PASS** |
| 4 | Step change 100→50% | 93% within 2% | **PASS** |
| 5 | Temp ramp 25→80°C | 93% within 2% | **PASS** |
| 6 | Eclipse entry | 100% within 2% | **PASS** |

### Tuned parameters

| Parameter | Value | Why |
|---|---|---|
| Step size | 328 counts (0.5%) | Large enough for SNR>1.7 after averaging |
| Noise threshold | 5 ADC counts | Filters quantization noise |
| Moving average window | 8 samples | Reduces noise by factor √8 ≈ 2.8 |
| Min duty cycle | 5% (3277) | Prevents converter disconnect |
| Max duty cycle | 95% (62258) | Prevents high-side switch short |
| Initial duty cycle | 50% (32768) | Mid-range starting point |

---

## 15. Bugs found during development

Three significant bugs were found and fixed during simulation development.
These bugs would have caused the real firmware to fail in orbit if not caught.

### Bug 1: Sign convention error in the ΔV≈0 case

**Symptom:** Algorithm diverged to V >> Voc instead of converging to MPP.

**Root cause:** When ΔV ≈ 0 and ΔI > 0 (current increased at same voltage,
suggesting irradiance increased), the algorithm should decrease D to raise
panel voltage (tracking the shifted MPP to higher voltage). But the code
was doing the opposite — increasing D, which lowered voltage further.

**Fix:** Swapped `increase_duty_cycle` and `decrease_duty_cycle` in the
ΔV≈0 branch.

### Bug 2: Exponential clamp too aggressive in solar panel model

**Symptom:** Panel model returned 1.0 A of current at V = 25 V, which is
physically impossible (V >> Voc ≈ 18.7 V, so current should be 0).

**Root cause:** The Newton-Raphson solver clamped `exp(x)` to `exp(80)` to
prevent overflow. But `Isat × exp(80)` was only ~1.1 A — not large enough
to fully cancel the photocurrent of 2.1 A. So the solver converged to
I ≈ 2.1 - 1.1 = 1.0 A instead of I ≈ 0.

**Fix:** Increased the clamp from `exp(80)` to `exp(500)`. On 64-bit doubles,
`exp(500)` ≈ 1.4×10²¹⁷, well within range (max ≈ 1.8×10³⁰⁸).

### Bug 3: Wrong bandgap energy for temperature scaling

**Symptom:** Scenario 3 (80°C) showed the algorithm producing more power
than the theoretical MPP — which is physically impossible.

**Root cause:** The bandgap energy was set to 1.12 eV (silicon), but our
triple-junction GaAs cells have a much higher effective Eg. With Eg = 1.12 eV,
the saturation current did not increase fast enough with temperature, so the
model's Voc at 80°C was 20.4 V instead of the correct 16.5 V. The model
allowed current at voltages where the real panel would produce none.

**Fix:** Derived the correct Eg from the datasheet temperature coefficient:
```
dVoc/dT ≈ (Voc - n×Eg) / T
-0.006 ≈ (2.669 - 1.3×Eg) / 301
Eg ≈ 3.4 eV
```
With Eg = 3.4 eV, the model's Voc matches the datasheet at all temperatures.

---

## 16. Hypotheses and assumptions

Every simulation makes assumptions. Here are ours, explicitly listed:

### Verified hypotheses (supported by datasheet or mission document)

| # | Hypothesis | Source |
|---|---|---|
| H1 | Solar cells are Azur Space 3G30C, Voc=2.669V, Isc=0.525A per cell | Datasheet p2, mission doc §3.4.6.1 |
| H2 | Buck converter: EPC2152, 300 kHz, V_out = D × V_in | Mission doc §3.4.2.2.1, p96 |
| H3 | Algorithm: Incremental Conductance | Mission doc §3.4.2.2.2, p97 |
| H4 | Battery voltage range: 6.0V–8.4V (2S Li-ion) | Mission doc Table 3.4.1, p94 |
| H5 | Panel temperature range: -40°C to 105°C | Mission doc Table 3.11.2, p227 |
| H6 | dVoc/dT = -6.0 mV/°C per cell | Datasheet p2 |
| H7 | dIsc/dT = +0.35 mA/°C per cell | Datasheet p2 |
| H8 | Orbit: 475 km SSO, 94 min period, max 37 min eclipse | Mission doc §1.6, p19 |

### Unverified hypotheses (require confirmation)

| # | Hypothesis | Risk if wrong | Action needed |
|---|---|---|---|
| U1 | Panel electrical config is 4P (all parallel) | If 2S2P, Voc=37.4V not 18.7V — completely different converter operating range | Verify with hardware team |
| U2 | ADC noise is ±2% | If higher, need larger averaging window | Measure on real hardware |
| U3 | Converter settling time < control period | If not, need to add delay in algorithm | Measure on real hardware |
| U4 | Single-diode model is adequate for IncCond validation | If panel has partial shading or mismatch, model is insufficient | Compare with real panel data |

### Model simplifications

| Simplification | Why it's acceptable |
|---|---|
| Ideal buck converter (no losses) | ~95% efficiency shifts MPP by ~2%, within tolerance |
| No inductor current ripple | Smoothed by output capacitors |
| No converter dynamics (instant settling) | Settling time ~1 ms << control period ~80 ms |
| Fixed battery voltage per simulation run | Battery voltage changes on minute timescale, algorithm runs at Hz |
| No partial shading | All 4 panels are on the same face (Z+), same illumination |
| No cell mismatch | Using datasheet typical values, not worst-case |

---

## 17. How to build and run

### Prerequisites

- **GCC** (native, not ARM cross-compiler): installed via
  `winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT`
- **Make**: already installed (from firmware toolchain setup)
- **Python 3.10+** with pip (for the Streamlit app)

### Build the C simulation

```bash
cd c:\Users\iceoc\Documents\EPS-mppt-algorithm
make -f Makefile.sim
```

This produces `tests/run_mppt_simulation.exe`.

### Run a single scenario

```bash
./tests/run_mppt_simulation.exe 1 4p        # scenario 1, 4P config
./tests/run_mppt_simulation.exe 3 4p 8.0    # scenario 3, battery at 8.0V
./tests/run_mppt_simulation.exe 1 2s2p      # scenario 1, 2S2P config
```

CSV output goes to stdout. Verdict goes to stderr.

### Run all scenarios

```bash
make -f Makefile.sim runall
```

### Launch the Streamlit visualization app

```bash
pip install -r tools/requirements.txt
streamlit run tools/mppt_app.py
```

Opens a browser with interactive controls, live plots, and expandable
physics explanations.

---

## 18. File inventory

All files are on the `mppt-algorithm` branch, in the git worktree at
`c:\Users\iceoc\Documents\EPS-mppt-algorithm`.

### Algorithm code (will be flashed to the chip)

| File | Purpose |
|---|---|
| `src/mppt_algorithm.h` | Public interface: constants, state struct, function declarations |
| `src/mppt_algorithm.c` | IncCond implementation: integer-only, cross-multiplication, 8-sample average |
| `src/assertion_handler.h` | SATELLITE_ASSERT macro (same as in conventions.md) |
| `src/assertion_handler.c` | Desktop assertion handler: prints to stderr, exits |

### Simulation code (PC only, never goes on the chip)

| File | Purpose |
|---|---|
| `tests/solar_panel_simulator.h` | Panel model interface |
| `tests/solar_panel_simulator.c` | Five-parameter single-diode model with Newton-Raphson solver |
| `tests/buck_converter_model.h` | Converter model interface |
| `tests/buck_converter_model.c` | Ideal buck + ADC noise injection |
| `tests/mppt_simulation_runner.c` | Closed-loop sim, 6 scenarios, CSV output |

### Build and visualization

| File | Purpose |
|---|---|
| `Makefile.sim` | Host GCC build for the simulation |
| `tools/mppt_app.py` | Streamlit interactive visualization app |
| `tools/requirements.txt` | Python dependencies |

### Documentation

| File | Purpose |
|---|---|
| `docs/simulation_of_MPPT.md` | This document |
| `docs/mppt_algorithm.md` | General MPPT theory (pre-existing) |

---

## 19. References

1. **Azur Space 3G30C Datasheet** (primary source for solar cell parameters)
   https://www.azurspace.com/media/uploads/file_links/file/bdb_00010891-01-00_tj3g30-advanced_4x8.pdf
   - Page 2: Electrical Data (Voc, Isc, Vmpp, Impp, efficiency)
   - Page 2: Temperature Coefficients (dVoc/dT, dIsc/dT)

2. **CHESS Pathfinder 0 Satellite Project File** (mission document)
   - Section 3.4.2.2.1, page 96: Buck converter specs (EPC2152, 300 kHz)
   - Section 3.4.2.2.2, page 97: MPPT algorithm choice (Incremental Conductance)
   - Section 3.4.6, page 113: Solar array configuration (4 panels, 7 cells each)
   - Table 3.4.1, page 94: EPS key parameters (voltage/current ranges)
   - Table 3.11.2, page 227: Component temperature limits
   - Section 1.6, page 19: Orbit parameters (475 km SSO, eclipse times)

3. **Esram & Chapman (2007)** — "Comparison of Photovoltaic Array Maximum Power
   Point Tracking Techniques", IEEE Transactions on Energy Conversion.
   Foundational paper comparing P&O and IncCond algorithms.

4. **Hussein et al. (1995)** — "A Novel Maximum Power Point Tracker for
   Photovoltaic Arrays", IEEE Transactions on Power Electronics.
   Original Incremental Conductance paper.

5. **NASA Power of 10 Rules** (Holzmann, 2006) — coding standard applied to
   the algorithm implementation via `conventions.md`.

6. **JPL Institutional Coding Standard D-60411** (2009) — additional safety
   rules for the firmware.
