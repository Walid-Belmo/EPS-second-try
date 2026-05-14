"""MPPT convergence page actions.

Pure-Python module: validates MPPT curve payloads, formats the
`start_mppt_demo` text command, and orchestrates the off/start/stream
sequence over the shared serial bridge. The HTTP routing layer in
run_local_web_app.py imports this module's public functions.
"""

from __future__ import annotations

import time
from typing import Any

from serial_bridge_shared_by_pages import (
    BRIDGE,
    STATE,
    send_off_command,
    _format_curve_for_log,
)


# -----------------------------------------------------------------------------
# Public — called from HTTP handler.
# -----------------------------------------------------------------------------


def send_start_mppt_command(payload: dict[str, Any]) -> str:
    """Validate the payload, format the command, run the start sequence.

    Returns the exact text command sent to the ESP32.
    """
    curve = _read_validated_mppt_curve_from_payload(payload)
    command = _build_start_mppt_command_from_curve(curve)

    with STATE.lock:
        STATE.telemetry = {}
        STATE.history.clear()
        STATE.last_history_loop = None
        STATE.active_curve = None
        STATE.requested_curve = dict(curve)
        STATE.mppt_running = True
        STATE.record_debug(
            "Start requested: " + _format_curve_for_log(curve)
        )

    try:
        BRIDGE.send("stream_values off")
        time.sleep(0.05)
        BRIDGE.send("off")
        time.sleep(0.05)
        BRIDGE.send(command)
        time.sleep(0.08)
        BRIDGE.send("get_values fields=all")
        time.sleep(0.08)
        BRIDGE.send("stream_values on period=1000 fields=mode,pwm,mppt")
        time.sleep(0.05)
        BRIDGE.send("get_values fields=mode,pwm,mppt")
        STATE.record_debug(
            "Start sequence sent; waiting for live board points"
        )
    except Exception as exc:
        with STATE.lock:
            STATE.mppt_running = False
            STATE.record_debug(f"Start failed: {exc}")
        raise

    return command


def build_start_mppt_command(payload: dict[str, Any]) -> str:
    """Format the command without sending it. Useful for previews."""
    return _build_start_mppt_command_from_curve(
        _read_validated_mppt_curve_from_payload(payload)
    )


# -----------------------------------------------------------------------------
# Private helpers — payload validation and command formatting.
# -----------------------------------------------------------------------------


def _read_validated_mppt_curve_from_payload(
    payload: dict[str, Any],
) -> dict[str, Any]:
    a = _read_float(payload, "a")
    b = _read_float(payload, "b")
    c = _read_int(payload, "c")
    v_min_mv = round(_read_float(payload, "v_min") * 1000.0)
    v_max_mv = round(_read_float(payload, "v_max") * 1000.0)
    battery_mv = round(_read_float(payload, "battery_voltage") * 1000.0)
    _validate_mppt_request(a, b, c, v_min_mv, v_max_mv, battery_mv)

    return {
        "a": a,
        "b": b,
        "c": c,
        "v_min": v_min_mv / 1000.0,
        "v_max": v_max_mv / 1000.0,
        "battery_voltage": battery_mv / 1000.0,
    }


def _build_start_mppt_command_from_curve(curve: dict[str, Any]) -> str:
    return (
        "start_mppt_demo curve=quadratic "
        f"a={curve['a']:g} b={curve['b']:g} c={curve['c']} "
        f"v_min={round(curve['v_min'] * 1000.0)} "
        f"v_max={round(curve['v_max'] * 1000.0)} "
        f"battery_voltage={round(curve['battery_voltage'] * 1000.0)}"
    )


def _validate_mppt_request(
    a: float,
    b: float,
    c: int,
    v_min_mv: int,
    v_max_mv: int,
    battery_mv: int,
) -> None:
    if not (-1000.0 <= a < 0.0):
        raise ValueError("A must be negative and finite (|A| <= 1000)")
    if not (-2000.0 <= b <= 5000.0):
        raise ValueError("B must be between -2000 and 5000 mA/V")
    if not (0 <= c <= 3000):
        raise ValueError("C must be between 0 and 3000 mA")
    if not (0 <= v_min_mv <= 8000):
        raise ValueError("Vmin must be between 0 and 8 V")
    if not (12000 <= v_max_mv <= 25000):
        raise ValueError("Vmax must be between 12 and 25 V")
    if v_min_mv >= v_max_mv:
        raise ValueError("Vmin must be lower than Vmax")
    if not (6500 <= battery_mv <= 8400):
        raise ValueError("Battery voltage must be between 6.5 and 8.4 V")

    curve = {
        "a": a,
        "b": b,
        "c": c,
        "v_min": v_min_mv / 1000.0,
        "v_max": v_max_mv / 1000.0,
        "battery_voltage": battery_mv / 1000.0,
    }
    starting_panel_voltage = curve["battery_voltage"] / 0.5
    starting_current = (
        curve["a"] * starting_panel_voltage * starting_panel_voltage
        + curve["b"] * starting_panel_voltage
        + curve["c"]
    )
    if starting_current <= 100.0:
        raise ValueError(
            "Curve gives almost no current at the MPPT starting voltage"
        )


def _read_float(payload: dict[str, Any], key: str) -> float:
    try:
        return float(payload[key])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"{key} must be a number") from exc


def _read_int(payload: dict[str, Any], key: str) -> int:
    try:
        return int(round(float(payload[key])))
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"{key} must be an integer") from exc


__all__ = [
    "build_start_mppt_command",
    "send_off_command",  # re-exported for convenience
    "send_start_mppt_command",
]
