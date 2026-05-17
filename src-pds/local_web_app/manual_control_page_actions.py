"""Manual Control page actions.

Sends the small set of text commands that drive the firmware's manual control
mode through the shared serial bridge. Each public function mirrors one HTTP
endpoint exposed by run_local_web_app.py so the routing layer stays thin.

The firmware accepts the four set_manual_* commands only while it is in
PDS_REQUESTED_MODE_MANUAL. enter_manual_control_mode() is the one entry
point that switches into that mode (and forces every output back to a
known safe state on the firmware side).
"""

from __future__ import annotations

import time

from serial_bridge_shared_by_pages import BRIDGE, STATE


# How often the firmware should push status packets while the operator is on
# the manual page. 250 ms gives a snappy UI without flooding the UART.
STREAM_PERIOD_MS = 250


def enter_manual_control_mode() -> str:
    """Off-then-enter sequence the manual page runs when the operator
    confirms they want to take direct control. Mirrors the equivalent
    sequence on the MPPT and State pages so the user-visible behaviour is
    consistent across pages."""

    STATE.record_debug(
        "Entering manual control mode (operator drives PWM, switches, LED)"
    )

    BRIDGE.send("stream_values off")
    time.sleep(0.05)
    BRIDGE.send("off")
    time.sleep(0.05)
    BRIDGE.send("enter_manual")
    time.sleep(0.08)
    BRIDGE.send(f"stream_values on period={STREAM_PERIOD_MS} fields=all")
    time.sleep(0.05)
    BRIDGE.send("get_values fields=all")

    with STATE.lock:
        STATE.in_manual_mode = True

    return "enter_manual"


def send_set_manual_pwm(duty: int) -> str:
    duty_int = int(duty)
    if duty_int < 0 or duty_int > 65535:
        raise ValueError("duty must be between 0 and 65535")
    command = f"set_manual_pwm duty={duty_int}"
    BRIDGE.send(command)
    return command


def send_set_manual_pv(on: bool) -> str:
    command = f"set_manual_pv {'on' if on else 'off'}"
    BRIDGE.send(command)
    return command


def send_set_manual_bat(on: bool) -> str:
    command = f"set_manual_bat {'on' if on else 'off'}"
    BRIDGE.send(command)
    return command


def send_set_manual_led(on: bool) -> str:
    command = f"set_manual_led {'on' if on else 'off'}"
    BRIDGE.send(command)
    return command
