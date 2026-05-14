"""Shared serial bridge and page state for the unified local web app.

Owns the single ESP32 USB serial connection. Both the MPPT page and the
State Transition page (and the future Manual PWM and Communication pages)
read from and write to one instance of this module so that the COM port is
held by exactly one process for the lifetime of the server.

Public surface:
    STATE              - the singleton PageState
    BRIDGE             - the singleton SourcePdsSerialBridge
    connect_to_serial_port(port_name)
    send_off_command()
    send_get_values()
"""

from __future__ import annotations

import re
import threading
import time
from collections import deque
from typing import Any

try:
    import serial
except ImportError as exc:
    serial = None
    SERIAL_IMPORT_ERROR = str(exc)
else:
    SERIAL_IMPORT_ERROR = ""


DEFAULT_SERIAL_PORT = "COM3"
BAUD_RATE = 115200
ESP32_RESET_SETTLE_SECONDS = 1.2


# -----------------------------------------------------------------------------
# Page state — RAM owned by this Python process, not by the SAMD21 board.
# A single instance is shared by all pages.
# -----------------------------------------------------------------------------


class PageState:
    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.serial_port = DEFAULT_SERIAL_PORT
        self.connected = False
        self.connection_error = SERIAL_IMPORT_ERROR

        # Free-form telemetry dict. Both pages' parsers write whichever fields
        # they recognize from each [BOARD] line.
        self.telemetry: dict[str, Any] = {}

        # MPPT-specific extras (curve points, history) — kept here so the
        # MPPT page state behaves identically to its old standalone server.
        self.active_curve: dict[str, Any] | None = None
        self.requested_curve: dict[str, Any] | None = None
        self.mppt_running = False
        self.history: deque[dict[str, Any]] = deque(maxlen=600)
        self.last_history_loop: int | None = None

        # State-page extras: scenario pass/fail bookkeeping.
        self.in_state_test_mode = False
        self.scenario_results: dict[str, dict[str, Any]] = {}
        self.last_scenario_run: dict[str, Any] | None = None

        # Shared command/log buffers.
        self.last_command = ""
        self.last_ack = ""
        self.commands: deque[dict[str, Any]] = deque(maxlen=200)
        self.lines: deque[str] = deque(maxlen=500)
        self.debug_events: deque[dict[str, Any]] = deque(maxlen=120)

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {
                "serial_port": self.serial_port,
                "connected": self.connected,
                "connection_error": self.connection_error,
                "telemetry": dict(self.telemetry),
                "active_curve": (
                    dict(self.active_curve) if self.active_curve else None
                ),
                "requested_curve": (
                    dict(self.requested_curve)
                    if self.requested_curve
                    else None
                ),
                "mppt_running": self.mppt_running,
                "history": list(self.history),
                "in_state_test_mode": self.in_state_test_mode,
                "scenario_results": dict(self.scenario_results),
                "last_scenario_run": (
                    dict(self.last_scenario_run)
                    if self.last_scenario_run
                    else None
                ),
                "last_command": self.last_command,
                "last_ack": self.last_ack,
                "commands": list(self.commands),
                "lines": list(self.lines),
                "debug_events": list(self.debug_events),
            }

    def record_command(self, direction: str, text: str) -> None:
        with self.lock:
            if direction.startswith("PC"):
                self.last_command = text
            self.commands.append(
                {"time": time.time(), "direction": direction, "text": text}
            )

    def record_line_from_esp32(self, line: str) -> None:
        with self.lock:
            self.lines.append(line)
            self.commands.append(
                {"time": time.time(), "direction": "ESP32 -> PC", "text": line}
            )
            parse_esp32_line_into_state(line, self)

    def record_debug(self, text: str) -> None:
        with self.lock:
            self.debug_events.append({"time": time.time(), "text": text})


STATE = PageState()


# -----------------------------------------------------------------------------
# Serial bridge — opens COM port once, reads lines into PageState in a thread.
# -----------------------------------------------------------------------------


class SourcePdsSerialBridge:
    def __init__(self, state: PageState) -> None:
        self.state = state
        self.lock = threading.RLock()
        self.port: Any = None
        self.reader_thread: threading.Thread | None = None
        self.stop_reader = threading.Event()

    def open(self, port_name: str) -> None:
        if serial is None:
            raise RuntimeError(
                "pyserial is not installed: " + SERIAL_IMPORT_ERROR
            )

        with self.lock:
            if self.port is not None and self.port.is_open:
                return

            self.port = serial.Serial(
                port=port_name,
                baudrate=BAUD_RATE,
                timeout=0.1,
                write_timeout=1.0,
                dsrdtr=False,
                rtscts=False,
            )
            self.port.dtr = False
            self.port.rts = False
            self.stop_reader.clear()
            self.reader_thread = threading.Thread(
                target=self._read_loop,
                name="source-pds-local-web-app-serial-reader",
                daemon=True,
            )
            self.reader_thread.start()
            self.state.record_debug(
                "Serial opened; waiting for ESP32 bridge reset"
            )
            time.sleep(ESP32_RESET_SETTLE_SECONDS)

            with self.state.lock:
                self.state.serial_port = port_name
                self.state.connected = True
                self.state.connection_error = ""

    def close(self) -> None:
        with self.lock:
            self.stop_reader.set()
            if self.port is not None:
                try:
                    if self.port.is_open:
                        self.port.close()
                finally:
                    self.port = None
            with self.state.lock:
                self.state.connected = False

    def send(self, command: str) -> None:
        command = command.strip()
        if not command:
            return

        with self.lock:
            if self.port is None or not self.port.is_open:
                self.open(self.state.serial_port)
            self.port.write((command + "\n").encode("utf-8"))
            self.port.flush()
            self.state.record_command("PC -> ESP32", command)

    def _read_loop(self) -> None:
        while not self.stop_reader.is_set():
            try:
                raw = self.port.readline() if self.port is not None else b""
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    self.state.record_line_from_esp32(line)
            except Exception as exc:
                with self.state.lock:
                    self.state.connected = False
                    self.state.connection_error = str(exc)
                return


BRIDGE = SourcePdsSerialBridge(STATE)


# -----------------------------------------------------------------------------
# Parse the line formats emitted by the ESP32 bridge.
# -----------------------------------------------------------------------------


PCU_MODE_NAMES = {
    0: "MPPT_CHARGE",
    1: "CV_FLOAT",
    2: "SA_LOAD_FOLLOW",
    3: "BATTERY_DISCHARGE",
}


def parse_esp32_line_into_state(line: str, state: PageState) -> None:
    if line.startswith("status=") or line.startswith("[BOARD] status="):
        state.last_ack = line
        # Fall through — the values reply also starts with "status=...".

    match = re.search(
        r"mode=(\w+) stream=(\d+) period=(\d+) stream_fields=0x([0-9A-Fa-f]+)",
        line,
    )
    if match:
        state.telemetry.update(
            {
                "requested_mode": match.group(1),
                "stream_enabled": int(match.group(2)),
                "stream_period_ms": int(match.group(3)),
                "stream_fields": int(match.group(4), 16),
            }
        )
        return

    match = re.search(
        r"pwm:?\s+fixed=(\d+) requested=(\d+) applied=(\d+) enabled=(\d+)",
        line,
    )
    if match:
        state.telemetry.update(
            {
                "fixed_pwm": int(match.group(1)),
                "requested_pwm": int(match.group(2)),
                "applied_pwm": int(match.group(3)),
                "pwm_enabled": int(match.group(4)),
            }
        )
        return

    match = re.search(
        r"\[ESP32\]\s+mppt_model\s+curve=quadratic\s+"
        r"a_scaled=(-?\d+) b_scaled=(-?\d+) c=(-?\d+) "
        r"v_range=(\d+)\.\.(\d+) battery=(\d+)",
        line,
    )
    if match:
        curve = _curve_from_scaled_values(match, first_scaled_group=1)
        state.telemetry.update(
            {
                "curve_source": "esp32_model",
                "curve_type": "quadratic",
                "a": curve["a"],
                "b": curve["b"],
                "c": curve["c"],
                "v_min_mv": round(curve["v_min"] * 1000.0),
                "v_max_mv": round(curve["v_max"] * 1000.0),
                "battery_mv": round(curve["battery_voltage"] * 1000.0),
            }
        )
        if state.active_curve is None or not _curves_match(
            state.active_curve, curve
        ):
            state.record_debug(
                "ESP32 model confirmed: " + _format_curve_for_log(curve)
            )
        state.active_curve = dict(curve)
        return

    match = re.search(
        r"mppt(?:_curve)?:?\s+curve=(\d+) a_scaled=(-?\d+) b_scaled=(-?\d+) c=(-?\d+) "
        r"v_range=(\d+)\.\.(\d+) battery=(\d+)",
        line,
    )
    if match:
        curve = _curve_from_scaled_values(match, first_scaled_group=2)
        state.telemetry.update(
            {
                "curve_source": "board_status",
                "curve_type": int(match.group(1)),
                "a": curve["a"],
                "b": curve["b"],
                "c": curve["c"],
                "v_min_mv": round(curve["v_min"] * 1000.0),
                "v_max_mv": round(curve["v_max"] * 1000.0),
                "battery_mv": round(curve["battery_voltage"] * 1000.0),
            }
        )
        if state.active_curve is None or not _curves_match(
            state.active_curve, curve
        ):
            state.record_debug(
                "Board curve confirmed: " + _format_curve_for_log(curve)
            )
        state.active_curve = dict(curve)
        return

    match = re.search(
        r"mppt_live loops=(\d+) panel=(\d+)mV (\d+)mA power=(\d+)mW duty=(\d+)",
        line,
    )
    if match:
        _update_mppt_live_telemetry(match, state)
        return

    if re.search(r"mppt_live\s+valid=0", line):
        state.telemetry.update({"mppt_sample_valid": 0})
        for key in (
            "panel_voltage_mv",
            "panel_current_ma",
            "panel_power_mw",
        ):
            state.telemetry.pop(key, None)
        return

    match = re.search(r"state loops=(\d+) pcu=(\w+) state_duty=(\d+)", line)
    if match:
        state.telemetry.update(
            {
                "loop_count": int(match.group(1)),
                "pcu_mode": match.group(2),
                "state_duty": int(match.group(3)),
            }
        )
        return

    match = re.search(r"faults safe=(\d+) reason=(\d+) safe_alert=(\d+)", line)
    if match:
        state.telemetry.update(
            {
                "safe_active": int(match.group(1)),
                "safe_reason": int(match.group(2)),
                "safe_alert": int(match.group(3)),
            }
        )
        return

    match = re.search(
        r"outputs panel_efuse=(\d+) heater=(\d+) loads=0x([0-9A-Fa-f]+)",
        line,
    )
    if match:
        state.telemetry.update(
            {
                "panel_efuse": int(match.group(1)),
                "heater": int(match.group(2)),
                "load_mask": int(match.group(3), 16),
            }
        )
        return

    match = re.search(r"status=(\w+) version=(\d+) timestamp_ms=(\d+)", line)
    if match:
        state.telemetry["timestamp_ms"] = int(match.group(3))


def _update_mppt_live_telemetry(match: re.Match[str], state: PageState) -> None:
    loop_count = int(match.group(1))
    point = {
        "time": time.time(),
        "loop_count": loop_count,
        "panel_voltage_mv": int(match.group(2)),
        "panel_current_ma": int(match.group(3)),
        "panel_power_mw": int(match.group(4)),
        "mppt_duty": int(match.group(5)),
        "mppt_sample_valid": 1,
    }
    state.telemetry.update(point)
    if state.last_history_loop != loop_count:
        state.history.append(point)
        state.last_history_loop = loop_count


def _curve_from_scaled_values(
    match: re.Match[str],
    *,
    first_scaled_group: int,
) -> dict[str, Any]:
    return {
        "a": int(match.group(first_scaled_group)) / 1000.0,
        "b": int(match.group(first_scaled_group + 1)) / 1000.0,
        "c": int(match.group(first_scaled_group + 2)),
        "v_min": int(match.group(first_scaled_group + 3)) / 1000.0,
        "v_max": int(match.group(first_scaled_group + 4)) / 1000.0,
        "battery_voltage": int(match.group(first_scaled_group + 5)) / 1000.0,
    }


def _curves_match(first: dict[str, Any], second: dict[str, Any]) -> bool:
    return (
        abs(float(first.get("a", 0.0)) - float(second.get("a", 0.0))) < 0.02
        and abs(float(first.get("b", 0.0)) - float(second.get("b", 0.0))) < 0.02
        and int(round(float(first.get("c", 0))))
        == int(round(float(second.get("c", 0))))
        and abs(float(first.get("v_min", 0.0)) - float(second.get("v_min", 0.0)))
        < 0.002
        and abs(float(first.get("v_max", 0.0)) - float(second.get("v_max", 0.0)))
        < 0.002
        and abs(
            float(first.get("battery_voltage", 0.0))
            - float(second.get("battery_voltage", 0.0))
        )
        < 0.002
    )


def _format_curve_for_log(curve: dict[str, Any]) -> str:
    return (
        f"a={float(curve['a']):g} b={float(curve['b']):g} "
        f"c={int(curve['c'])} "
        f"v={float(curve['v_min']):g}..{float(curve['v_max']):g}V "
        f"battery={float(curve['battery_voltage']):g}V"
    )


# -----------------------------------------------------------------------------
# Shared high-level commands.
# -----------------------------------------------------------------------------


def connect_to_serial_port(port_name: str) -> None:
    try:
        BRIDGE.open(port_name)
        BRIDGE.send("get_values fields=all")
    except Exception as exc:
        with STATE.lock:
            STATE.connected = False
            STATE.connection_error = str(exc)
        raise


def send_off_command() -> None:
    STATE.record_debug("Off requested: stopping stream and switching board off")
    BRIDGE.send("stream_values off")
    time.sleep(0.05)
    BRIDGE.send("off")
    time.sleep(0.05)
    BRIDGE.send("get_values fields=all")
    with STATE.lock:
        STATE.mppt_running = False
        STATE.in_state_test_mode = False
        STATE.active_curve = None
        STATE.requested_curve = None
        STATE.telemetry = {}
        STATE.history.clear()
        STATE.last_history_loop = None
        STATE.record_debug("Off sequence sent")


def send_get_values() -> None:
    BRIDGE.send("get_values fields=all")
