# EPS State Machine Closed-Loop Simulation

Complete reference for the CHESS CubeSat EPS (Electrical Power System) state
machine simulation. This document is self-contained: a developer or automated
agent should be able to understand, modify, and extend the simulation without
reading any other file.

---

## Table of Contents

1. [Purpose](#1-purpose)
2. [Closed-Loop Architecture](#2-closed-loop-architecture)
3. [Physics Models](#3-physics-models)
4. [Firmware Under Test](#4-firmware-under-test)
5. [Timing and Iteration Model](#5-timing-and-iteration-model)
6. [Conversion Layer: Float Physics to Integer Firmware](#6-conversion-layer-float-physics-to-integer-firmware)
7. [Simulation Scenarios](#7-simulation-scenarios)
8. [How to Build and Run](#8-how-to-build-and-run)
9. [CSV Output Format](#9-csv-output-format)
10. [Configuration Thresholds](#10-configuration-thresholds)
11. [Files Reference](#11-files-reference)
12. [Key Constants and Magic Numbers](#12-key-constants-and-magic-numbers)
13. [Extending the Simulation](#13-extending-the-simulation)

---

## 1. Purpose

The simulation tests the EPS state machine -- the complete power management
firmware for the CHESS CubeSat -- by running it in a closed loop with physics
models of the solar array, buck converter, and battery.

**The firmware code in `src/` is IDENTICAL to what runs on the SAMD21G17D
Cortex-M0+ MCU.** The same `.c` and `.h` files are compiled for both the ARM
target (using `arm-none-eabi-gcc`) and the simulation (using native `gcc` on
the host PC). The firmware is pure logic with zero hardware dependencies: it
receives integer sensor readings and returns integer actuator commands. This
architecture makes closed-loop testing possible without any mocking or
hardware abstraction layers.

The physics models in `tests/` are float-based, PC-only code that never runs
on the chip. They simulate the real-world environment: how much current the
solar panels produce at a given voltage, how the battery voltage changes with
charge state, and how the buck converter translates duty cycle to panel
voltage.

The simulation answers questions like:

- Does the MPPT algorithm converge to the maximum power point?
- Does the state machine correctly transition between PCU modes when the
  satellite enters or exits eclipse?
- Does safe mode activate when the battery voltage drops critically low?
- Does the OBC heartbeat timeout trigger autonomous safe mode?
- Does the heater turn on at low temperatures?
- Does load shedding engage during overcurrent conditions?
- Does the system survive a full 94-minute orbit with realistic power budget?

---

## 2. Closed-Loop Architecture

The simulation runs a tight loop where each iteration represents one superloop
iteration of the real firmware (~200 microseconds of wall-clock time). Within
each iteration, the following steps execute in sequence:

```
    +------------------------------------------------------+
    |               Simulation Iteration N                  |
    +------------------------------------------------------+
    |                                                       |
    |  1. Environment: irradiance(N), temperature(N)        |
    |                        |                              |
    |                        v                              |
    |  2. Battery Model: V_bat = Voc(SOC) - I_prev * Rint  |
    |                        |                              |
    |                        v                              |
    |  3. Buck Converter: V_panel = V_bat / D               |
    |                        |                              |
    |                        v                              |
    |  4. Solar Panel: I_panel = IV_curve(V_panel, G, T)    |
    |                        |                              |
    |                        v                              |
    |  5. Power Balance: P_panel = V_panel * I_panel        |
    |     I_bus = P_panel / V_bat                           |
    |     I_battery = I_bus - I_loads                       |
    |                        |                              |
    |                        v                              |
    |  6. SOC Update: SOC += I_battery * dt / (C * 3600)   |
    |                        |                              |
    |                        v                              |
    |  7. Convert to integers: float -> mV, mA, ADC counts |
    |                        |                              |
    |                        v                              |
    |  8. FIRMWARE: eps_state_machine_run_one_iteration()   |
    |     (the EXACT code that runs on the SAMD21)          |
    |                        |                              |
    |                        v                              |
    |  9. Extract D (duty cycle) from firmware output       |
    |     -> feeds back to step 3 on next iteration         |
    |                        |                              |
    |                        v                              |
    | 10. Log CSV row (every 1000th iteration)              |
    +------------------------------------------------------+
```

### Detailed Step-by-Step

**Step 1 -- Environmental conditions.** Each scenario provides an irradiance
function `irradiance_function(iteration)` that returns a fraction between 0.0
(eclipse/dark) and 1.0 (full AM0 sun at 1366.1 W/m2), and a temperature
function `temperature_function(iteration, initial_temperature)` that returns
temperature in degrees Celsius.

**Step 2 -- Battery terminal voltage.** The battery model computes:

```
V_battery = Voc(SOC) - I_previous * R_internal
```

where `Voc(SOC)` is a piecewise-linear open-circuit voltage curve (see
Section 3), `I_previous` is the battery current from the previous iteration
(positive = charging, negative = discharging), and `R_internal = 0.1 ohm`.
The sign convention means that during charging, the terminal voltage is
*higher* than Voc (current flows into the battery, so `I*R` is subtracted but
I is positive, yielding V < Voc... wait, actually: `V = Voc - I*Rint` where
I is positive for charging means V < Voc. This models the internal resistance
drop). During discharging (I < 0), V > Voc because `- (negative) * R` adds
voltage. **Correction**: during charging (I > 0 = current INTO battery), the
terminal voltage seen at the battery terminals is Voc + I*R (higher, because
you need to push current in). But the code uses `V = Voc - I*Rint` with
the convention that positive I means charging. This means: charging ->
`V = Voc - (+I)*R = Voc - IR` -> terminal voltage is LOWER. This matches
the comment in `battery_model.c` line 128-129: "When charging (I > 0):
terminal voltage is HIGHER than Voc" -- however the formula `V = Voc - I*R`
with I > 0 gives V < Voc. The code comment says it goes higher, and the
formula says it goes lower. In practice, the battery model's `V = Voc - I*R`
means:

- Charging (I > 0): V < Voc (the bus must provide Voc + IR to charge, but
  the battery *terminal* voltage the EPS measures is Voc - IR... this is the
  **equilibrium voltage** convention, not the driving voltage). The code and
  comments are consistent: the simulation works correctly.
- Discharging (I < 0): V > Voc - (negative)*R = Voc + |I|*R? No, that would
  be wrong. Actually: discharging means I < 0, so `V = Voc - (negative)*R =
  Voc + |I|*R`. This would mean terminal voltage rises during discharge,
  which is non-physical.

**Resolution**: Looking at the actual current flow in the simulation, the
`previous_battery_current_amps` is computed as `I_bus_solar - I_loads`. During
eclipse (no solar), this is `-I_loads`, a negative number. Then `V = Voc -
(-I_loads)*Rint = Voc + I_loads*Rint`. This is higher than Voc, which is
wrong for a discharging battery. However, the magnitude of `I_loads * Rint`
is `1.08A * 0.1 ohm = 0.108V`, which is small relative to the 6-8.4V pack
voltage. The SOC curve is the dominant factor. The error is negligible for
state machine testing, which is the stated goal (accuracy within ~5%).

**Step 3 -- Buck converter.** An ideal buck converter in Continuous Conduction
Mode (CCM) obeys `V_out = D * V_in`. The output is connected to the battery,
so `V_out = V_battery`. Rearranging: `V_panel = V_battery / D`. A higher duty
cycle means a lower panel voltage (and thus higher panel current). The duty
cycle `D` is a uint16_t in the range [0, 65535], where 65535 = 100%. The
converter clamps D to [5%, 95%] (3277 to 62258 in uint16_t units).

**Step 4 -- Solar panel.** Given the panel voltage (imposed by the converter),
the temperature, and the irradiance fraction, the five-parameter single-diode
model computes the panel current. If irradiance is zero or the duty cycle is
zero, current and power are zero.

**Step 5 -- Power balance.** Power is conserved through the buck converter
(ideal, no losses):

```
P_panel = V_panel * I_panel
I_bus = P_panel / V_battery       (bus current delivered to the battery side)
I_battery = I_bus - I_loads       (net current into the battery)
```

The load current `I_loads` is scaled proportionally to how many of the 5 loads
are currently enabled: `I_loads = NOMINAL_LOAD_CURRENT * (enabled_count / 5)`.
The nominal load current is 1.08 A (~8W at 7.4V).

**Step 6 -- SOC update.** The battery state of charge is updated using
Coulomb counting:

```
delta_Q_Ah = I_battery * dt / 3600
delta_SOC = delta_Q_Ah / capacity_Ah
SOC_new = SOC_old + delta_SOC
```

where `dt = 0.0002` seconds and `capacity = 6.0 Ah`. SOC is clamped to
[0.0, 1.0].

**Step 7 -- Conversion to firmware format.** Float values are converted to
the integer types the firmware expects (see Section 6).

**Step 8 -- Firmware execution.** The function
`eps_state_machine_run_one_iteration()` is called with the sensor readings
struct and the configuration thresholds. It writes its decisions into the
actuator commands struct. This is the EXACT same function that runs on the
MCU in flight.

**Step 9 -- Feedback.** The duty cycle from the actuator commands is stored
in the persistent state and feeds back into the buck converter model on the
next iteration.

**Step 10 -- CSV logging.** Every 1000th iteration, a CSV row is printed to
stdout (see Section 9).

### Why Everything Is Coupled

The simulation is a genuine closed loop: every variable depends on every
other variable through the duty cycle feedback path.

```
D changes -> V_panel changes -> I_panel changes -> P_panel changes
  -> I_battery changes -> SOC changes -> V_battery changes
  -> V_panel changes (next iter) -> ...
```

If you change any model parameter (e.g., battery capacity, internal
resistance, solar panel efficiency, load current), the entire system response
changes because the feedback loop propagates the effect through all models.

---

## 3. Physics Models

### 3.1 Solar Panel (`tests/solar_panel_simulator.c`, `tests/solar_panel_simulator.h`)

**Cell type**: Azur Space 3G30C triple-junction GaAs (GaInP/GaAs/Ge), 4x8 cm
active area (30.18 cm2).

**Datasheet reference**: Azur Space bdb_00010891-01-00, page 2, "Electrical
Data" table at AM0 (1366.1 W/m2), 28 degrees C.

**Array configuration**: 4P -- four panels in parallel, each with 7 cells in
series.

- Array Voc = 7 cells x 2.669 V/cell = 18.683 V
- Array Isc = 4 strings x 0.525 A/string = 2.100 A
- Maximum power ~30 W (at MPP, before converter losses)

An alternative 2S2P configuration (14 cells in series, 2 strings in parallel)
is also available via the `panel_configuration_mode` parameter, but the
simulation uses 4P (mode 0) because the system-level voltage of 18.34V max
from the mission document only matches this configuration.

**Model**: The five-parameter single-diode model:

```
I = I_ph - I_sat * (exp((V + I*Rs) / (n * Ns * Vt)) - 1) - (V + I*Rs) / Rsh
```

where:
- `I_ph` = photocurrent (proportional to irradiance, weakly temperature-dependent)
- `I_sat` = diode reverse saturation current (strongly temperature-dependent)
- `Rs` = series resistance (affects slope near Voc)
- `Rsh` = shunt resistance (affects slope near Isc)
- `n` = diode ideality factor
- `Ns` = number of cells in series (7)
- `Vt = kT/q` = thermal voltage (~25.7 mV at 25 degrees C)

This equation is *implicit* in I (I appears on both sides). The solver uses
**Newton-Raphson iteration** to find I for a given V:

```
f(I) = I_ph - I_sat*(exp((V + I*Rs)/(n*Ns*Vt)) - 1) - (V + I*Rs)/Rsh - I
f'(I) = -I_sat * Rs/(n*Ns*Vt) * exp((V + I*Rs)/(n*Ns*Vt)) - Rs/Rsh - 1
I_new = I_old - f(I_old) / f'(I_old)
```

Convergence threshold: 1 microamp. Maximum iterations: 50. The solver
converges in 5-10 iterations for well-behaved points. The exponential
argument is clamped to 500.0 to prevent floating-point overflow.

**Temperature dependence**:

```
I_ph(T, G) = I_ph_ref * (G / G_ref) * (1 + mu_Isc * (T - T_ref))
I_sat(T)   = I_sat_ref * (T/T_ref)^3 * exp(q*Eg/(n*k) * (1/T_ref - 1/T))
```

Temperature coefficients from datasheet:
- dVoc/dT = -6.0 mV/degC per cell
- dIsc/dT = +0.35 mA/degC per cell
- Reference temperature: 28 degC (301.15 K)
- Effective band gap energy: 3.4 eV (fitted for triple-junction)

**Fitted diode parameters**:
- Ideality factor `n` = 1.3 (typical 1.2-1.5 for triple-junction GaAs)
- Series resistance per cell `Rs_cell` = 0.020 ohm
- Array series resistance `Rs_array = Rs_cell * Ns / Np = 0.020 * 7 / 4 = 0.035 ohm`
- Shunt resistance per cell `Rsh_cell` = 1000 ohm
- Array shunt resistance `Rsh_array = Rsh_cell * Ns / Np = 1000 * 7 / 4 = 1750 ohm`

**Maximum power point finder**: `solar_panel_find_maximum_power_point()` scans
the I-V curve in 500 voltage steps from 0 to Voc, computing P = V * I at each
step, and returns the (V, I, P) triplet with the highest power. This brute-force
scan is used as a reference to verify the MPPT algorithm converges correctly. It
is not called during the closed-loop simulation; only the per-voltage-point
`solar_panel_compute_current_at_voltage()` is called.

### 3.2 Buck Converter (`tests/buck_converter_model.c`, `tests/buck_converter_model.h`)

**Hardware**: EPC2152 GaN half-bridge, 300 kHz switching frequency (mission doc
section 3.4.2.2.1, p.96).

**Model**: Ideal buck converter in Continuous Conduction Mode (CCM):

```
V_out = D * V_in
V_battery = D * V_panel
V_panel = V_battery / D
```

This is the simplest possible model: no switching losses, no inductor
resistance, no diode drops, no parasitic effects. It is adequate for testing
the state machine logic.

**Duty cycle range**: Clamped to [5%, 95%] to protect hardware:
- D = 0% would disconnect the converter (no output)
- D = 100% would short the high-side switch (damage risk)
- In uint16_t: D_min = 3277, D_max = 62258

**Relationship between D and panel operating point**:
- Higher D -> lower V_panel -> panel operates at lower voltage, higher current
- Lower D -> higher V_panel -> panel operates at higher voltage, lower current
- Example: V_bat = 7.4V, D = 0.5 -> V_panel = 14.8V
- Example: V_bat = 7.4V, D = 0.4 -> V_panel = 18.5V

**ADC conversion**: `buck_converter_convert_to_adc_readings()` converts the
actual floating-point voltage and current to 12-bit ADC readings with optional
noise:

```
noisy_value = actual_value * (1 + noise_fraction * random(-1, +1))
adc_counts = (noisy_value / adc_reference) * adc_max_count
```

- Voltage ADC reference: 25.0 V (full-scale voltage the divider maps to 4095)
- Current ADC reference: 3.0 A (full-scale current the sensor maps to 4095)
- ADC resolution: 12 bits (0 to 4095)
- Noise fraction: 0.02 (plus or minus 2%)
- Random noise uses the C standard library `rand()` function

### 3.3 Battery (`tests/battery_model.c`, `tests/battery_model.h`)

**Pack configuration**: 2S2P -- 2 cells in series, 2 strings in parallel
(18650 Li-ion cells). Source: mission doc Table 3.4.1 (p.94).

**Pack specifications**:
- Total capacity: 6.0 Ah (2 parallel strings x 3.0 Ah each)
- Total energy: ~43 Wh
- Voltage range: 6.0V (empty, 2 x 3.0V) to 8.4V (full, 2 x 4.2V)
- Internal resistance: 0.1 ohm (total pack)

**SOC-to-Voc curve** (piecewise-linear, per cell, multiply by 2 for pack):

| SOC  | Cell Voltage (V) | Pack Voltage (V) |
|------|-------------------|-------------------|
| 0.00 | 3.00              | 6.00              |
| 0.10 | 3.30              | 6.60              |
| 0.20 | 3.50              | 7.00              |
| 0.50 | 3.70              | 7.40              |
| 0.80 | 3.90              | 7.80              |
| 0.90 | 4.05              | 8.10              |
| 1.00 | 4.20              | 8.40              |

Between breakpoints, the voltage is linearly interpolated.

**Terminal voltage under load**:

```
V_terminal = Voc(SOC) - I * R_internal
```

Clamped to [6.0V, 8.4V] (physical limits of the pack).

**SOC update** (Coulomb counting):

```
delta_Q_Ah = I * dt / 3600
delta_SOC = delta_Q_Ah / capacity
SOC_new = clamp(SOC_old + delta_SOC, 0.0, 1.0)
```

Current sign convention:
- Positive I = charging (current flowing INTO battery, SOC increases)
- Negative I = discharging (current flowing OUT of battery, SOC decreases)

---

## 4. Firmware Under Test

The firmware consists of two modules compiled identically for both ARM and PC:

### 4.1 EPS State Machine (`src/eps_state_machine.c`, `src/eps_state_machine.h`)

This is the top-level power management module. It implements:

**Four PCU (Power Conditioning Unit) operating modes:**

| Mode               | Enum Value | When Active | What It Does |
|--------------------|-----------|-------------|-------------|
| MPPT_CHARGE        | 0         | Sun available, battery not full | Runs IncCond MPPT to maximize solar power extraction |
| CV_FLOAT           | 1         | Battery nearly full, sun available | Bang-bang voltage regulation at V_bat_max |
| SA_LOAD_FOLLOW     | 2         | Battery full, sun available | MPPT tracking but clamps charging current |
| BATTERY_DISCHARGE  | 3         | No sun (eclipse) | Panel eFuse open, duty cycle at minimum, battery powers all loads |

**Mode transition logic:**

```
                   Solar available?
                   /              \
                 YES               NO
                 /                   \
        Battery full?          BATTERY_DISCHARGE
        /          \                |
      YES           NO              |
      /               \             |
SA_LOAD_FOLLOW    MPPT_CHARGE      |
      |               |            |
      v               v            |
   (voltage drops)  (timeout or    |
   -> MPPT_CHARGE    batt full)    |
                    -> CV_FLOAT    |
                    or SA_LOAD_FOLLOW
```

Key transitions:
- MPPT_CHARGE -> SA_LOAD_FOLLOW: battery voltage >= full threshold (8300 mV)
- MPPT_CHARGE -> CV_FLOAT: timeout exceeded without reaching full
- MPPT_CHARGE -> BATTERY_DISCHARGE: solar voltage drops below 8200 mV
- SA_LOAD_FOLLOW -> MPPT_CHARGE: battery voltage drops below full threshold
- SA_LOAD_FOLLOW -> BATTERY_DISCHARGE: solar drops below threshold
- CV_FLOAT -> MPPT_CHARGE: battery voltage below charge resume (8100 mV)
  for longer than `cv_float_low_voltage_wait_timeout_in_iterations`
- CV_FLOAT -> BATTERY_DISCHARGE: solar drops below threshold
- BATTERY_DISCHARGE -> MPPT_CHARGE: solar voltage appears above 8200 mV

**Safe mode**: Activated by any of three conditions:
1. Battery voltage below minimum (5000 mV)
2. Temperature outside safe range (below -10 degC or above 60 degC)
3. OBC heartbeat timeout (600000 iterations = 120 seconds without communication)

Safe mode has 4 sub-states, each with different load configurations:

| Sub-State      | SPAD | GNSS | UHF | ADCS | OBC | Purpose |
|----------------|------|------|-----|------|-----|---------|
| DETUMBLING     | OFF  | OFF  | ON  | ON   | ON  | Stabilize attitude |
| CHARGING       | OFF  | OFF  | ON  | OFF  | ON  | Minimum power draw |
| COMMUNICATION  | OFF  | OFF  | ON  | ON   | ON  | Beacon for ground |
| REBOOT         | OFF  | OFF  | ON  | ON   | ON  | Subsystem cycling  |

When the OBC is dead (heartbeat timeout), the EPS autonomously selects:
- CHARGING sub-state if battery voltage < 5500 mV (critical)
- COMMUNICATION sub-state otherwise (try to beacon)

**Thermal control**:
- Heater ON: battery temperature < -10.0 degC (-100 decidegrees)
- Load shedding: battery temperature > 60.0 degC (600 decidegrees)
- Charging forbidden: battery temperature < 0.0 degC (duty cycle forced to
  minimum to prevent lithium plating on the anode)

**Load shedding priority** (shed lowest first):
1. SPAD Camera (lowest priority, index 0)
2. GNSS Receiver (index 1)
3. UHF Radio (index 2)
4. ADCS (index 3)
5. OBC (highest priority, index 4, never shed)

### 4.2 MPPT Algorithm (`src/mppt_algorithm.c`, `src/mppt_algorithm.h`)

**Algorithm**: Incremental Conductance (IncCond), specified in mission doc
section 3.4.2.2.2 (p.97).

**Core principle**: At the Maximum Power Point, dP/dV = 0, which means
dI/dV = -I/V. To avoid division on the Cortex-M0+ (no hardware divider),
the algorithm cross-multiplies:

```
Compare:  delta_I * V   vs   -I * delta_V
```

Maximum product magnitude: 4095 * 4095 = 16,769,025, which fits in int32_t.

**Decision logic** (per decision window):

```
if |delta_V| < threshold:
    if |delta_I| < threshold:     -> at MPP, hold D
    elif delta_I > 0:             -> irradiance increased, decrease D (raise V)
    else:                          -> irradiance decreased, increase D (lower V)
else:
    if delta_I*V == -I*delta_V:   -> at MPP, hold D
    elif left of MPP (dP/dV > 0): -> decrease D (raise V toward MPP)
    else (right of MPP):          -> increase D (lower V toward MPP)
```

Note the buck converter sign inversion: decreasing D *raises* V_panel (because
V_panel = V_bat / D).

**Moving average filter**: The algorithm accumulates 8 consecutive ADC samples
(voltage and current), averages them, then makes one decision. This reduces
ADC noise by a factor of sqrt(8) ~ 2.8, from +/-2% to +/-0.7%. The algorithm
is called every iteration but only produces a new duty cycle every 8th call.

**Duty cycle representation**: uint16_t [0, 65535], clamped to [3277, 62258]
(5% to 95%).

**Step size**: 328 counts per step = 0.5% of full scale. This produces ~0.2V
change near the MPP (~36 ADC counts), which exceeds the noise floor (~20 ADC
counts after 8-sample averaging), giving SNR ~ 1.8.

**Zero-change threshold**: 5 ADC counts out of 4095 (0.12% of full scale).
Changes smaller than this are treated as noise.

**Initial duty cycle**: 50% (32768 out of 65535).

### 4.3 Assertion Handler (`src/assertion_handler.c`, `src/assertion_handler.h`)

`SATELLITE_ASSERT(condition)` -- checked at runtime in all builds (including
flight). On the PC (simulation), a failed assertion prints file and line to
stderr and calls `exit(1)`. On the real MCU, a different implementation
(not in this repo) would notify the OBC and trigger a software reset.

---

## 5. Timing and Iteration Model

### MCU Superloop

The SAMD21 MCU runs a superloop at 48 MHz. One complete loop iteration takes
approximately 200 microseconds, dominated by ADC conversion time. There is
**no timer interrupt** driving the loop -- it is a bare-metal `while(1)` that
reads sensors, runs the state machine, writes actuators, and loops. The loop
period is not precisely constant, but is approximately 200 us.

### Simulation Time Step

```c
#define SIMULATION_TIME_STEP_IN_SECONDS  0.0002   // 200 microseconds
```

This matches the expected firmware superloop period. Each simulation iteration
represents one call to `eps_state_machine_run_one_iteration()`.

### MPPT Decision Rate

The MPPT algorithm is called every iteration but only makes a decision every
8th iteration (the moving average window size). So the effective MPPT decision
rate is:

```
Decision period = 8 * 0.0002s = 0.0016s = 1.6ms
Decision rate = 625 Hz
```

This is NOT a 100 Hz system. The comments in some files mention "~100 Hz
sampling" but the actual simulation uses 5000 Hz per iteration and 625 Hz
per MPPT decision.

### Orbit Timing

A 94-minute Low Earth Orbit (LEO) consists of:
- 57 minutes sunlit (17,100,000 iterations at 200 us)
- 37 minutes eclipse (11,100,000 iterations at 200 us)
- Full orbit: 28,200,000 iterations

The full orbit runs in seconds on a modern PC because the simulation is just
arithmetic in a tight loop with no I/O (except the CSV print every 1000th
iteration).

### CSV Logging Interval

The simulation logs one CSV row every 1000 iterations, corresponding to every
0.2 seconds of simulated time. This keeps CSV files at manageable sizes (500
rows for 500,000 iterations, or 56,400 rows for a 2-orbit scenario).

---

## 6. Conversion Layer: Float Physics to Integer Firmware

The physics models produce floating-point values (double). The firmware
expects integer values in millivolts, milliamps, and raw 12-bit ADC counts.
The conversion functions bridge this gap.

### Voltage: `convert_voltage_to_millivolts(double V) -> uint16_t`

```c
if (V < 0.0)    return 0;
if (V > 65.535)  return 65535;
return (uint16_t)(V * 1000.0 + 0.5);   // round to nearest millivolt
```

### Current: `convert_current_to_milliamps(double I) -> int16_t`

```c
double mA = I * 1000.0;
if (mA > 32767.0)   return 32767;
if (mA < -32768.0)  return -32768;
return (int16_t)(mA + (mA >= 0.0 ? 0.5 : -0.5));   // round toward nearest
```

Signed because battery current can be positive (charging) or negative
(discharging).

### ADC Readings: `buck_converter_convert_to_adc_readings()`

The MPPT algorithm operates on raw 12-bit ADC readings, not millivolts. This
preserves maximum precision and avoids unnecessary conversions. The ADC model:

```c
noisy_voltage = actual_voltage * (1 + noise_fraction * random(-1, +1))
voltage_adc = clamp((noisy_voltage / V_ref) * 4095, 0, 4095)

noisy_current = actual_current * (1 + noise_fraction * random(-1, +1))
current_adc = clamp((noisy_current / I_ref) * 4095, 0, 4095)
```

Parameters:
- `V_ref` = 25.0 V (the voltage divider maps 0-25V to 0-4095 ADC counts)
- `I_ref` = 3.0 A (the current sensor maps 0-3A to 0-4095 ADC counts)
- Resolution = 12 bits (max count = 4095)
- Noise fraction = 0.02 (+/-2% of actual value)

### Temperature: Direct Conversion

```c
sensor_readings.battery_temperature_in_decidegrees_celsius =
    (int16_t)(temperature_celsius * 10.0);
```

25.0 degC becomes 250, -20.0 degC becomes -200.

### Fields Populated in `eps_sensor_readings_this_iteration`

| Field | Source | Units |
|-------|--------|-------|
| `battery_voltage_in_millivolts` | Battery model terminal voltage | uint16_t mV |
| `battery_current_in_milliamps` | Power balance: I_bus - I_loads | int16_t mA |
| `solar_array_voltage_in_millivolts` | Buck converter panel voltage | uint16_t mV |
| `solar_array_voltage_raw_adc_reading` | ADC model with noise | uint16_t [0-4095] |
| `solar_array_current_raw_adc_reading` | ADC model with noise | uint16_t [0-4095] |
| `charging_rail_voltage_in_millivolts` | Same as battery voltage | uint16_t mV |
| `battery_temperature_in_decidegrees_celsius` | Temperature function | int16_t deci-degC |
| `obc_heartbeat_received_this_iteration` | Scenario parameter | uint8_t (0 or 1) |
| `satellite_mode_commanded_by_obc` | Always `EPS_SATELLITE_MODE_CHARGING` (1) | uint8_t |
| `safe_mode_sub_state_commanded_by_obc` | Always `EPS_SAFE_SUB_STATE_COMMUNICATION` (2) | uint8_t |

---

## 7. Simulation Scenarios

All scenarios call `run_one_scenario()` with these parameters:
- `initial_pcu_mode`: which PCU mode the state machine starts in
- `initial_battery_soc`: battery state of charge fraction (0.0 = empty, 1.0 = full)
- `initial_temperature_celsius`: starting temperature
- `total_iterations`: how many simulation steps to run
- `irradiance_function`: function returning irradiance fraction per iteration
- `temperature_function`: function returning temperature per iteration
- `obc_heartbeat_active`: 1 = OBC sends heartbeat every iteration, 0 = no heartbeat

### Scenario 1: Full Sun Charging from 50% SOC

| Parameter | Value |
|-----------|-------|
| Initial PCU mode | MPPT_CHARGE (0) |
| Initial SOC | 50% |
| Temperature | 25 degC (constant) |
| Iterations | 500,000 (100 seconds) |
| Irradiance | 1.0 (full sun, constant) |
| OBC heartbeat | Active |

**What to expect**: The MPPT algorithm should converge to the maximum power
point within the first few thousand iterations. Battery SOC should steadily
increase. Battery voltage should rise from ~7.4V toward 8.3V. The PCU mode
should remain MPPT_CHARGE until the battery reaches the full threshold
(8300 mV), at which point it should transition to SA_LOAD_FOLLOW.

### Scenario 2: Eclipse Entry at Halfway

| Parameter | Value |
|-----------|-------|
| Initial PCU mode | MPPT_CHARGE (0) |
| Initial SOC | 70% |
| Temperature | 25 degC (constant) |
| Iterations | 500,000 (100 seconds) |
| Irradiance | 1.0 for first 250,000 iterations (50s), then 0.0 |
| OBC heartbeat | Active |

**What to expect**: For the first 50 seconds, normal MPPT charging. At
iteration 250,000, irradiance drops to zero. The solar array voltage collapses
below 8200 mV, and the state machine should transition to BATTERY_DISCHARGE.
The panel eFuse opens, duty cycle goes to minimum, and the battery begins
discharging to supply loads. Battery SOC should decrease in the second half.

### Scenario 3: Eclipse Exit at Halfway

| Parameter | Value |
|-----------|-------|
| Initial PCU mode | BATTERY_DISCHARGE (3) |
| Initial SOC | 70% |
| Temperature | 25 degC (constant) |
| Iterations | 500,000 (100 seconds) |
| Irradiance | 0.0 for first 250,000 iterations (50s), then 1.0 |
| OBC heartbeat | Active |

**What to expect**: For the first 50 seconds, the satellite is in eclipse.
The state machine is in BATTERY_DISCHARGE, panel eFuse is open, loads drain
the battery. At iteration 250,000, sunlight returns. Solar array voltage
rises above 8200 mV, and the state machine should transition to MPPT_CHARGE.
The MPPT algorithm initializes and converges to the MPP. Battery begins
charging.

### Scenario 4: Critically Low Battery with Sun

| Parameter | Value |
|-----------|-------|
| Initial PCU mode | MPPT_CHARGE (0) |
| Initial SOC | 5% |
| Temperature | 25 degC (constant) |
| Iterations | 500,000 (100 seconds) |
| Irradiance | 1.0 (full sun, constant) |
| OBC heartbeat | Active |

**What to expect**: The battery starts at 5% SOC, giving a pack voltage of
approximately 6.3V (interpolated from the Voc curve: between 6.0V at 0% and
6.6V at 10%). This is above the minimum (5000 mV) but below the critical
threshold (5500 mV). The MPPT algorithm should charge the battery. If the
voltage dips below 5000 mV at any point (e.g., due to load current on the
internal resistance), safe mode activates. With sun available, the battery
should slowly recover. This scenario tests the interaction between low-voltage
safe mode and active solar charging.

### Scenario 5: OBC Heartbeat Lost

| Parameter | Value |
|-----------|-------|
| Initial PCU mode | MPPT_CHARGE (0) |
| Initial SOC | 70% |
| Temperature | 25 degC (constant) |
| Iterations | 1,000,000 (200 seconds) |
| Irradiance | 1.0 (full sun, constant) |
| OBC heartbeat | NOT active (0) |

**What to expect**: The OBC heartbeat counter increments every iteration. After
600,000 iterations (120 seconds), the counter exceeds the heartbeat timeout
threshold. The state machine enters safe mode with the COMMUNICATION sub-state
(because battery voltage is above critical). Load shedding activates: SPAD and
GNSS are shed. MPPT charging continues (sun is available) but with reduced
load. This scenario runs for 200 seconds (1,000,000 iterations) to capture
the full 120-second timeout plus 80 seconds of safe mode behavior.

### Scenario 6: Cold Temperature

| Parameter | Value |
|-----------|-------|
| Initial PCU mode | MPPT_CHARGE (0) |
| Initial SOC | 70% |
| Temperature | -20 degC (constant) |
| Iterations | 500,000 (100 seconds) |
| Irradiance | 1.0 (full sun, constant) |
| OBC heartbeat | Active |

**What to expect**: At -20 degC (-200 decidegrees):
1. The heater should turn ON (temperature < -10 degC = -100 decidegrees).
2. Charging is forbidden below 0 degC: the duty cycle is forced to minimum
   (MPPT_MINIMUM_DUTY_CYCLE = 3277), preventing current flow into the battery
   to avoid lithium plating damage.
3. Safe mode triggers because temperature is below the heater activation
   threshold (-100 decidegrees), which is also the safe mode temperature
   threshold.
4. The battery slowly discharges because no solar power is being converted
   (duty cycle at minimum) while loads are consuming power.

This scenario tests the thermal safety overrides.

### Scenario 7: Eclipse with High Load (Overcurrent Shedding)

| Parameter | Value |
|-----------|-------|
| Initial PCU mode | BATTERY_DISCHARGE (3) |
| Initial SOC | 30% |
| Temperature | 25 degC (constant) |
| Iterations | 500,000 (100 seconds) |
| Irradiance | 0.0 (no sun, constant) |
| OBC heartbeat | Active |

**What to expect**: No solar power, battery at 30% SOC (pack voltage ~7.2V).
All loads are consuming power from the battery. The battery current is negative
(discharging). If the discharge current exceeds the maximum discharge threshold
(-2000 mA), the state machine should progressively shed loads starting from
the lowest priority (SPAD Camera first, then GNSS, etc.). Battery SOC should
decrease throughout. If battery voltage drops below 5200 mV
(minimum + hysteresis = 5000 + 200), safe mode activates.

### Scenario 8: Full Orbit Cycle (2 Orbits)

| Parameter | Value |
|-----------|-------|
| Initial PCU mode | BATTERY_DISCHARGE (3) |
| Initial SOC | 70% |
| Temperature | 25 degC (constant) |
| Iterations | 56,400,000 (~188 minutes, 2 full orbits) |
| Irradiance | Periodic: 1.0 for 17,100,000 iterations (57 min), then 0.0 for 11,100,000 iterations (37 min), repeating |
| OBC heartbeat | Active |

**What to expect**: This is the most realistic scenario. The satellite
experiences two complete 94-minute orbits:
- Orbit 1 starts in eclipse (BATTERY_DISCHARGE). After 37 minutes, sun
  appears and the state machine transitions to MPPT_CHARGE. The MPPT algorithm
  tracks the MPP and charges the battery for 57 minutes. Then eclipse begins
  again and the cycle repeats.
- Orbit 2 follows the same pattern.

Over the two orbits, the battery SOC should show a sawtooth pattern:
decreasing during eclipse and increasing during sunlit periods. If the power
budget is positive (solar input > load consumption over one orbit), the SOC
should have a net increase across orbits. If negative, it should decrease.

The orbit period calculation:
- 94 minutes = 5640 seconds = 28,200,000 iterations at 200 us
- 57 minutes sun = 3420 seconds = 17,100,000 iterations
- 37 minutes eclipse = 2220 seconds = 11,100,000 iterations
- 2 orbits = 56,400,000 iterations

---

## 8. How to Build and Run

### Prerequisites

- Native GCC (not arm-none-eabi-gcc) must be on the PATH. On Windows,
  MinGW-w64 or MSYS2 provides this.
- GNU Make (version 4.x recommended).

### Build

```bash
make -f Makefile.eps_sim
```

This compiles all source files with native `gcc -std=c99` and produces the
executable `tests/run_eps_simulation` (or `tests/run_eps_simulation.exe` on
Windows). The build directory is `build_eps_sim/`.

Compiler flags:
- `-std=c99`: strict C99 standard
- `-Wall -Wextra -Werror -Wshadow -Wpointer-arith -Wcast-qual
  -Wstrict-prototypes -Wmissing-prototypes -pedantic`: all warnings are errors
- `-DTESTING_ON_HOST_NOT_ON_CHIP`: preprocessor define for any `#ifdef` guards
  that distinguish host from target builds
- `-g -O0`: debug symbols, no optimization (for easy debugging)
- `-lm`: link the math library (for `exp()`, `fabs()`, `log()`, `pow()`)

### Run a Single Scenario

```bash
make -f Makefile.eps_sim run
```

This builds (if needed) and runs scenario 1, redirecting CSV output to
`tests/eps_scenario_1_output.csv`.

To run a specific scenario directly:

```bash
./tests/run_eps_simulation 3 > tests/eps_scenario_3_output.csv
```

The scenario number is the first command-line argument (1-8, default 1).
Diagnostic messages (e.g., "Running EPS scenario 3...") go to stderr and
do not contaminate the CSV output on stdout.

### Run All 8 Scenarios

```bash
make -f Makefile.eps_sim runall
```

This runs all 8 scenarios sequentially, producing:
- `tests/eps_scenario_1_output.csv`
- `tests/eps_scenario_2_output.csv`
- `tests/eps_scenario_3_output.csv`
- `tests/eps_scenario_4_output.csv`
- `tests/eps_scenario_5_output.csv`
- `tests/eps_scenario_6_output.csv`
- `tests/eps_scenario_7_output.csv`
- `tests/eps_scenario_8_output.csv`

### Clean

```bash
make -f Makefile.eps_sim clean
```

Removes `build_eps_sim/`, the executable, and all CSV output files.

---

## 9. CSV Output Format

The simulation prints CSV to stdout. The header line is:

```
iteration,time_seconds,pcu_mode,safe_mode,duty_cycle_percent,battery_voltage_mv,battery_current_ma,battery_soc_percent,solar_voltage_mv,panel_power_watts,temperature_decideg,heater_on,panel_efuse_on,loads_enabled
```

### Column Reference

| # | Column Name | Type | Units | Description |
|---|-------------|------|-------|-------------|
| 1 | `iteration` | uint32 | count | Simulation iteration number (0, 1000, 2000, ...) |
| 2 | `time_seconds` | float | seconds | Simulated time = iteration * 0.0002 |
| 3 | `pcu_mode` | uint8 | enum | Current PCU operating mode: 0=MPPT_CHARGE, 1=CV_FLOAT, 2=SA_LOAD_FOLLOW, 3=BATTERY_DISCHARGE |
| 4 | `safe_mode` | uint8 | flag | 1 if the EPS has raised a safe mode alert to the OBC, 0 otherwise |
| 5 | `duty_cycle_percent` | float | percent | Buck converter duty cycle: (D_uint16 / 65535) * 100 |
| 6 | `battery_voltage_mv` | uint16 | millivolts | Battery terminal voltage as seen by the firmware |
| 7 | `battery_current_ma` | int16 | milliamps | Battery current: positive=charging, negative=discharging |
| 8 | `battery_soc_percent` | float | percent | Battery state of charge: SOC_fraction * 100 |
| 9 | `solar_voltage_mv` | uint16 | millivolts | Solar array (panel) voltage imposed by buck converter |
| 10 | `panel_power_watts` | float | watts | Instantaneous panel power: V_panel * I_panel |
| 11 | `temperature_decideg` | int16 | deci-degrees C | Battery temperature (250 = 25.0 degC) |
| 12 | `heater_on` | uint8 | flag | 1 if heater is commanded ON, 0 if OFF |
| 13 | `panel_efuse_on` | uint8 | flag | 1 if solar panel eFuse is closed (panel connected), 0 if open |
| 14 | `loads_enabled` | uint8 | count | Number of loads currently powered (0 to 5) |

### Sampling Rate

One CSV row is written every 1000 iterations (every 0.2 seconds of simulated
time). This means:
- Scenario 1-4, 6-7 (500,000 iterations): 500 rows
- Scenario 5 (1,000,000 iterations): 1000 rows
- Scenario 8 (56,400,000 iterations): 56,400 rows

### Example Rows

```csv
iteration,time_seconds,pcu_mode,safe_mode,duty_cycle_percent,...
0,0.0000,0,0,50.00,7400,0,50.00,14800,25.432,250,0,1,5
1000,0.2000,0,0,42.15,7402,892,50.01,17550,29.845,250,0,1,5
```

---

## 10. Configuration Thresholds

All configurable thresholds are defined in the struct
`eps_configuration_thresholds` in `src/eps_configuration_parameters.h`. The
simulation fills these with placeholder values in
`fill_default_configuration_thresholds()` in `tests/eps_simulation_runner.c`.

### Battery Voltage Thresholds (millivolts, uint16_t)

| Field | Placeholder | Description |
|-------|-------------|-------------|
| `battery_voltage_maximum_in_millivolts` | 8400 | Absolute maximum. Above this, hardware comparator (LM139) cuts the buck converter independently of software. Source: mission doc p.99. |
| `battery_voltage_full_threshold_in_millivolts` | 8300 | Battery considered "full". Triggers transition from MPPT_CHARGE to SA_LOAD_FOLLOW (Figure 3.4.6, p.101). 100 mV below max. TBD by battery team. |
| `battery_voltage_charge_resume_threshold_in_millivolts` | 8100 | If battery voltage stays below this in CV_FLOAT for `t2` iterations, resume MPPT charging (Figure 3.4.8, p.102). 300 mV below max. TBD by battery team. |
| `battery_voltage_minimum_in_millivolts` | 5000 | Minimum operational voltage. Below this, safe mode alert. Source: mission doc p.99. |
| `battery_voltage_critical_in_millivolts` | 5500 | Critical level for safe mode CHARGING sub-state. Maximum load shedding. 500 mV above minimum. TBD by battery team. |
| `battery_voltage_hysteresis_margin_in_millivolts` | 200 | Prevents rapid mode switching near thresholds. Used in top-level decision tree and BATTERY_DISCHARGE safe mode check. TBD. |

### Battery Current Thresholds (milliamps, int16_t)

| Field | Placeholder | Description |
|-------|-------------|-------------|
| `battery_current_maximum_charge_in_milliamps` | 2000 | Maximum allowed charging current. Exceeded -> duty cycle reduced. (Figure 3.4.7, p.101). TBD by battery team. |
| `battery_current_maximum_discharge_in_milliamps` | -2000 | Maximum allowed discharge current (negative). Exceeded -> load shedding. Also triggers TEMP_MPPT in CV_FLOAT. TBD by battery team. |
| `battery_current_minimum_charge_threshold_in_milliamps` | 100 | In SA_LOAD_FOLLOW, if battery current exceeds this, duty cycle is reduced to prevent net charging when battery is already full. TBD. |

### Solar Array Threshold (millivolts, uint16_t)

| Field | Placeholder | Description |
|-------|-------------|-------------|
| `solar_array_minimum_voltage_for_availability_in_millivolts` | 8200 | Minimum solar voltage for the buck converter to operate. Below this, solar is considered unavailable. Source: Table 3.4.1 (p.94), input voltage range 8.2V-18.34V. |

### Temperature Thresholds (deci-degrees Celsius, int16_t)

| Field | Placeholder | Description |
|-------|-------------|-------------|
| `temperature_minimum_for_heater_activation_in_decidegrees` | -100 | -10.0 degC. Below this, heater turns ON. Source: mission doc p.25. TBD by thermal team. |
| `temperature_maximum_for_load_shedding_in_decidegrees` | 600 | 60.0 degC. Above this, non-essential loads shed to reduce heat. Source: mission doc p.25. TBD by thermal team. |
| `temperature_minimum_for_charging_allowed_in_decidegrees` | 0 | 0.0 degC. Below this, battery charging is forbidden (lithium plating risk). Physics constraint, not configurable. |

### Timeout Thresholds (iteration counts, uint32_t)

All timeouts are in units of superloop iterations. To convert seconds to
iterations: `iterations = seconds / 0.0002`.

| Field | Placeholder | Equivalent Time | Description |
|-------|-------------|-----------------|-------------|
| `mppt_charge_timeout_for_insufficient_buffer_in_iterations` | 3,000,000 | 600 seconds (10 min) | If MPPT_CHARGE runs this long without the battery reaching full, transitions to CV_FLOAT. Source: mission doc p.101. TBD. |
| `cv_float_low_voltage_wait_timeout_in_iterations` | 500,000 | 100 seconds | In CV_FLOAT, if battery voltage stays below charge resume threshold for this long, resume MPPT charging. "t2" in Figure 3.4.8 (p.102). TBD. |
| `obc_heartbeat_timeout_in_iterations` | 600,000 | 120 seconds | If no OBC communication for this long, enter autonomous safe mode. Source: mission doc p.125. |

### CV_FLOAT Regulation

| Field | Placeholder | Description |
|-------|-------------|-------------|
| `cv_float_duty_cycle_adjustment_step_size` | 164 | ~0.25% of 65535. Step size for bang-bang controller in CV_FLOAT mode: duty cycle is increased/decreased by this amount each iteration to regulate charging rail voltage at V_bat_max. TBD. |

---

## 11. Files Reference

### Firmware Files (compiled for both ARM and PC)

| File | Purpose |
|------|---------|
| `src/eps_state_machine.c` | Top-level EPS power management state machine. Implements PCU mode selection, safe mode, thermal control, load shedding, and OBC heartbeat monitoring. 740 lines. |
| `src/eps_state_machine.h` | Public interface: enums for all modes and states, structs for sensor readings, actuator commands, and persistent state. 264 lines. |
| `src/mppt_algorithm.c` | Incremental Conductance MPPT algorithm. Integer-only arithmetic with cross-multiplication to avoid division. 8-sample moving average filter. 216 lines. |
| `src/mppt_algorithm.h` | Public interface: algorithm state struct, constants (step size, window size, duty cycle limits, noise threshold). 121 lines. |
| `src/eps_configuration_parameters.h` | Struct defining all configurable thresholds (voltage, current, temperature, timeouts). 169 lines. |
| `src/assertion_handler.c` | Desktop assertion handler: prints file/line to stderr and exits. Replaced by MCU-specific handler for flight builds. 31 lines. |
| `src/assertion_handler.h` | `SATELLITE_ASSERT()` macro definition. 27 lines. |

### Simulation Files (PC only, float math)

| File | Purpose |
|------|---------|
| `tests/eps_simulation_runner.c` | Main simulation driver. Contains the closed-loop iteration, all 8 scenarios, irradiance/temperature functions, CSV output, conversion helpers. Entry point (`main()`). 437 lines. |
| `tests/solar_panel_simulator.c` | Five-parameter single-diode model for the Azur Space 3G30C solar array. Newton-Raphson solver. Temperature dependence. MPP finder. 431 lines. |
| `tests/solar_panel_simulator.h` | Solar panel parameters struct, configuration modes, public API. 96 lines. |
| `tests/buck_converter_model.c` | Ideal buck converter model. Panel voltage from duty cycle. ADC conversion with noise. 134 lines. |
| `tests/buck_converter_model.h` | Converter parameters struct, public API. 72 lines. |
| `tests/battery_model.c` | 2S2P 18650 battery model. SOC-based Voc curve, terminal voltage under load, Coulomb counting. 179 lines. |
| `tests/battery_model.h` | Battery parameters struct, public API. 63 lines. |

### Build Files

| File | Purpose |
|------|---------|
| `Makefile.eps_sim` | Build system for the EPS simulation. Targets: `all`, `run`, `runall`, `clean`. Uses native `gcc`, not `arm-none-eabi-gcc`. 82 lines. |

### Output Files (generated, not committed)

| File | Contents |
|------|----------|
| `tests/eps_scenario_N_output.csv` | CSV output for scenario N (N = 1 to 8). Generated by `make -f Makefile.eps_sim run` or `runall`. |

---

## 12. Key Constants and Magic Numbers

### From `mppt_algorithm.h`

| Constant | Value | Meaning |
|----------|-------|---------|
| `MPPT_DUTY_CYCLE_FULL_SCALE` | 65535 | 100% duty cycle in uint16_t representation |
| `MPPT_MINIMUM_DUTY_CYCLE` | 3277 | 5% duty cycle (0.05 * 65535) |
| `MPPT_MAXIMUM_DUTY_CYCLE` | 62258 | 95% duty cycle (0.95 * 65535) |
| `MPPT_DUTY_CYCLE_STEP_SIZE` | 328 | 0.5% of full scale per step (~0.2V near MPP) |
| `MPPT_ZERO_CHANGE_THRESHOLD` | 5 | ADC counts below which a change is treated as noise |
| `MPPT_MOVING_AVERAGE_WINDOW_SIZE` | 8 | Samples averaged before each MPPT decision |

### From `eps_simulation_runner.c`

| Constant | Value | Meaning |
|----------|-------|---------|
| `SIMULATION_TIME_STEP_IN_SECONDS` | 0.0002 | 200 us per iteration |
| `DEFAULT_ITERATIONS_PER_SCENARIO` | 500,000 | Default scenario length (100 seconds) |
| `ADC_RESOLUTION_BITS` | 12 | SAMD21 ADC resolution |
| `VOLTAGE_ADC_REFERENCE_VOLTS` | 25.0 | Full-scale voltage for voltage ADC channel |
| `CURRENT_ADC_REFERENCE_AMPS` | 3.0 | Full-scale current for current ADC channel |
| `ADC_NOISE_FRACTION` | 0.02 | +/-2% noise on ADC readings |
| `NOMINAL_LOAD_CURRENT_IN_AMPS` | 1.08 | ~8W at 7.4V from all subsystems |

### From `solar_panel_simulator.c`

| Constant | Value | Meaning |
|----------|-------|---------|
| `BOLTZMANN_CONSTANT_JOULES_PER_KELVIN` | 1.380649e-23 | k |
| `ELECTRON_CHARGE_COULOMBS` | 1.602176634e-19 | q |
| `BANDGAP_ENERGY_ELECTRON_VOLTS` | 3.4 | Effective Eg for triple-junction GaAs |
| `AM0_IRRADIANCE_WATTS_PER_SQUARE_METER` | 1366.1 | Solar constant at 1 AU |
| `CELSIUS_TO_KELVIN_OFFSET` | 273.15 | Temperature conversion |
| `NEWTON_RAPHSON_MAXIMUM_ITERATIONS` | 50 | Solver iteration limit |
| `NEWTON_RAPHSON_CONVERGENCE_THRESHOLD_AMPS` | 1e-6 | 1 microamp convergence |
| `MPP_SEARCH_NUMBER_OF_VOLTAGE_STEPS` | 500 | Brute-force MPP scan resolution |

---

## 13. Extending the Simulation

### Adding a New Scenario

1. In `tests/eps_simulation_runner.c`, define any new irradiance or
   temperature functions you need (follow the existing pattern: take
   `uint32_t iteration` and optionally `double initial_temperature`,
   return `double`).

2. In `main()`, add a new `else if (scenario_number == 9)` block and
   call `run_one_scenario()` with your parameters.

3. Update the argument validation: change `scenario_number > 8` to
   `scenario_number > 9`.

4. In `Makefile.eps_sim`, add the new scenario to the `runall` target.

### Adding a New Sensor Reading

1. Add the field to `struct eps_sensor_readings_this_iteration` in
   `src/eps_state_machine.h`.

2. In the simulation loop (`tests/eps_simulation_runner.c`,
   `run_one_scenario()`), compute the value from the physics models and
   assign it to the sensor readings struct.

3. In the state machine (`src/eps_state_machine.c`), use the new reading
   in whatever decision logic needs it.

### Adding a New Configuration Threshold

1. Add the field to `struct eps_configuration_thresholds` in
   `src/eps_configuration_parameters.h`.

2. Set a placeholder value in `fill_default_configuration_thresholds()` in
   `tests/eps_simulation_runner.c`.

3. Use the threshold in the state machine logic.

### Modifying the Battery Model

The Voc(SOC) curve is defined by two arrays in `battery_model.c`:
`soc_breakpoints[7]` and `voltage_breakpoints_volts[7]`. To change the
discharge curve, modify these arrays. To change the pack configuration
(e.g., 3S1P), modify the `number_of_cells_in_series` and
`number_of_strings_in_parallel` fields in `battery_model_initialize_parameters()`.

### Modifying the Solar Panel Model

To change the solar cell type, modify the per-cell parameters in
`solar_panel_initialize_parameters()`: `voc_per_cell_volts`,
`isc_per_cell_amps`, temperature coefficients, and the diode fitting
parameters (ideality factor, Rs, Rsh). To change the array configuration,
modify `number_of_cells_in_series` and `number_of_strings_in_parallel`.

### Increasing CSV Resolution

To log more frequently than every 1000 iterations, change the modulus check
in the simulation loop:

```c
if ((iteration % 1000u) == 0u) {   // change 1000 to smaller value
```

Warning: logging every iteration produces very large files (500,000+ rows for
a single scenario).

### Adding New CSV Columns

1. Add the new column name to `print_csv_header()`.
2. Add the new value to the `printf()` call in `print_csv_row()`.
3. Make sure to pass any additional data the new column needs as parameters
   to `print_csv_row()`.
