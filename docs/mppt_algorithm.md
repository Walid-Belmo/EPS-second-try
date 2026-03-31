# MPPT Algorithm

This document covers the two main MPPT algorithm approaches, why the choice
depends on the DC/DC converter topology, and how to test the algorithm on a
laptop before touching any hardware.

---

## What MPPT Is

A solar panel is a non-linear power source. For a given illumination level,
it has a unique operating point (voltage, current pair) that produces maximum
power. This point shifts continuously as illumination changes.

The goal of MPPT is to continuously find and track this Maximum Power Point (MPP)
by adjusting the operating point through control of the DC/DC converter switching.

The relationship between voltage and power for a solar panel has one peak (the MPP).
Below the MPP voltage, power increases with voltage. Above it, power decreases.

```
Power
  │
  │        ← MPP →
  │       /         \
  │      /            \
  │     /               \
  │    /                  \
  │___/_____________________ Voltage
       Vmpp              Voc
```

The algorithm's job: move the operating point toward this peak continuously.

---

## How Control Works

The DC/DC converter changes the effective load seen by the solar panel by
varying its switching duty cycle. The relationship between duty cycle and
operating point depends entirely on the converter topology:

- **Boost converter:** Higher duty cycle → lower input voltage → moves operating
  point left on the I-V curve
- **Buck converter:** Higher duty cycle → higher input voltage → moves right
- **SEPIC/Ćuk:** Different relationships again

This is why the algorithm code must know the converter topology before it can
correctly determine whether to increase or decrease duty cycle in response to
a measured power change.

**This decision cannot be made until the DC/DC converter is specified.**
The algorithm structure (P&O or IncCond) can be written and tested now, but the
sign of the duty cycle adjustment step must match the topology.

---

## Algorithm Approach 1: Perturb and Observe (P&O)

**How it works:**

Every control iteration, slightly adjust the duty cycle in one direction
(perturbation). Measure the resulting power. If power increased, keep going
in the same direction. If power decreased, reverse direction.

```
Pseudocode:
  power_now = voltage_now × current_now
  delta_power = power_now - power_previous

  if delta_power > 0:
    if duty_cycle increased last step: keep increasing
    if duty_cycle decreased last step: keep decreasing
  else:
    if duty_cycle increased last step: start decreasing
    if duty_cycle decreased last step: start increasing

  power_previous = power_now
```

**Advantages:**
- Simple to implement and understand
- Only 3 measurements needed per iteration (voltage, current, previous power)
- Well-documented behaviour in literature
- Used in the vast majority of small satellite power systems

**Disadvantages:**
- Oscillates around the MPP — never settles exactly on it, always ±1 step
- Confused by rapid irradiance changes (can track in the wrong direction temporarily)
- Step size is a fixed tradeoff: large step = fast tracking, large oscillation;
  small step = slow tracking, small oscillation

**Recommended starting point.** For a first implementation, start with P&O.
It is well understood and its failure modes are predictable.

Source: "Comparison of Photovoltaic Array Maximum Power Point Tracking Techniques"
        Esram & Chapman, IEEE Transactions on Energy Conversion, 2007
        (foundational paper, widely cited in satellite power literature)

---

## Algorithm Approach 2: Incremental Conductance (IncCond)

**How it works:**

At the MPP, the derivative of power with respect to voltage is zero:
dP/dV = 0. Since P = V × I, this expands to: I + V × (dI/dV) = 0.

IncCond approximates this derivative from discrete measurements:
- dI/dV ≈ ΔI/ΔV = (I_now - I_prev) / (V_now - V_prev)

```
Pseudocode:
  delta_V = voltage_now - voltage_previous
  delta_I = current_now - current_previous

  if delta_V == 0:
    if delta_I == 0: at MPP, do nothing
    if delta_I > 0:  below MPP, increase duty
    if delta_I < 0:  above MPP, decrease duty
  else:
    conductance_slope = delta_I / delta_V
    instantaneous_conductance = -current_now / voltage_now
    if conductance_slope > instantaneous_conductance: below MPP
    if conductance_slope < instantaneous_conductance: above MPP
    if conductance_slope == instantaneous_conductance: at MPP
```

**Advantages:**
- Does not oscillate at steady state (stops when MPP is found)
- More accurate under rapidly changing irradiance
- Theoretically more efficient in stable conditions

**Disadvantages:**
- More complex implementation
- Division required (expensive on Cortex-M0+ which has no hardware divider)
- Sensitive to noise in ADC readings (ΔV and ΔI can be very small numbers)
- Fixed-point implementation requires careful scaling to avoid overflow

**Recommendation:** Implement after P&O is working and verified. Use IncCond if
oscillation around the MPP proves to be a problem in practice.

Source: Same Esram & Chapman 2007 reference above.
Source: "A Novel Maximum Power Point Tracker for Photovoltaic Arrays"
        Hussein et al., IEEE Transactions on Power Electronics, 1995 (original IncCond paper)

---

## Laptop Simulation — Testing Before Hardware

The algorithm file (`mppt_algorithm.c`) must have zero hardware dependencies.
It takes two `uint16_t` values representing raw ADC readings and returns an updated
duty cycle. This signature allows it to be compiled and run on a laptop.

**Solar panel I-V curve simulation approach:**

A real solar panel's I-V curve follows the single-diode model. A simplified
linear approximation is sufficient for algorithm validation:

```
I(V) = Isc × (1 - V/Voc) × irradiance_fraction
P(V) = V × I(V)
```

Where:
- Isc = short-circuit current (maximum current, at V=0)
- Voc = open-circuit voltage (maximum voltage, at I=0)
- irradiance_fraction = 0.0 (dark) to 1.0 (full sun)

The DC/DC converter maps duty cycle to operating voltage:
- This mapping depends on topology — use a placeholder linear relationship
  for simulation, replace with the real relationship once topology is known

**What the simulation measures:**

Run 200 iterations of the algorithm against the simulated panel. At each step:
1. Convert duty cycle to operating voltage (via converter model)
2. Calculate current from I-V model
3. Calculate power
4. Log: iteration, duty_cycle, voltage, current, power to CSV
5. Feed ADC-equivalent values to the algorithm
6. Observe the algorithm's duty cycle converge toward the MPP voltage

Output a CSV file. Open in Excel or plot with Python/matplotlib to visualize convergence.

**Compile and run on Windows (inside MSYS2):**

```bash
# Compile the algorithm + simulator + test harness using native GCC (not ARM)
gcc -Wall -Wextra -std=c99 \
    -DTESTING_ON_HOST_NOT_ON_CHIP \
    src/mppt_algorithm.c \
    tests/mppt_simulation_test.c \
    -lm -o tests/run_mppt_simulation

./tests/run_mppt_simulation > simulation_output.csv
```

The `-DTESTING_ON_HOST_NOT_ON_CHIP` define allows `mppt_algorithm.c` to skip
any hardware-specific includes (it should have none, but the define provides
a safety net).

**What good convergence looks like:**

```
step, duty_cycle, voltage, current, power
0,    0.300,      14.7,    2.45,    36.0
10,   0.320,      13.6,    2.62,    35.6
20,   0.340,      12.5,    2.80,    35.0
...
```

Wait — if power is decreasing, the algorithm is going the wrong direction.
Check the duty-cycle-to-voltage mapping for your converter topology.

Good convergence: power increases monotonically toward a stable peak value.
Bad convergence: power oscillates without settling, or trends consistently downward.

---

## Things to Be Careful About

**The step size is critical.** Too large: the algorithm overshoots and oscillates
badly. Too small: it takes too long to reach the MPP after an irradiance change.
The optimal step size depends on the panel's I-V curve characteristics and the
expected rate of irradiance change. Start with a small step and increase if
tracking speed is insufficient.

**ADC noise affects both algorithms.** If the ADC reads slightly different values
on consecutive samples with no real change in voltage or current, the algorithm
sees phantom power changes and dithers. Consider averaging 2-4 ADC readings before
feeding them to the algorithm.

**Integer vs floating-point.** On Cortex-M0+, there is no hardware FPU and no
hardware divider. Float operations work but are emulated in software (~10-20 cycles
per operation). Integer division is also software-emulated (~30-40 cycles). For a
control loop running at 100 Hz this is fine. For a loop running at 10 kHz, consider
fixed-point arithmetic. Profile before optimizing.

**The duty cycle must be bounded.** The algorithm must never output a duty cycle
of 0% or 100%. A 0% duty cycle means the converter is off. A 100% duty cycle
saturates most converter topologies and can damage hardware. Clamp the output:

```c
#define MPPT_MINIMUM_DUTY_CYCLE_FRACTION  0.05f
#define MPPT_MAXIMUM_DUTY_CYCLE_FRACTION  0.95f
```

**Test under multiple irradiance levels.** An algorithm that works at full sun
may not recover correctly from partial shading. Run the simulation at 100%, 50%,
and 25% irradiance, including sudden transitions between levels.
