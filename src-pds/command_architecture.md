# Command Architecture Draft

This document defines the first draft of the command path.
It is intentionally not final.

There are two command layers:

1. Computer -> ESP32
2. ESP32 -> main board firmware

The computer commands are human-readable text commands.
The ESP32 translates those text commands into CHIPS packets.
The main board firmware only receives CHIPS packets.

## Computer -> ESP32 Commands

These are the commands typed by a human or sent by the local UI.

```text
off

run_pwm duty=32768

start_mppt_demo curve=quadratic a=-0.000012 b=0.03 c=2500 v_min=0 v_max=18000 battery_voltage=7400

start_state_demo battery_voltage=7400 battery_current=0 panel_voltage=12000 panel_current=1000 rail_voltage=7400 temperature=220 heartbeat=1 obc_mode=charging safe_substate=charging faults=0

inject_state battery_voltage=7200 battery_current=-300 panel_voltage=0 panel_current=0 rail_voltage=7200 temperature=220 heartbeat=1 obc_mode=charging safe_substate=charging faults=0

get_values fields=mode,pwm,state,loads,faults

stream_values on period=200 fields=mode,pwm,state,loads,faults

stream_values off
```

## ESP32 -> Main Board Firmware Commands

These are CHIPS commands sent by the ESP32 to the board.
They should carry the same meaning as the human-readable commands above.

```text
BOARD_OFF

BOARD_RUN_PWM
payload: duty

BOARD_START_MPPT_DEMO
payload: curve_type, a, b, c, v_min, v_max, battery_voltage

BOARD_START_STATE_DEMO
payload: battery_voltage, battery_current, panel_voltage, panel_current,
         rail_voltage, temperature, heartbeat, obc_mode, safe_substate, faults

BOARD_INJECT_STATE_INPUTS
payload: battery_voltage, battery_current, panel_voltage, panel_current,
         rail_voltage, temperature, heartbeat, obc_mode, safe_substate, faults

BOARD_GET_VALUES
payload: field list

BOARD_STREAM_VALUES
payload: enabled, period_ms, field list
```

## Current Rule

Every command should be complete by itself.

The one exception in this draft is `inject_state`.
State-transition demonstrations require values to change over time, so
`inject_state` only makes sense after `start_state_demo`.

## State Machine Demo Inputs

The state machine demo should inject the values that actually drive the EPS
state transitions.

The injectable values are:

```text
battery_voltage
battery_current
panel_voltage
panel_current
rail_voltage
temperature
heartbeat
obc_mode
safe_substate
faults
```

These are sensor-like values or OBC-like values.
They are the values the real firmware logic already reads.

## Derived Values

Some diagrams use intermediate values such as `SA_available`.

`SA_available` should not be injected directly in the first version.

In the current code, it is computed like this:

```text
SA_available = panel_voltage >= solar_available_threshold
```

So to make `SA_available = false`, inject a low `panel_voltage`.

## What Drives PCU Mode Transitions

`MPPT_CHARGE` changes when:

```text
panel_voltage too low -> BATTERY_DISCHARGE
battery_voltage >= battery_full_threshold -> SA_LOAD_FOLLOW
MPPT timeout reached -> CV_FLOAT
```

`CV_FLOAT` changes when:

```text
panel_voltage too low -> BATTERY_DISCHARGE
battery_current very negative -> temporary MPPT inside CV_FLOAT
battery_voltage < charge_resume_threshold for long enough -> MPPT_CHARGE
rail_voltage controls whether duty goes up or down
```

`SA_LOAD_FOLLOW` changes when:

```text
panel_voltage too low -> BATTERY_DISCHARGE
battery_voltage < battery_full_threshold -> MPPT_CHARGE
battery_current too positive -> reduce duty to avoid overcharging
```

`BATTERY_DISCHARGE` changes when:

```text
panel_voltage becomes high enough -> MPPT_CHARGE
battery_voltage too low -> safe alert
battery_current too negative -> shed loads
```

## Safe Mode Triggers

Safe mode checks are independent of the current PCU mode.

Safe mode can be triggered by:

```text
battery_voltage < minimum_battery_voltage
temperature < minimum_temperature
temperature > maximum_temperature
heartbeat = 0 for too long
```

`safe_substate` controls which loads stay on while safe mode is active.

## State Demo Rule

For the state demo, inject real input values.

Do not inject vague labels such as:

```text
solar=0
SA_available=false
```

Instead inject explicit values:

```text
panel_voltage=0
panel_current=0
```

Then the board computes the derived state from those values.
