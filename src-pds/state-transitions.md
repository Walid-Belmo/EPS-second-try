# State Transitions

Per-state transition logic for the EPS power state machine, written in plain
if-then form. Includes the arrows drawn in the mission doc figures plus the
implied arrows we agreed are necessary for the FSM to be complete.

⚠️ **What this document is, and what the firmware actually does**

This document describes the eventual flight-grade state machine — every per-state
entry action, internal duty algorithm, timer, sub-state, and hysteresis rule.

**This is NOT what the firmware currently implements.**

The spec we inherited from the mission doc has real ambiguities (drawn diagrams
contradict each other in places, several thresholds are TBD, and many transition
arrows that the physics requires are not drawn). Resolving all of that properly
requires conversations and hardware data we don't have yet.

What we ship for now is a single **pure `update_state` function**: it takes all
inputs (sensor readings plus stateful counters like the heartbeat counter and
the safe-mode-latched flag) and returns the next state. Safe-mode entry, the
safe-mode persistence rule (only OBC command exits), and sub-state selection
all live inside that function. The set of base cases is small enough (two
variables — solar availability and battery voltage — yield four normal modes,
plus the safe-mode override) that a human can manually verify the function
against the dispatcher logic at the end of this document.

The per-state complexity described in the rest of this doc exists to
compensate for real-hardware effects: sensor noise, battery dynamics, transient
load spikes, debounce around threshold crossings. On a bench simulator with
clean injected values, none of those effects appear, so the pure function
behaves correctly without the extra machinery.

Once we test against real hardware, we will incrementally layer in the
per-state logic from this document as real-world misbehavior shows up.
The open issues at the bottom of this file are the punch list for that work.

---

## Simplification: one voltage on the battery side

The mission doc names two signals on the battery side of the buck converter:
`Vbat` (battery terminal voltage) and `Vchg_rail` (the rail between buck and
battery). The PCU block diagram shows only one voltmeter on that side, so we
treat them as a single MCU input. Everywhere in this document we use `Vbat`.
If hardware turns out to expose two separate sensors, the FSM logic does not
change — both readings track each other while the battery eFuse is closed,
which is every mode except BATTERY_DISCHARGE (and BATTERY_DISCHARGE does not
use this signal).

---

## Glossary of inputs and thresholds

**Sensor inputs** (raw or filtered, one reading per control cycle):

- `Vbat` — battery voltage (mV)
- `Ibat` — battery current (mA, signed: + charging, − discharging)
- `Vpanel` — solar panel voltage (mV)
- `Ipanel` — solar panel current (mA)
- `Tbat` — battery temperature (0.1 °C)
- `heartbeat_received` — 1 if OBC sent a heartbeat this iteration
- `obc_mode` — satellite mode commanded by OBC
- `obc_safe_substate` — safe-mode sub-state commanded by OBC
- `faults` — bitmask of injected/triggered fault flags

**Derived predicates** (computed once per cycle from the inputs above):

- `SA_available` ≡ `Vpanel ≥ Vpanel_min_avail`
- `battery_full` ≡ `Vbat ≥ Vbat_full AND |Ibat| < Ibat_taper`
- `battery_critical` ≡ `Vbat < Vbat_critical`
- `battery_below_minimum` ≡ `Vbat < Vbat_min`
- `temp_out_of_range` ≡ `Tbat < Temp_min OR Tbat > Temp_max`
- `heartbeat_timeout` ≡ `iterations_since_last_heartbeat > heartbeat_timeout_count`

**Thresholds** (numeric values — placeholders until tuned):

| Name | Meaning | Provisional value |
|---|---|---|
| `Vbat_max` | Battery max safe voltage | 8400 mV |
| `Vbat_full` | "Battery full" threshold | 8300 mV |
| `Vbat_charge_resume` | Hysteresis: resume MPPT below this | 8100 mV |
| `Vbat_min` | Minimum safe voltage | 5000 mV |
| `Vbat_critical` | Critical low | 5500 mV |
| `D` | Hysteresis margin | 200 mV |
| `Ibat_max_charge` | Max safe charge current | 2000 mA |
| `Ibat_max_discharge` | Max safe discharge (signed) | −2000 mA |
| `Ibat_taper` | "Near zero" current for battery_full | 100 mA |
| `Vpanel_min_avail` | Panel voltage to count as available | 8200 mV |
| `Temp_min` | Lowest safe battery temp | −10 °C |
| `Temp_max` | Highest safe battery temp | 60 °C |
| `t1` | MPPT_CHARGE timeout to CV_FLOAT | TBD iterations |
| `t2` | CV_FLOAT debounce before MPPT_CHARGE | TBD iterations |
| `heartbeat_timeout_count` | Iterations without OBC heartbeat | TBD (~120 s worth) |

---

## Safety check (runs first, every iteration, before any state)

Safety overrides are checked at the top of every control cycle, before the
current state's own logic. If any condition is true, the FSM transitions
immediately to SAFE_MODE.

```
IF battery_below_minimum:
    → SAFE_MODE (reason = BATTERY_BELOW_MIN)

ELSE IF temp_out_of_range:
    → SAFE_MODE (reason = TEMPERATURE_OUT_OF_RANGE)

ELSE IF heartbeat_timeout:
    → SAFE_MODE (reason = OBC_HEARTBEAT_TIMEOUT)
    autonomous_substate = (battery_critical ? CHARGING : COMMUNICATION)
```

If none trip, fall through to the current state's own handler.

---

## State: MPPT_CHARGE

**Purpose:** Sun is available, battery is not full — extract as much power
from the panel as possible and direct it to the battery.

**Entry action:** Reset MPPT algorithm state. Close panel eFuse. Reset `t1`
timer counter to 0.

**Internal behavior (per cycle, no state change):**

```
IF Ibat > Ibat_max_charge:
    reduce duty cycle

ELSE IF Vbat > Vbat_max:
    reduce duty cycle

ELSE:
    run incremental conductance MPPT
    adjust PWM
```

**Exit conditions:**

```
IF NOT SA_available:
    → BATTERY_DISCHARGE

ELSE IF battery_full:
    → SA_LOAD_FOLLOW

ELSE IF t1_elapsed AND charging_buffer_insufficient:
    → CV_FLOAT
```

Note: `charging_buffer_insufficient` means MPPT has been running for `t1`
iterations and the battery still isn't progressing toward full — operational
sign that the battery has entered constant-voltage phase.

---

## State: CV_FLOAT

**Purpose:** Battery is at or near max voltage. Hold the battery voltage
constant at `Vbat_max` so the battery doesn't overcharge, while the panel
keeps powering loads.

CV_FLOAT has one normal mode and one sub-state called TEMP_MPPT.

**Entry action:** Reset `t2` timer counter to 0. Close panel eFuse. Set
sub-state to NORMAL.

**Internal behavior (NORMAL sub-state, per cycle):**

```
IF Vbat < Vbat_max:
    increase duty cycle

ELSE IF Vbat ≥ Vbat_max:
    decrease duty cycle
```


**Sub-state TEMP_MPPT** is entered when a transient load causes the battery
to discharge briefly. While in TEMP_MPPT, run the MPPT algorithm to maximize
panel extraction until the discharge stops.

**Exit conditions:**

```
IF NOT SA_available:
    → BATTERY_DISCHARGE

ELSE IF Ibat < Ibat_max_discharge AND sub-state == NORMAL:
    → CV_FLOAT (sub-state = TEMP_MPPT)

ELSE IF Ibat ≥ 0 AND sub-state == TEMP_MPPT:
    → CV_FLOAT (sub-state = NORMAL)

ELSE IF Vbat < Vbat_charge_resume:
    increment t2 counter
    IF t2_elapsed AND Vbat still < Vbat_charge_resume:
        → MPPT_CHARGE
ELSE:
    reset t2 counter to 0
```

---

## State: SA_LOAD_FOLLOW

**Purpose:** Battery is full and sun is available. Run the panel only hard
enough to cover the load, do not push current into a full battery.

**Entry action:** Close panel eFuse. Reset MPPT state.

**Internal behavior (per cycle):**

```
IF Ibat > Ibat_min_charge_clamp:
    reduce and clamp duty cycle (prevent overcharge)

ELSE:
    run incremental conductance MPPT
    adjust PWM
```

`Ibat_min_charge_clamp` is the small positive current above which we judge
the battery is being charged — in load-follow we want approximately zero
net battery current.

**Exit conditions:**

```
IF NOT SA_available:
    → BATTERY_DISCHARGE

ELSE IF Vbat < Vbat_full:
    → MPPT_CHARGE
```

---

## State: BATTERY_DISCHARGE

**Purpose:** No sun. Battery powers everything. Panel is disconnected.

**Entry action:** Open panel eFuse (disconnect array). Force buck converter
duty cycle to minimum. Reset MPPT state.

**Internal behavior (per cycle):**

```
IF Ibat < Ibat_max_discharge:
    diagnose discharge overcurrent
    shed lowest-priority enabled load
```

Loads are shed one per iteration in priority order (lowest first): SPAD →
GNSS → UHF → ADCS → OBC (OBC never shed).

**Exit conditions:**

```
IF SA_available:
    → MPPT_CHARGE
```

Note: the safety check at the top of the cycle already handles
`battery_below_minimum` → SAFE_MODE, so it doesn't need to be repeated here.

---

## State: SAFE_MODE

**Purpose:** Protect the satellite. Hold a safe configuration until ground
or OBC explicitly commands an exit.

SAFE_MODE has four sub-states determined either by OBC command or, if the
heartbeat is lost, autonomously chosen by the FSM at entry:

| Sub-state | Loads enabled |
|---|---|
| `DETUMBLING` | OBC, ADCS, UHF |
| `CHARGING` | OBC, UHF |
| `COMMUNICATION` | OBC, ADCS, UHF |
| `REBOOT` | OBC, ADCS, UHF |

**Entry action:** Disable all loads not allowed by current sub-state. Open
panel eFuse if entered because of battery/temperature fault. Latch the
`safe_mode_alert_flag` for OBC.

**Internal behavior (per cycle):**

```
IF obc_safe_substate has changed:
    apply new load mask for that sub-state
```

**Exit conditions:**

```
IF obc_mode is non-SAFE AND safety check above did not trip this cycle:
    re-enable all loads
    → run dispatcher to choose initial mode
       (typically MPPT_CHARGE if SA_available, else BATTERY_DISCHARGE)
```

SAFE_MODE does not exit autonomously. It needs a clean OBC command and a
clean safety check.

---

## Verification layer (NOT part of the implementation)

A separate pure function used by tests and by an in-firmware sanity assert:

```
function dispatcher(inputs):
    IF battery_below_minimum OR temp_out_of_range OR heartbeat_timeout:
        return SAFE_MODE
    IF NOT SA_available:
        return BATTERY_DISCHARGE
    IF Vbat < Vbat_max − D:
        return MPPT_CHARGE
    IF battery_full:
        return SA_LOAD_FOLLOW
    return CV_FLOAT
```

Invariant we expect to hold at steady state: after inputs have been stable
for longer than `max(t1, t2)`, the actual FSM mode must equal
`dispatcher(inputs)`. If it ever doesn't, that is a bug in either the FSM
implementation or the dispatcher.

---

## Open issues to resolve before flight

These are the gaps and weaknesses in the spec above. Listed here so the next
person picking this up knows what to fix. None of them block the simulator
(which uses the pure dispatcher), but they all matter for flight.

### A. Placeholder threshold values — set when hardware is finalized

These exist in the doc with provisional numbers. The real values come from
the chosen battery cell datasheet, the chosen solar panel, the thermal
model, and the orbit profile. The battery and thermal teams own these.

- `Vbat_max`, `Vbat_full`, `Vbat_charge_resume`, `Vbat_min`, `Vbat_critical`,
  `D` — from battery cell datasheet
- `Ibat_max_charge`, `Ibat_max_discharge`, `Ibat_taper`,
  `Ibat_min_charge_clamp` — from battery datasheet and load budget
- `Vpanel_min_avail` — from panel selection
- `Temp_min`, `Temp_max` — from cell datasheet (operating temperature range)
- `t1`, `t2`, `heartbeat_timeout_count` — depend on the chosen control loop
  rate, which is not yet specified

### B. Logic gaps in the per-state spec

- **`battery_full` not used by CV_FLOAT.** We defined `battery_full` as
  `Vbat ≥ Vbat_full AND |Ibat| < Ibat_taper`. Both conditions become true
  naturally during CV_FLOAT (CV_FLOAT holds voltage and lets current taper).
  But CV_FLOAT has no exit triggered by `battery_full`, so a fully-charged
  battery in CV_FLOAT stays in CV_FLOAT instead of graduating to
  SA_LOAD_FOLLOW. **Fix:** add `IF battery_full → SA_LOAD_FOLLOW` to
  CV_FLOAT's exit list.
- **SAFE_MODE exit is sloppy.** Current text says "run dispatcher to choose
  initial mode" but we rejected the dispatcher as an implementation pattern.
  **Fix:** SAFE_MODE always exits to BATTERY_DISCHARGE; the normal arrows
  take over from there within one iteration.
- **`t1` semantics are vague.** `IF t1_elapsed AND charging_buffer_insufficient`
  uses an undefined predicate. **Fix:** redefine as
  `IF Vbat ≥ Vbat_full for t1 consecutive cycles → CV_FLOAT`. Drops the
  separate `charging_buffer_insufficient` predicate.

### C. Robustness flags

- **Safety check priority.** Three safety conditions checked with `ELSE IF`
  — only one fires per cycle. If both low battery and temperature are bad
  simultaneously, we report only the first. Consider reporting a bitmask
  of all triggered reasons instead.
- **TEMP_MPPT exit is hair-trigger.** Exit condition `Ibat ≥ 0` will bounce
  on noise. Replace with `Ibat ≥ small_positive_value` sustained for N
  cycles once a noise floor is known.
- **Sub-state behavior while in SAFE_MODE.** If we entered SAFE_MODE
  autonomously (heartbeat timeout) and the battery transitions from critical
  to non-critical, do we autonomously change sub-state from CHARGING to
  COMMUNICATION? Currently undefined. Either spec it or pick one and
  document it.
- **Autonomous SAFE_MODE exit on heartbeat return.** A returning heartbeat
  does not autonomously exit SAFE_MODE — only an OBC command does. This is
  the conservative choice. Confirm it is intentional.
- **BATTERY_DISCHARGE entry duty value.** "Force buck duty to minimum" —
  is minimum 0% (converter off) or 5% (idle but warm)? Old code uses 5%
  but spec is silent.
- **BATTERY_DISCHARGE load shedding rate.** Old code sheds one load per
  iteration. Spec doesn't constrain this. Conservative for now, worth
  revisiting with the load model in hand.

### D. Doc cleanup

- **`faults` input is unused.** Listed in glossary but no rule references
  it. Either define what bits drive what behavior, or drop from input list.
- **Inputs vs derived stateful predicates.** Glossary mixes raw sensor
  readings (`Vbat`, `Ibat`, `Tbat`) with stateful counters
  (`heartbeat_timeout`, `iterations_since_last_heartbeat`). Split into two
  subsections so a code reader doesn't confuse them.
