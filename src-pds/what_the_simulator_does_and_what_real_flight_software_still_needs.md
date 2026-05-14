# What the simulator does and what the real flight software still needs

This file is the project scope statement. It is written for someone who has
never seen this codebase before. It explains, in plain English, what was
built so far, why it was built that way, what it actually proves, and what
still needs to happen before any of this code can fly on a real satellite.

If anything in another document seems to disagree with this file, this file
is the high-level truth and the other document is detail.

---

## What this satellite has to do, in everyday words

The CHESS satellite has solar panels, a battery, and several pieces of
equipment that need electrical power (a flight computer, a radio, a camera,
and so on). A single small computer chip on the satellite is in charge of
deciding, every fraction of a second:

1. Whether to charge the battery from the solar panels right now.
2. Whether to power the equipment from the panels directly, from the
   battery, or from both.
3. Whether to leave the battery alone because it is already full.
4. Whether something is wrong (battery too low, too cold, too hot, no
   message from the main flight computer for too long) and the satellite
   should fall back to a protective mode.

This decision-making is a state machine: at any moment the chip is in one
of a small number of modes, and it changes modes based on the readings it
gets from sensors. The four normal modes are:

- **MPPT_CHARGE** — there is sun, the battery is not full, charge it as
  hard as possible. "MPPT" is the algorithm that finds the operating
  point on the solar panel that gives the most power.
- **CV_FLOAT** — the battery is near full but not totally topped off.
  Hold the voltage steady while the remaining charge trickles in.
- **SA_LOAD_FOLLOW** — the battery is fully charged. Run the panels just
  hard enough to cover the equipment that is currently turned on.
- **BATTERY_DISCHARGE** — no sun (the satellite is in eclipse). Power
  the equipment from the battery.

There is also a fifth mode, **SAFE_MODE**, which the chip enters when
something dangerous is happening (battery critically low, temperature out
of safe range, no contact from the main flight computer for too long).

---

## What we built right now

We built two things:

### 1. A simplified version of the state machine, running on real hardware

This lives in the folder `src-pds/state_machine_pure_logic/`. It is the
chip's decision-making code, written as one short function that reads the
sensor values and returns the new mode plus the orders for the chip's
outputs (which pieces of equipment to power, whether to turn on the
heater, and so on).

We chose to keep this function very simple on purpose. It does the
minimum needed to land in the correct mode for any given set of sensor
values. It does **not** yet do the additional protective tricks that the
real flight software will have to do (we will list these later).

### 2. A local web page that runs 12 preset scenarios

This lives in the folder `src-pds/local_web_app/`. You start it on a
laptop with a single Python command. It opens a web page in the browser.

The page has a list of 12 scenarios. Each scenario is a recipe: a set of
sensor readings that, when sent to the chip, should make the chip land in
a specific mode and produce specific outputs. You click "Run" on a
scenario. The page sends the sensor readings to the chip through a small
intermediary board (the ESP32, which translates plain text from the
laptop into the radio-style binary messages the chip expects). The page
then waits a second or two, reads back what the chip is now doing, and
compares it to what was expected.

If the chip behaved as expected, the scenario's status dot turns green.
If it did not, the dot turns red and the page shows a side-by-side table
of the expected vs the actually observed values.

The 12 scenarios were chosen to cover:

- The three normal modes that can be reached just by changing sensor
  values: MPPT_CHARGE, CV_FLOAT, SA_LOAD_FOLLOW, BATTERY_DISCHARGE.
  (CV_FLOAT we get to by sitting in a narrow voltage window.)
- The three reasons for entering SAFE_MODE: battery too low, temperature
  out of range, no message from the main flight computer for too long.
- The four different sets of equipment that should be powered while in
  SAFE_MODE (each set is called a "sub-state": CHARGING, COMMUNICATION,
  DETUMBLING, REBOOT).

---

## What the green dots actually prove

When all 12 scenarios pass, this is what you have shown:

- Given a set of sensor readings, the chip's decision-making function
  returns the mode that the mission specification (page 100 of the
  CHESS mission document, the figure labelled "PCU Operating modes")
  says it should return.
- The chip's output orders for that mode are correct: it asks for the
  right state of the solar panel switch (open or closed), the right
  state of the heater (on or off), and the right list of pieces of
  equipment to power.
- The whole path between the laptop, the intermediary board, and the
  chip is working end to end. Commands flow down, telemetry comes back,
  the page can parse it.

In short: the **steady-state behavior of the simplified decision-making
function matches the specification's overview diagram.**

---

## What the green dots do NOT prove

A lot. These are real things the flight software will have to do, none
of which the simulator currently exercises:

- **Timing-based transitions.** The specification says that to leave
  MPPT_CHARGE for CV_FLOAT, the chip should wait a while (a time the
  specification calls "t1") with the battery near full but not totally
  full. The simulator detects "totally full" instantaneously based on
  voltage and current together, and skips the wait. The flight version
  will need a real timer.
- **The internal sub-state of CV_FLOAT called TEMP_MPPT.** This kicks in
  when a sudden large equipment load briefly drains the battery while
  CV_FLOAT is active. The chip is supposed to temporarily switch the
  charging algorithm, then switch back once the current settles. The
  simulator does not do this.
- **The detailed control of the buck converter duty cycle inside each
  mode.** The "buck converter" is the part of the circuit that turns
  the solar panel voltage into the voltage the battery wants. Its
  control knob, called the "duty cycle", is what the MPPT and CV
  algorithms actually wiggle. In the simulator, we use a single fixed
  duty cycle value as a placeholder. The flight version will have to
  run real charging algorithms.
- **Hysteresis and noise robustness on the sensor inputs.** Real sensor
  readings jiggle around. A reading that briefly crosses a threshold
  should not cause the chip to flap between modes. The simulator
  receives clean injected numbers, so it never sees this problem. The
  flight version will need filters and dead-bands.
- **Per-iteration load shedding when discharging too hard.** During
  BATTERY_DISCHARGE, if the equipment draws too much current at once,
  the chip is supposed to turn equipment off one item at a time,
  starting with the least important. The simulator does not do this.
- **Fault flags.** The intermediary board can send a bitmask of injected
  fault flags. The simulator does not interpret any bit of it yet.
- **The exit rule from SAFE_MODE.** In the specification, once the
  satellite is in SAFE_MODE, it stays there until the main flight
  computer explicitly commands a different mode, even if the original
  danger is gone. The simulator does follow this rule (it remembers it
  is in SAFE_MODE), but the 12 scenarios do not exercise the exit path.

If a reviewer with experience in satellite electrical power systems sees
the page, these are the gaps they will ask about. The list above is the
honest answer.

---

## How the simulator differs from real flight code, at the function-name level

The simulator has one short pure function that, every cycle, reads sensor
values and returns the new mode plus the output orders. Real flight code
will have all the same inputs and outputs but with more logic between
them:

- Several **input-conditioning steps** in front of the decision function:
  low-pass filters on the noisy sensor channels, debouncing on threshold
  crossings, computation of derived booleans (whether the solar panel is
  "available", whether the battery is "fully charged", and so on) using
  hysteresis bands.
- A **per-mode behavior block** for each of the five modes. Inside each
  block: the duty-cycle algorithm for that mode, the entry action that
  runs once when the mode is first entered (for example, opening the
  solar panel switch when entering BATTERY_DISCHARGE), and any
  per-iteration internal book-keeping.
- A **timer layer**: counters that get incremented every cycle while a
  particular condition holds, so transitions can be gated on "this has
  been true continuously for N cycles" instead of just "this is true
  right now".
- An **expanded safety check** that not only spots danger but also
  records the reason, picks the right sub-state autonomously when the
  main computer is silent, and refuses to leave SAFE_MODE without an
  explicit command.

The simulator's single function will grow into the **decision-making
core** of this expanded architecture. The other layers are added around
it. The simulator's contract (same inputs, same outputs) stays the same.

---

## What the path from simulator to flight code looks like, step by step

Each step adds one piece of behavior. Each step can be tested by adding
a new scenario, or by adding a new kind of scenario, to the local web
page.

1. **Add a control-loop iteration counter** to the chip's state. Increment
   it every cycle. This is the foundation for every timing-based
   behavior.
2. **Add the t1 timeout** that moves MPPT_CHARGE to CV_FLOAT after a
   sustained "near full" voltage. Test with a scenario that holds the
   inputs steady and waits.
3. **Add the t2 wait** that debounces the return from CV_FLOAT to
   MPPT_CHARGE.
4. **Add the TEMP_MPPT sub-state** inside CV_FLOAT. Test with a scenario
   that briefly injects a large negative battery current.
5. **Replace the placeholder duty cycle with the real MPPT incremental
   conductance algorithm.** This already exists in the older code at
   `src/mppt_algorithm.c` and can be re-used.
6. **Replace the placeholder duty cycle with a real bang-bang regulator**
   for CV_FLOAT.
7. **Add the SA_LOAD_FOLLOW current-clamp duty algorithm.**
8. **Add per-iteration load shedding** to BATTERY_DISCHARGE.
9. **Add input low-pass filters and hysteresis** for every sensor
   channel. The current simulator uses raw injected values; flight code
   reads jittery ADCs ("ADC" is the chip's tool for turning an analog
   voltage from a sensor into a number it can use).
10. **Define and interpret the fault-flag bits.**
11. **Replace the placeholder threshold values** with real ones once the
    battery, the solar panel, and the orbit profile are finalized
    (see next section).

After all eleven items: the chip has the full per-mode flight FSM and
the simulator's page can verify every transition in isolation.

---

## Where the threshold numbers come from

The simulator uses placeholder numeric values for everything: the
battery-full voltage, the maximum charge current, the temperature limits,
the timeout durations, and so on. The full list lives in the file
`src/eps_configuration_parameters.h` (the struct named
`eps_configuration_thresholds`).

None of those numbers are real flight values. Each one will be replaced
when the matching piece of physical hardware is finalized:

- **Battery voltages and currents** come from the battery cell datasheet.
  The battery team is responsible for picking these and providing the
  numbers.
- **Temperature limits** come from the battery cell datasheet's safe
  operating temperature range, and from the thermal model of the
  satellite. The thermal team handles this.
- **Solar panel "available" voltage** comes from the chosen solar panel
  and from the buck converter's minimum input voltage. The power team
  handles this.
- **Timer durations** depend on how fast the chip's main loop runs and
  on the orbital mechanics (how long an eclipse lasts, how long a
  charging period lasts, how often the main flight computer is expected
  to send a heartbeat). The systems team handles this.

A reviewer looking at the simulator should understand that the goal of
the placeholders was to make the decision logic testable, not to set
flight values. Setting flight values is a separate task that happens
later, with real hardware data in hand.

---

## Files that go with this document

- `src-pds/state-transitions.md` — the long-form specification for the
  flight-grade state machine, with all transition rules and a list of
  the issues that the current simulator simplifies past.
- `src-pds/local_web_app/README.md` — how to run the laptop web page
  and the description of its routes.
- `src-pds/plan.md` — the project roadmap for the demo pages that will
  exist on top of the current state-transition and MPPT pages.
- `src-pds/state_machine_pure_logic/functions_to_compute_next_pcu_state_and_actuator_commands_from_pure_logic.c`
  — the actual short decision function that the simulator runs on the
  chip today.
- `src/eps_state_machine.c` — the older, more complex draft of the
  per-mode logic from before this rebuild. It is not currently used by
  the simulator, but it is the starting point for the per-mode duty
  algorithms that step 5, 6, and 7 above will pull in.

---

## Summary in one paragraph

We built a simplified version of the satellite's electrical power
decision-making code, plus a local web page that runs 12 preset
scenarios against the real chip and shows whether the chip lands in the
expected state for each scenario. The simplified code is enough to
prove that the high-level mode mapping (from sensor values to one of
five modes) matches the specification, and that the chip can drive its
outputs correctly for each mode. The simplified code is **not** yet
flight software: it lacks timer-based transitions, hysteresis, real
charging algorithms, load shedding, fault interpretation, and tuned
threshold values. The path from what we have today to flight software
is a clearly-listed sequence of eleven steps, each of which can be
verified by adding new scenarios to the same web page.
