# EPS State Machine — Complete Technical Reference

This document explains the complete EPS (Electrical Power Subsystem) state
machine for the CHESS Pathfinder 0 CubeSat. It is written so that someone
with no background in satellite power systems, embedded firmware, or the
CHESS mission can read it from top to bottom and understand every design
decision, every state transition, and every threshold value.

If you are a future developer (human or AI) tasked with modifying this code,
this document is your map. Every claim is sourced from the CHESS Satellite
Mission Document (referred to throughout as "mission doc") with page numbers.
Every engineering inference is labeled as such.

---

## Table of Contents

1. [What is the EPS and why does it need a state machine?](#1-what-is-the-eps-and-why-does-it-need-a-state-machine)
2. [Two levels of decision-making](#2-two-levels-of-decision-making)
3. [PCU state machine detail](#3-pcu-state-machine-detail)
4. [Safe mode](#4-safe-mode)
5. [Thermal control](#5-thermal-control)
6. [Load shedding](#6-load-shedding)
7. [Hardware protection](#7-hardware-protection)
8. [Threshold values table](#8-threshold-values-table)
9. [Code design overview](#9-code-design-overview)

---

## 1. What is the EPS and why does it need a state machine?

### What the EPS does

The Electrical Power Subsystem (EPS) manages all electrical power on the
CHESS Pathfinder 0 CubeSat. It sits between the solar panels and every
other subsystem on the satellite. Its job:

- Harvest energy from solar panels via a buck converter
- Charge the battery safely
- Distribute power to subsystems (OBC, ADCS, UHF radio, GNSS, SPAD camera)
- Protect the battery from overcharge, overdischarge, and thermal damage
- Shed non-essential loads when power is insufficient

The EPS has its own dedicated microcontroller: a Microchip SAMD21G17D,
which is an ARM Cortex-M0+ processor. This is a separate chip from the
OBC (On-Board Computer). The EPS MCU runs independently. If the OBC
crashes, the EPS continues operating and protecting the battery.

Source: mission doc Section 3.4 (p.93-103) for EPS architecture.

### Why the MCU matters for code design

The SAMD21G17D has specific hardware constraints that dictate every code
decision:

| Resource    | Value   | Implication                                     |
|-------------|---------|--------------------------------------------------|
| CPU core    | Cortex-M0+ | No hardware floating-point unit (FPU)         |
| Divider     | None    | No hardware integer divider (software emulation) |
| RAM         | 16 KB   | All state must fit in a few hundred bytes         |
| Flash       | 128 KB  | Total code + constants budget for the firmware    |
| Clock       | 48 MHz  | After DFLL48M configuration                      |

Because there is no FPU and no hardware divider, the firmware uses
**integer-only arithmetic**. All voltages are in millivolts (uint16_t),
all currents in milliamps (int16_t), all temperatures in deci-degrees
Celsius (int16_t, so -10.0 C = -100). No floats anywhere in the
firmware. The simulation code (Python, runs on a laptop) can use floats
freely because it never runs on the MCU.

Source: SAMD21G17D datasheet; mission doc p.93 for MCU selection.

### Why a state machine?

The EPS does not do the same thing all the time. Its behavior depends on
conditions that change continuously:

- Is the satellite in sunlight or eclipse?
- Is the battery full, partially charged, or critically low?
- Is the battery temperature safe for charging?
- Is the OBC still alive and communicating?
- Are any subsystems drawing too much current?

A state machine is the standard way to organize this kind of
condition-dependent behavior. Each **state** represents a distinct
operating mode with its own control logic. **Transitions** between states
are triggered by measured conditions crossing defined thresholds.

The alternative — a giant if/else tree re-evaluated every iteration —
would be fragile, hard to test, and difficult to reason about. A state
machine makes each mode's logic self-contained, its transitions explicit,
and its behavior verifiable.

### Why pure C and integer-only

Three reasons:

1. **No FPU.** The Cortex-M0+ has no hardware floating-point. Float
   operations are emulated in software at ~10-20 cycles each. For a
   power control loop this would work but wastes cycles and flash space.
   Integer arithmetic is direct and predictable.

2. **Determinism.** Integer arithmetic on a Cortex-M0+ produces the exact
   same result every time for the same inputs. Float emulation can have
   subtle rounding differences depending on compiler version and
   optimization level. For safety-critical power management, determinism
   matters.

3. **Testability.** The state machine is Category 1 code (pure logic,
   zero hardware dependencies). It takes integer sensor readings in, and
   produces integer actuator commands out. It can be compiled with native
   GCC on a laptop and tested without any hardware. The simulation feeds
   it the same integer types the real ADC would produce.

---

## 2. Two levels of decision-making

The CHESS satellite has two separate levels of power management decisions.
Understanding this split is essential before reading the state machine code.

### Level 1: Satellite operating modes (decided by the OBC)

The OBC (On-Board Computer) decides what the satellite is doing at a
high level. There are four satellite operating modes:

| Mode                  | What the satellite is doing                        |
|-----------------------|-----------------------------------------------------|
| MEASUREMENT           | Taking science data (SPAD camera, GNSS)             |
| CHARGING              | Prioritizing battery charging over science           |
| UHF_COMMUNICATION     | Active communication pass with ground station        |
| SAFE                  | Something went wrong, survival mode                  |

Source: mission doc p.20-25, Section 1.7.

The OBC chooses these modes based on orbit position, ground commands,
battery state of charge, and mission planning. The EPS does not decide
satellite modes. The EPS is told which mode the OBC has chosen and adjusts
its load management accordingly.

### Level 2: PCU operating modes (decided by the EPS autonomously)

Within whatever satellite mode the OBC has chosen, the EPS independently
manages the Power Conditioning Unit (PCU) — the buck converter that sits
between the solar panels and the battery. The EPS chooses one of four PCU
modes based on two physical questions:

1. Is solar power available? (solar array voltage >= 8200 mV)
2. What is the battery state? (voltage relative to thresholds)

The four PCU modes:

| PCU Mode           | Sun? | Battery state           | What the converter does          |
|--------------------|------|-------------------------|-----------------------------------|
| MPPT_CHARGE        | Yes  | Not full                | Track maximum power point, charge |
| CV_FLOAT           | Yes  | Nearly full, maintain   | Constant voltage regulation       |
| SA_LOAD_FOLLOW     | Yes  | Full                    | Match solar output to load demand |
| BATTERY_DISCHARGE  | No   | Any                     | Converter off, battery powers all |

Source: mission doc p.100-103, Section 3.4.2.2.4, Figures 3.4.6-3.4.10.

### Why two separate levels?

The OBC has information the EPS does not: orbit predictions, ground
commands, mission schedule, payload status. The EPS has information the
OBC does not (or at least has faster access to): real-time battery voltage,
solar array voltage, battery current, battery temperature.

The split lets each processor make the decisions it is best equipped for.
The OBC handles mission-level planning. The EPS handles millisecond-level
power control. Neither can do the other's job well.

### OBC-EPS communication: UART/CHIPS protocol

The OBC and EPS communicate over UART using the CHIPS protocol (CHESS
Internal Protocol over Serial). Despite some references in the mission
document to I2C (e.g., p.95), the actual communication bus is UART. The
protocol name itself — "CHESS Internal Protocol over **Serial**" —
confirms this. UART is the correct interface.

Key properties of the communication:

- **Master/slave architecture.** The OBC is always the master. The EPS is
  always the slave. The EPS **cannot initiate** communication. It can
  only respond when the OBC polls it.

- **Polling model.** The OBC periodically sends a request to the EPS. The
  EPS responds with its current telemetry (battery voltage, current, PCU
  mode, alert flags, etc.). The OBC includes the current satellite mode
  and safe mode sub-state in its request.

- **120-second autonomy timeout.** If the EPS receives no communication
  from the OBC for 120 seconds, it assumes the OBC is dead or
  unresponsive. The EPS then enters autonomous safe mode and manages
  loads on its own.

Source: mission doc p.125 for the 120s timeout. CHIPS protocol
specification for UART bus type.

### How the EPS "requests" safe mode (passively)

The EPS cannot send a message to the OBC saying "enter safe mode." It is
a slave — it can only respond to polls. So when the EPS detects a
dangerous condition (critically low battery, extreme temperature), it does
two things:

1. **Takes immediate local action.** Sheds loads, disables charging,
   activates heater — whatever is needed to protect the battery right now.

2. **Sets a flag in its telemetry.** The `safe_mode_alert_flag_for_obc`
   and `safe_mode_alert_reason` fields in the actuator output struct are
   stored in the EPS housekeeping data (HK_EPS_ALERT_LOG, mission doc
   p.36). The next time the OBC polls the EPS, it reads these flags and
   can decide to transition the satellite to SAFE mode.

This is a passive request. The EPS cannot force the OBC to do anything.
But the EPS has already protected the battery regardless of whether the
OBC acts on the alert.

---

## 3. PCU state machine detail

### The four modes

The PCU state machine is the core of the EPS firmware. It runs once per
superloop iteration (approximately every 200 microseconds at 48 MHz with
ADC reads). Each iteration, it reads sensor data, runs the logic for the
current mode, checks for mode transitions, and outputs actuator commands.

Source for all four modes: mission doc Figures 3.4.6-3.4.10 (p.100-103).

#### MPPT_CHARGE — "Sun is available, battery needs charging"

This is the primary charging mode. The buck converter is active and the
MPPT (Maximum Power Point Tracking) algorithm adjusts the duty cycle to
extract maximum power from the solar panels.

**What happens each iteration:**

1. Solar panel eFuse is enabled (panel connected to converter).
2. Safety checks run first:
   - If battery temperature < 0 C: duty cycle forced to minimum (no
     charging — lithium-ion safety).
   - If battery current > maximum charge current: duty cycle decreased
     (overcurrent protection).
   - If battery voltage > maximum voltage (8400 mV): duty cycle decreased
     (overvoltage protection).
3. If no safety condition triggered: the Incremental Conductance MPPT
   algorithm runs and returns the new duty cycle.
4. Check for mode transitions:
   - Solar voltage dropped below 8200 mV? Transition to BATTERY_DISCHARGE.
   - Battery voltage reached "full" threshold? Transition to SA_LOAD_FOLLOW.
   - Timeout expired with insufficient charge? Transition to CV_FLOAT.

Source: mission doc p.101, Figure 3.4.7.

#### CV_FLOAT — "Battery is nearly full, maintain voltage"

Once the battery is near full charge, the converter switches from MPPT
tracking to constant-voltage regulation. The goal is to hold the battery
at its maximum voltage without overcharging.

CV_FLOAT has an internal sub-state for handling transient load spikes:

- **NORMAL:** Bang-bang voltage regulation. Each iteration, compare the
  charging rail voltage to the target (battery maximum voltage). If below
  target, increase duty cycle by one step. If above, decrease by one step.

- **TEMP_MPPT:** If a large transient load causes heavy battery discharge
  (battery current drops below the maximum discharge threshold), the
  converter temporarily switches back to MPPT to extract maximum solar
  power. When battery current returns to non-negative, it goes back to
  NORMAL.

**Mode transitions from CV_FLOAT:**

- Solar voltage dropped below 8200 mV? Transition to BATTERY_DISCHARGE.
- Battery voltage stayed below the charge-resume threshold for t2
  iterations? Transition to MPPT_CHARGE (battery needs more charging).

Source: mission doc p.101-102, Figure 3.4.8.

#### SA_LOAD_FOLLOW — "Battery is full, match solar to load"

When the battery is fully charged and solar power is available, there is
no point in continuing to charge. Instead, the converter adjusts its
output to match the current load demand. The MPPT algorithm still runs
(to track available solar power), but the duty cycle is reduced if battery
charging current exceeds a minimum threshold — the goal is zero net
battery charge.

**Mode transitions from SA_LOAD_FOLLOW:**

- Solar voltage dropped below 8200 mV? Transition to BATTERY_DISCHARGE.
- Battery voltage dropped below "full" threshold? Transition to
  MPPT_CHARGE (battery needs charging again).

Source: mission doc p.102-103, Figure 3.4.9.

#### BATTERY_DISCHARGE — "No sun, running on battery"

In eclipse (or when solar panels are stowed, such as during LEOP), there
is no solar power. The buck converter is turned off (duty cycle = minimum).
The solar panel eFuse is opened (panel disconnected). The battery is the
sole power source.

**What happens each iteration:**

1. Solar panel eFuse disabled.
2. Duty cycle set to minimum (converter effectively off).
3. Check if solar has returned (voltage >= 8200 mV). If yes, transition
   to MPPT_CHARGE.
4. Check if battery voltage is critically low (below minimum + hysteresis).
   If yes, request safe mode.
5. Check if discharge current exceeds maximum. If yes, shed the lowest
   priority load that is still enabled.

Source: mission doc p.103, Figure 3.4.10.

### Mode transition design decision: approach B

There are two ways to implement mode transitions in a state machine:

**Approach A — Re-run the top-level tree every iteration.** Each iteration,
start from scratch: check solar availability, check battery voltage, and
select the mode from the top. Simple but fragile: if battery voltage
oscillates around a threshold, the mode flips back and forth every
iteration. This causes the MPPT algorithm to be re-initialized repeatedly,
destroying its tracking state.

**Approach B — Each mode handles its own transitions.** The current mode
runs its logic, then checks whether conditions warrant a specific
transition. If not, it stays in the current mode. This approach naturally
provides hysteresis (you leave a mode only when a specific exit condition
is met, not just because the entry condition for another mode is marginally
true).

This firmware uses **Approach B**. Each of the four mode handler functions
(`run_mppt_charge_mode_logic`, `run_cv_float_mode_logic`, etc.) contains
its own transition checks at the end. The top-level dispatch function
(`run_pcu_mode_logic_for_current_operating_mode`) simply calls the handler
for the current mode.

The main benefit: the MPPT algorithm state is preserved as long as the
mode does not change. When a mode transition does occur, the MPPT state
is re-initialized only if the new mode uses MPPT (MPPT_CHARGE or the
TEMP_MPPT sub-state of CV_FLOAT).

### Mode transition summary

```
                   Solar available?
                  /                \
                Yes                 No
                 |                   |
          Battery full?        BATTERY_DISCHARGE
         /             \              |
       No              Yes            | (solar returns)
        |               |             v
   MPPT_CHARGE    SA_LOAD_FOLLOW --> MPPT_CHARGE
        |               |
        | (full)        | (voltage dropped)
        v               v
   SA_LOAD_FOLLOW  MPPT_CHARGE
        |
        | (timeout with low charge buffer)
        v
    CV_FLOAT
        |
        | (voltage stayed low for t2)
        v
   MPPT_CHARGE
```

Every transition to BATTERY_DISCHARGE: solar dropped below 8200 mV.
Every transition out of BATTERY_DISCHARGE: solar rose above 8200 mV.
Every transition resets `iterations_in_current_pcu_mode` to zero.

---

## 4. Safe mode

### What safe mode is

Safe mode is the satellite's survival state. Something has gone wrong —
the battery is dying, the OBC is unresponsive, temperatures are extreme —
and the satellite must minimize power consumption to survive.

Safe mode is NOT a single static state. It has four sub-states, each
requiring a different set of powered subsystems (load configuration).
The EPS is directly involved in safe mode because it controls the eFuses
that enable or disable power to each subsystem.

Source: mission doc p.24-25, Section 1.7.4.2.

### Safe mode triggers

The mission document (p.23) lists 8 conditions that can trigger safe mode.
The EPS can directly detect triggers 2, 5, 6, and 7 from its own sensors,
plus the 120-second OBC timeout:

| # | Trigger                                      | EPS can detect? |
|---|----------------------------------------------|-----------------|
| 1 | OBC detects anomalous behavior               | No (OBC only)   |
| 2 | Battery voltage below minimum (5000 mV)      | Yes             |
| 3 | Ground command                                | No (OBC relays) |
| 4 | Payload anomaly                               | No (OBC only)   |
| 5 | Battery temperature below TempMin             | Yes             |
| 6 | Battery temperature above TempMax             | Yes             |
| 7 | Overcurrent on battery discharge rail         | Yes             |
| 8 | 120s OBC heartbeat timeout                    | Yes             |

Source: mission doc p.23 for the trigger list.

When the EPS detects a trigger, it:
1. Sets `safe_mode_is_active = 1` in its persistent state.
2. Sets the alert flag and reason in the actuator output (stored in
   HK_EPS_ALERT_LOG, mission doc p.36).
3. Takes immediate protective action (load shedding, heater activation,
   charging prohibition as appropriate).

### Safe mode sub-states and load profiles

Safe mode has four sub-states, each with a distinct purpose and a different
set of subsystems that must remain powered. The EPS controls which
subsystems receive power by enabling or disabling their eFuses.

**DETUMBLING** — Restoring attitude stability after an anomaly. The
satellite may be tumbling, so the ADCS (Attitude Determination and Control
System) must be powered to run the magnetorquers. UHF is kept on for
beacon transmission.

| Subsystem     | Powered? | Reason                              |
|---------------|----------|--------------------------------------|
| OBC           | Yes      | Always powered                       |
| ADCS          | Yes      | Needed for magnetorquer detumbling   |
| UHF Radio     | Yes      | Beacon transmission                  |
| SPAD Camera   | No       | Non-essential, shed                  |
| GNSS Receiver | No       | Non-essential, shed                  |

**CHARGING** — Battery is critically low. Maximum load shedding to
direct all available solar power into the battery. Only the OBC and UHF
beacon remain powered.

| Subsystem     | Powered? | Reason                              |
|---------------|----------|--------------------------------------|
| OBC           | Yes      | Always powered                       |
| ADCS          | No       | Shed to save power                   |
| UHF Radio     | Yes      | Beacon transmission (minimal power)  |
| SPAD Camera   | No       | Non-essential, shed                  |
| GNSS Receiver | No       | Non-essential, shed                  |

**COMMUNICATION** — Lost contact with ground station. The UHF radio is
powered at full transmit power to beacon and attempt contact. ADCS is
powered to maintain attitude for the antenna.

| Subsystem     | Powered? | Reason                              |
|---------------|----------|--------------------------------------|
| OBC           | Yes      | Always powered                       |
| ADCS          | Yes      | Attitude control for antenna pointing|
| UHF Radio     | Yes      | Full power for beacon/contact        |
| SPAD Camera   | No       | Non-essential, shed                  |
| GNSS Receiver | No       | Non-essential, shed                  |

**REBOOT** — Cycling power to subsystems to clear stuck hardware or
software. The EPS keeps UHF and ADCS powered while other subsystems
are power-cycled.

| Subsystem     | Powered? | Reason                              |
|---------------|----------|--------------------------------------|
| OBC           | Yes      | Always powered (rebooted externally) |
| ADCS          | Yes      | Maintained during reboot cycle       |
| UHF Radio     | Yes      | Maintained during reboot cycle       |
| SPAD Camera   | No       | Non-essential, shed                  |
| GNSS Receiver | No       | Non-essential, shed                  |

Source: mission doc p.24, Section 1.7.4.2 for sub-state definitions.

### Who decides the sub-state?

Under normal conditions (OBC alive), the OBC commands the safe mode
sub-state via the CHIPS polling message. The EPS reads the
`safe_mode_sub_state_commanded_by_obc` field from the sensor readings
and applies the corresponding load profile.

If the OBC is dead (120s heartbeat timeout), the EPS selects the
sub-state autonomously:

- Battery voltage < critical threshold (5500 mV) --> CHARGING sub-state
  (maximize charging, minimize load).
- Otherwise --> COMMUNICATION sub-state (try to beacon for ground
  contact).

This autonomous logic is a last resort. The EPS has no orbit knowledge,
no ground schedule, no mission planning. It picks the most conservative
option: charge if dying, beacon if not.

### Exiting safe mode

Safe mode exit requires a ground command (per ECSS-E-ST-70-11C standard,
referenced in mission doc p.26). The OBC relays this by commanding a
non-SAFE satellite mode. When the EPS sees `satellite_mode_commanded_by_obc`
change from SAFE to any other mode, it:

1. Clears `safe_mode_is_active`.
2. Re-enables all loads.

The EPS cannot exit safe mode on its own. Even if the battery recovers
to full charge, the EPS stays in safe mode until the OBC commands
otherwise. This is a deliberate safety design: safe mode should be sticky
so ground operators can investigate the anomaly before resuming normal
operations.

---

## 5. Thermal control

The EPS manages three temperature-related functions. All use the battery
temperature measured via a thermistor over SPI, stored as deci-degrees
Celsius (int16_t, so -10.0 C = -100, 60.0 C = 600).

### Battery heater activation

If battery temperature drops below TempMin (configurable, placeholder
-10.0 C), the EPS activates the battery heater by setting
`heater_should_be_enabled = 1` in the actuator output. When temperature
rises above TempMin, the heater is turned off.

Source: mission doc p.25 — "If temperature falls below TempMin, heaters
are activated."

### Charging prohibition below 0 C

Lithium-ion batteries must **never** be charged below 0 C. Charging below
freezing causes lithium plating on the anode, permanently and irreversibly
damaging the battery. This is not a configurable threshold — it is a
physics constraint.

If battery temperature < 0 C and the EPS is in any charging mode
(MPPT_CHARGE, CV_FLOAT, or SA_LOAD_FOLLOW), the duty cycle is forced to
minimum. The converter effectively stops transferring solar power to the
battery. The battery heater (if active) runs from existing battery charge.

This check runs as a final override in the thermal control function,
after the PCU mode logic has already set its duty cycle. It cannot be
overridden by any other logic.

### Overtemperature load shedding

If battery temperature exceeds TempMax (configurable, placeholder 60.0 C),
the EPS sheds the lowest-priority load that is still enabled to reduce
heat generation. This uses the same priority-based load shedding mechanism
described in Part 6.

Source: mission doc p.25 — "If temperature exceeds TempMax, non-essential
systems remain OFF to prevent overheating."

---

## 6. Load shedding

### What load shedding is

Load shedding means turning off a subsystem's power to reduce total
current draw from the battery. The EPS controls power to five subsystems
via electronic fuses (eFuses). Each eFuse can be enabled (load powered)
or disabled (load shed).

### Priority order

Loads are shed in strict priority order, from lowest priority (shed first)
to highest (shed last):

| Priority | Subsystem      | Shed order | Notes                       |
|----------|----------------|------------|-----------------------------|
| 0 (low)  | SPAD Camera    | First      | Science payload, non-essential|
| 1        | GNSS Receiver  | Second     | Navigation, non-essential    |
| 2        | UHF Radio      | Third      | Communication, important     |
| 3        | ADCS           | Fourth     | Attitude control, critical   |
| 4 (high) | OBC            | Never      | Cannot be shed by firmware   |

Source: mission doc p.25, Figure 1.7.1 (p.24) for the priority hierarchy.

### One load per iteration

A critical design decision: the firmware sheds **at most one load per
iteration**. If battery current exceeds the maximum discharge threshold,
the lowest-priority load that is still enabled is shed. On the next
iteration, if the current is still too high, the next load is shed. And
so on.

Why not shed multiple loads at once? Because shedding a load changes the
current draw, and we need to measure the effect before deciding if more
shedding is needed. Shedding multiple loads in one iteration could
over-shed, leaving the satellite without communication or attitude control
when one fewer shed would have been sufficient.

The iteration period is approximately 200 microseconds. Even shedding one
load per iteration, the firmware can shed all four non-essential loads in
less than 1 millisecond. This is fast enough for any realistic power
emergency.

### When load shedding happens

Load shedding is triggered by three conditions:

1. **Battery overcurrent in BATTERY_DISCHARGE mode.** Discharge current
   exceeds the maximum discharge threshold. The battery is being drained
   too fast. Source: mission doc Figure 3.4.10 (p.103).

2. **Overtemperature.** Battery temperature exceeds TempMax. Source:
   mission doc p.25.

3. **Safe mode sub-state load profiles.** Each safe mode sub-state has a
   fixed set of loads that must be shed. This is not gradual — the
   load profile is applied immediately based on the sub-state. Source:
   mission doc p.24, Section 1.7.4.2.

---

## 7. Hardware protection

### The LM139 comparator — hardware-level battery protection

The EPS PCB includes an LM139 comparator circuit that monitors battery
voltage independently of the microcontroller firmware. If battery voltage
reaches 8.4 V (the absolute maximum for the lithium-ion cells), the
comparator directly cuts the buck converter's enable signal, stopping
all charging current.

This is a pure hardware protection mechanism. It works even if the MCU
firmware has crashed, is in an infinite loop, or has a bug. It is the
last line of defense against battery overcharge.

Source: mission doc p.99 for the 8.4 V maximum battery voltage.

### eFuse enable logic — fail-safe defaults

The eFuses that control power to each subsystem are designed with a
fail-safe default: **eFuses default to OFF if the MCU is not actively
driving them.** This means:

- If the MCU crashes and stops driving GPIO pins, all load eFuses turn
  off. The battery is protected from uncontrolled discharge.
- If the MCU resets, all loads start unpowered. The firmware must
  explicitly enable each load during initialization.

This is why the `eps_state_machine_initialize()` function enables all
loads as one of its first actions — without this, no subsystem would
receive power after a reset.

### Firmware role in hardware protection

The firmware does not implement the hardware comparator or eFuse default
logic — that is all in the PCB design. The firmware's role is:

1. **Read fault GPIO pins.** The eFuse ICs and comparator circuit expose
   fault status signals on GPIO pins. The firmware can read these to
   detect if a hardware protection event has occurred (e.g., the
   comparator tripped, an eFuse detected an overcurrent).

2. **Coordinate with hardware protection.** The firmware's voltage
   thresholds are set conservatively below the hardware cutoffs. For
   example, the firmware starts reducing duty cycle when battery voltage
   approaches 8300 mV (the "full" threshold), well before the hardware
   comparator trips at 8400 mV. The firmware is the fine-grained
   controller; the hardware is the emergency stop.

3. **Report hardware faults to OBC.** If a hardware protection event is
   detected via the fault GPIO pins, the firmware includes this
   information in its telemetry for the OBC to report to ground.

---

## 8. Threshold values table

All thresholds are configurable at initialization time via the
`struct eps_configuration_thresholds` in `eps_configuration_parameters.h`.
No magic numbers exist anywhere else in the code. Values marked "TBD" are
placeholders awaiting finalization by the battery team, thermal team, or
mission operations.

### Battery voltage thresholds

| Threshold            | Value    | Unit | Source                                  |
|----------------------|----------|------|-----------------------------------------|
| Maximum voltage      | 8400     | mV   | Mission doc p.99 — "Battery: 8.4V max"  |
| Full threshold       | 8300     | mV   | Placeholder (100 mV below max), TBD     |
| Charge resume        | 8100     | mV   | Placeholder (300 mV below max), TBD     |
| Minimum voltage      | 5000     | mV   | Mission doc p.99 — "Battery: 5V min"    |
| Critical voltage     | 5500     | mV   | Placeholder (500 mV above min), TBD     |
| Hysteresis margin    | 200      | mV   | Placeholder, TBD                        |

### Battery current thresholds

| Threshold                  | Value  | Unit | Source                            |
|----------------------------|--------|------|-----------------------------------|
| Max charge current         | 2000   | mA   | Placeholder, TBD by battery team  |
| Max discharge current      | -2000  | mA   | Placeholder, TBD by battery team  |
| Min charge threshold (S.A.)| 100    | mA   | Placeholder, TBD                  |

### Solar array threshold

| Threshold                  | Value  | Unit | Source                                            |
|----------------------------|--------|------|---------------------------------------------------|
| Minimum voltage for availability | 8200 | mV | Mission doc Table 3.4.1, p.94: "Input Voltage (PV): 8.2V - 18.34V" |

The 8200 mV threshold is an engineering inference: the buck converter's
specified input voltage range starts at 8.2 V. Below this voltage, the
converter cannot operate within its design parameters. The mission document
does not explicitly define a "solar available" threshold, so we use the
converter's minimum input voltage.

### Temperature thresholds

| Threshold                  | Value  | Unit         | Source                          |
|----------------------------|--------|--------------|----------------------------------|
| Heater activation (TempMin)| -100   | deci-deg C   | Placeholder (-10.0 C), TBD      |
| Load shed (TempMax)        | 600    | deci-deg C   | Placeholder (60.0 C), TBD       |
| Charging prohibition       | 0      | deci-deg C   | Physics: 0 C, non-configurable  |

### Timeout thresholds

| Threshold                  | Value        | Unit       | Source                                       |
|----------------------------|--------------|------------|----------------------------------------------|
| OBC heartbeat timeout      | 600000*      | iterations | Mission doc p.125: 120s. At ~200us/iter = 600000 |
| MPPT charge timeout        | TBD          | iterations | Mission doc p.101                            |
| CV_FLOAT low-voltage wait  | TBD          | iterations | Mission doc p.102, labeled "t2"              |

*The iteration count depends on the actual superloop period. 600000 assumes
~200 microseconds per iteration (120s / 0.0002s = 600000).

### Duty cycle parameters

| Parameter                  | Value  | Unit    | Source                          |
|----------------------------|--------|---------|----------------------------------|
| Full scale                 | 65535  | counts  | 16-bit PWM resolution            |
| Minimum (5%)               | 3277   | counts  | 0.05 * 65535                     |
| Maximum (95%)              | 62258  | counts  | 0.95 * 65535                     |
| Step size (0.5%)           | 328    | counts  | 0.005 * 65535                    |
| CV_FLOAT adjustment step   | 164    | counts  | Placeholder (~0.25%), TBD        |

---

## 9. Code design overview

### File structure

The state machine implementation spans three files:

```
src/
  eps_state_machine.h              Public interface (enums, structs, function prototypes)
  eps_state_machine.c              Implementation (all mode logic, safe mode, thermal, shedding)
  eps_configuration_parameters.h   All configurable thresholds in one struct
  mppt_algorithm.h                 MPPT algorithm interface (called by state machine)
  mppt_algorithm.c                 Incremental Conductance implementation
  assertion_handler.h              SATELLITE_ASSERT macro (active in all builds)
```

### Enums

All enums have explicitly assigned integer values (per the project's
conventions.md Rule C15). This prevents the compiler from silently
reordering values if someone adds or removes an entry.

```c
enum eps_pcu_operating_mode {
    EPS_PCU_MODE_MPPT_CHARGE       = 0,
    EPS_PCU_MODE_CV_FLOAT          = 1,
    EPS_PCU_MODE_SA_LOAD_FOLLOW    = 2,
    EPS_PCU_MODE_BATTERY_DISCHARGE = 3
};

enum eps_satellite_operating_mode {
    EPS_SATELLITE_MODE_MEASUREMENT       = 0,
    EPS_SATELLITE_MODE_CHARGING          = 1,
    EPS_SATELLITE_MODE_UHF_COMMUNICATION = 2,
    EPS_SATELLITE_MODE_SAFE              = 3
};

enum eps_safe_mode_sub_state {
    EPS_SAFE_SUB_STATE_DETUMBLING    = 0,
    EPS_SAFE_SUB_STATE_CHARGING      = 1,
    EPS_SAFE_SUB_STATE_COMMUNICATION = 2,
    EPS_SAFE_SUB_STATE_REBOOT        = 3
};

enum eps_load_identifier {
    EPS_LOAD_SPAD_CAMERA   = 0,   // Lowest priority — shed first
    EPS_LOAD_GNSS_RECEIVER = 1,
    EPS_LOAD_UHF_RADIO     = 2,
    EPS_LOAD_ADCS          = 3,
    EPS_LOAD_OBC           = 4,   // Highest priority — never shed
    EPS_LOAD_COUNT         = 5
};

enum eps_safe_mode_reason {
    EPS_SAFE_REASON_NONE                     = 0,
    EPS_SAFE_REASON_BATTERY_BELOW_MINIMUM    = 1,
    EPS_SAFE_REASON_TEMPERATURE_OUT_OF_RANGE = 2,
    EPS_SAFE_REASON_OBC_HEARTBEAT_TIMEOUT    = 3
};

enum eps_cv_float_substate {
    EPS_CV_FLOAT_SUBSTATE_NORMAL    = 0,
    EPS_CV_FLOAT_SUBSTATE_TEMP_MPPT = 1
};
```

### Key structs

**Input: `struct eps_sensor_readings_this_iteration`** — Everything the
state machine reads each iteration. Filled by the main loop from ADC/I2C/SPI
readings (on real hardware) or from physics models (in simulation).

**Output: `struct eps_actuator_output_commands`** — Everything the state
machine commands each iteration. Read by the main loop and applied to
hardware (PWM duty cycle, eFuse enables, heater, alert flags).

**State: `struct eps_state_machine_persistent_state`** — All internal
state that persists between iterations. Current PCU mode, safe mode
status, iteration counters, MPPT algorithm state, per-load enable flags.
Passed by pointer per conventions.md Rule B2. No global variables.

**Config: `struct eps_configuration_thresholds`** — All configurable
thresholds. Passed as `const` pointer. Set once at initialization, not
modified at runtime (though the OBC could potentially update thresholds
via a CHIPS command in a future revision).

### Public function signatures

```c
// Initialize the state machine. Called once at boot.
// initial_pcu_operating_mode: for LEOP, pass EPS_PCU_MODE_BATTERY_DISCHARGE
// (solar panels are stowed at boot, mission doc p.27).
// For simulation, pass whatever the scenario requires.
void eps_state_machine_initialize(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    uint8_t initial_pcu_operating_mode);

// Run one iteration. Called once per superloop tick.
// Pure function of (persistent_state + sensor_readings + config) -> actuator_commands.
// No side effects beyond modifying *eps_persistent_state and *actuator_commands_output.
void eps_state_machine_run_one_iteration(
    struct eps_state_machine_persistent_state *eps_persistent_state,
    const struct eps_sensor_readings_this_iteration *sensor_readings_this_iteration,
    const struct eps_configuration_thresholds *eps_configuration_thresholds,
    struct eps_actuator_output_commands *actuator_commands_output);
```

### Execution order within one iteration

The `eps_state_machine_run_one_iteration` function executes these steps in
order:

1. **Copy current load state to output.** The actuator output starts with
   whatever loads are currently enabled.

2. **Update OBC heartbeat counter.** If a heartbeat was received this
   iteration, reset the counter. Otherwise, increment it.

3. **Check safe mode entry conditions.** Battery voltage, temperature, and
   OBC timeout. If any trigger fires, set safe mode active and record the
   reason.

4. **If safe mode is active:** Apply the sub-state-specific load profile.
   Check if the OBC has commanded exit from safe mode.

5. **Run PCU mode logic.** Dispatch to the handler for the current mode
   (MPPT_CHARGE, CV_FLOAT, SA_LOAD_FOLLOW, or BATTERY_DISCHARGE). The
   handler sets the duty cycle and checks for mode transitions.

6. **Apply thermal control.** Heater activation/deactivation, overtemp
   load shedding, and the charging prohibition below 0 C override.

7. **Sync output duty cycle.** Copy the final duty cycle from persistent
   state to the actuator output (mode handlers may return early during
   transitions without setting the output directly).

8. **Set telemetry fields.** Copy current PCU mode to the telemetry output
   field.

9. **Post-condition assertions.** Verify the output duty cycle is within
   the safe [5%, 95%] range.

### Initial mode at boot

The initial PCU mode is **configurable via parameter** — the
`initial_pcu_operating_mode` argument to `eps_state_machine_initialize()`.
This is important because different mission phases require different
starting conditions:

- **LEOP (Launch and Early Orbit Phase):** The solar panels are stowed
  against the satellite body at launch. There is no solar power available
  until the panels are deployed. The correct initial mode is
  `EPS_PCU_MODE_BATTERY_DISCHARGE`. Source: mission doc p.27.

- **Simulation testing:** The initial mode can be set to whatever the
  test scenario requires (e.g., start in MPPT_CHARGE to test charging
  behavior).

- **Recovery from watchdog reset:** The firmware could be configured to
  start in a conservative mode and let the OBC command the appropriate
  mode once communication is established.

### Testing strategy

Because the state machine is pure logic (Category 1 — zero hardware
dependencies), it compiles and runs on a laptop with native GCC:

```bash
gcc -Wall -Wextra -std=c99 \
    src/eps_state_machine.c \
    src/mppt_algorithm.c \
    src/assertion_handler.c \
    tests/eps_state_machine_test.c \
    -o tests/run_eps_test
```

The test harness creates a `struct eps_sensor_readings_this_iteration`,
fills it with known values, calls `eps_state_machine_run_one_iteration()`,
and checks the `struct eps_actuator_output_commands` for expected outputs.
Every mode, every transition, every safe mode sub-state, and every
threshold boundary can be tested this way without touching any hardware.

The Streamlit-based simulation (`tools/mppt_app.py`) provides a visual,
interactive way to exercise the state machine with realistic solar panel
and battery physics models.
