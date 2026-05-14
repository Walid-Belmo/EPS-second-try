#!/usr/bin/env python3
"""Serve the local MPPT convergence page and talk to the ESP32 bridge.

Run from this folder or from the repository root:

    python src-pds/local_web_app_to_demo_mppt_convergence/run_mppt_convergence_page.py

Then open:

    http://127.0.0.1:8001
"""

from __future__ import annotations

import argparse
import atexit
import json
import mimetypes
import re
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

try:
    import serial
except ImportError as exc:
    serial = None
    SERIAL_IMPORT_ERROR = str(exc)
else:
    SERIAL_IMPORT_ERROR = ""


APP_DIR = Path(__file__).resolve().parent
STATIC_DIR = APP_DIR / "static"
DEFAULT_SERIAL_PORT = "COM3"
DEFAULT_SERVER_HOST = "127.0.0.1"
DEFAULT_SERVER_PORT = 8001
BAUD_RATE = 115200
ESP32_RESET_SETTLE_SECONDS = 1.2


def requested_mode_name(mode: str) -> str:
    return {
        "off": "off",
        "flight": "flight",
        "mppt_test": "mppt_test",
        "state_test": "state_test",
        "fixed_pwm_test": "fixed_pwm_test",
    }.get(mode, mode)


class PageState:
    """RAM owned by the Python server, not by the SAMD21 board."""

    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.serial_port = DEFAULT_SERIAL_PORT
        self.connected = False
        self.connection_error = SERIAL_IMPORT_ERROR
        self.mppt_running = False
        self.last_ack = ""
        self.last_command = ""
        self.telemetry: dict[str, Any] = {}
        self.active_curve: dict[str, Any] | None = None
        self.requested_curve: dict[str, Any] | None = None
        self.commands: deque[dict[str, Any]] = deque(maxlen=120)
        self.lines: deque[str] = deque(maxlen=250)
        self.debug_events: deque[dict[str, Any]] = deque(maxlen=120)
        self.history: deque[dict[str, Any]] = deque(maxlen=600)
        self.last_history_loop: int | None = None

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {
                "serial_port": self.serial_port,
                "connected": self.connected,
                "connection_error": self.connection_error,
                "mppt_running": self.mppt_running,
                "last_ack": self.last_ack,
                "last_command": self.last_command,
                "telemetry": dict(self.telemetry),
                "active_curve": dict(self.active_curve) if self.active_curve else None,
                "requested_curve": (
                    dict(self.requested_curve) if self.requested_curve else None
                ),
                "commands": list(self.commands),
                "lines": list(self.lines),
                "debug_events": list(self.debug_events),
                "history": list(self.history),
            }

    def record_command(self, direction: str, text: str) -> None:
        with self.lock:
            self.last_command = text if direction.startswith("PC") else self.last_command
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


class SourcePdsSerialBridge:
    """Serial link from this computer to the ESP32 USB port."""

    def __init__(self, state: PageState) -> None:
        self.state = state
        self.lock = threading.RLock()
        self.port: Any = None
        self.reader_thread: threading.Thread | None = None
        self.stop_reader = threading.Event()

    def open(self, port_name: str) -> None:
        if serial is None:
            raise RuntimeError("pyserial is not installed: " + SERIAL_IMPORT_ERROR)

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
                name="source-pds-mppt-page-serial-reader",
                daemon=True,
            )
            self.reader_thread.start()
            self.state.record_debug("Serial opened; waiting for ESP32 bridge reset")
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


def curves_match(first: dict[str, Any], second: dict[str, Any]) -> bool:
    return (
        abs(float(first.get("a", 0.0)) - float(second.get("a", 0.0))) < 0.02
        and abs(float(first.get("b", 0.0)) - float(second.get("b", 0.0))) < 0.02
        and int(round(float(first.get("c", 0)))) == int(round(float(second.get("c", 0))))
        and abs(float(first.get("v_min", 0.0)) - float(second.get("v_min", 0.0))) < 0.002
        and abs(float(first.get("v_max", 0.0)) - float(second.get("v_max", 0.0))) < 0.002
        and abs(
            float(first.get("battery_voltage", 0.0))
            - float(second.get("battery_voltage", 0.0))
        )
        < 0.002
    )


def format_curve_for_log(curve: dict[str, Any]) -> str:
    return (
        f"a={float(curve['a']):g} b={float(curve['b']):g} c={int(curve['c'])} "
        f"v={float(curve['v_min']):g}..{float(curve['v_max']):g}V "
        f"battery={float(curve['battery_voltage']):g}V"
    )


def parse_esp32_line_into_state(line: str, state: PageState) -> None:
    if line.startswith("status=") or line.startswith("[BOARD] status="):
        state.last_ack = line
        return

    match = re.search(
        r"mode=(\w+) stream=(\d+) period=(\d+) stream_fields=0x([0-9A-Fa-f]+)",
        line,
    )
    if match:
        state.telemetry.update(
            {
                "mode": requested_mode_name(match.group(1)),
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
        curve = curve_from_scaled_values(match, first_scaled_group=1)
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
        if state.active_curve is None or not curves_match(state.active_curve, curve):
            state.record_debug(f"ESP32 model confirmed: {format_curve_for_log(curve)}")
        state.active_curve = dict(curve)
        return

    match = re.search(
        r"mppt(?:_curve)?:?\s+curve=(\d+) a_scaled=(-?\d+) b_scaled=(-?\d+) c=(-?\d+) "
        r"v_range=(\d+)\.\.(\d+) battery=(\d+)",
        line,
    )
    if match:
        curve = curve_from_scaled_values(match, first_scaled_group=2)
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
        if state.active_curve is None or not curves_match(state.active_curve, curve):
            state.record_debug(f"Board curve confirmed: {format_curve_for_log(curve)}")
        state.active_curve = dict(curve)
        return

    match = re.search(
        r"mppt_live loops=(\d+) panel=(\d+)mV (\d+)mA power=(\d+)mW duty=(\d+)",
        line,
    )
    if match:
        update_mppt_live_telemetry(match, state)
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

    match = re.search(
        r"snapshot: loops=(\d+) panel=(\d+)mV (\d+)mA power=(\d+)mW "
        r"mppt_duty=(\d+) state_duty=(\d+) pcu=(\w+)",
        line,
    )
    if match:
        update_snapshot_telemetry(match, state)
        return

    match = re.search(
        r"safety: safe=(\d+) reason=(\d+) panel_efuse=(\d+) heater=(\d+) "
        r"safe_alert=(\d+) loads=0x([0-9A-Fa-f]+)",
        line,
    )
    if match:
        state.telemetry.update(
            {
                "safe_active": int(match.group(1)),
                "safe_reason": int(match.group(2)),
                "panel_efuse": int(match.group(3)),
                "heater": int(match.group(4)),
                "safe_alert": int(match.group(5)),
                "load_mask": int(match.group(6), 16),
            }
        )


def update_mppt_live_telemetry(match: re.Match[str], state: PageState) -> None:
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


def update_snapshot_telemetry(match: re.Match[str], state: PageState) -> None:
    loop_count = int(match.group(1))
    point = {
        "time": time.time(),
        "loop_count": loop_count,
        "panel_voltage_mv": int(match.group(2)),
        "panel_current_ma": int(match.group(3)),
        "panel_power_mw": int(match.group(4)),
        "mppt_duty": int(match.group(5)),
        "state_duty": int(match.group(6)),
        "pcu_mode": match.group(7),
        "mppt_sample_valid": 1,
    }

    state.telemetry.update(point)
    if state.last_history_loop != loop_count:
        state.history.append(point)
        state.last_history_loop = loop_count


def curve_from_scaled_values(
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
    time.sleep(0.03)
    BRIDGE.send("off")
    time.sleep(0.03)
    BRIDGE.send("get_values fields=all")
    with STATE.lock:
        STATE.mppt_running = False
        STATE.active_curve = None
        STATE.requested_curve = None
        STATE.telemetry = {}
        STATE.history.clear()
        STATE.last_history_loop = None
        STATE.record_debug("Off sequence sent")


def send_start_mppt_command(payload: dict[str, Any]) -> str:
    curve = read_validated_mppt_curve_from_payload(payload)
    command = build_start_mppt_command_from_curve(curve)
    with STATE.lock:
        STATE.telemetry = {}
        STATE.history.clear()
        STATE.last_history_loop = None
        STATE.active_curve = None
        STATE.requested_curve = dict(curve)
        STATE.mppt_running = True
        STATE.record_debug(f"Start requested: {format_curve_for_log(curve)}")

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
        STATE.record_debug("Start sequence sent; waiting for live board points")
    except Exception as exc:
        with STATE.lock:
            STATE.mppt_running = False
            STATE.record_debug(f"Start failed: {exc}")
        raise
    return command


def build_start_mppt_command(payload: dict[str, Any]) -> str:
    return build_start_mppt_command_from_curve(
        read_validated_mppt_curve_from_payload(payload)
    )


def read_validated_mppt_curve_from_payload(payload: dict[str, Any]) -> dict[str, Any]:
    a = read_float(payload, "a")
    b = read_float(payload, "b")
    c = read_int(payload, "c")
    v_min_mv = round(read_float(payload, "v_min") * 1000.0)
    v_max_mv = round(read_float(payload, "v_max") * 1000.0)
    battery_mv = round(read_float(payload, "battery_voltage") * 1000.0)
    validate_mppt_request(a, b, c, v_min_mv, v_max_mv, battery_mv)

    return {
        "a": a,
        "b": b,
        "c": c,
        "v_min": v_min_mv / 1000.0,
        "v_max": v_max_mv / 1000.0,
        "battery_voltage": battery_mv / 1000.0,
    }


def build_start_mppt_command_from_curve(curve: dict[str, Any]) -> str:
    return (
        "start_mppt_demo curve=quadratic "
        f"a={curve['a']:g} b={curve['b']:g} c={curve['c']} "
        f"v_min={round(curve['v_min'] * 1000.0)} "
        f"v_max={round(curve['v_max'] * 1000.0)} "
        f"battery_voltage={round(curve['battery_voltage'] * 1000.0)}"
    )


def validate_mppt_request(
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


def read_float(payload: dict[str, Any], key: str) -> float:
    try:
        return float(payload[key])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"{key} must be a number") from exc


def read_int(payload: dict[str, Any], key: str) -> int:
    try:
        return int(round(float(payload[key])))
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"{key} must be an integer") from exc


class MpptPageHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path in ("/", "/mppt"):
            self.send_file(STATIC_DIR / "mppt_convergence.html")
        elif path == "/api/status":
            self.send_json(STATE.snapshot())
        elif path.startswith("/static/"):
            self.send_file(STATIC_DIR / path.removeprefix("/static/"))
        else:
            self.send_error(404, "Not found")

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        try:
            payload = self.read_json_body()
            result = self.handle_api_post(path, payload)
            self.send_json({"ok": True, **result})
        except Exception as exc:
            self.send_json({"ok": False, "error": str(exc)}, status=400)

    def handle_api_post(self, path: str, payload: dict[str, Any]) -> dict[str, Any]:
        if path == "/api/connect":
            port = str(payload.get("port", STATE.serial_port)).strip()
            connect_to_serial_port(port or DEFAULT_SERIAL_PORT)
            return {"status": "connected"}
        if path == "/api/off":
            send_off_command()
            return {"status": "off_command_sent"}
        if path == "/api/start_mppt":
            command = send_start_mppt_command(payload)
            return {"status": "mppt_started", "command": command}
        if path == "/api/get_values":
            BRIDGE.send("get_values fields=all")
            return {"status": "get_values_sent"}
        raise ValueError("Unknown API endpoint")

    def read_json_body(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length == 0:
            return {}
        body = self.rfile.read(length).decode("utf-8")
        return json.loads(body)

    def send_json(self, value: dict[str, Any], status: int = 200) -> None:
        body = json.dumps(value).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, path: Path) -> None:
        if not path.is_file() or STATIC_DIR not in path.resolve().parents:
            self.send_error(404, "Not found")
            return
        body = path.read_bytes()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format_text: str, *args: Any) -> None:
        return


def shutdown_safely() -> None:
    try:
        if STATE.connected:
            send_off_command()
            time.sleep(0.1)
    except Exception:
        pass
    BRIDGE.close()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=DEFAULT_SERVER_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_SERVER_PORT)
    parser.add_argument("--serial-port", default=DEFAULT_SERIAL_PORT)
    return parser.parse_args()


def main() -> None:
    args = parse_arguments()
    with STATE.lock:
        STATE.serial_port = args.serial_port

    atexit.register(shutdown_safely)
    server = ThreadingHTTPServer((args.host, args.port), MpptPageHandler)
    print(f"MPPT convergence page: http://{args.host}:{args.port}")
    print(f"ESP32 serial port default: {args.serial_port} at {BAUD_RATE} baud")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping MPPT convergence page.")
    finally:
        shutdown_safely()
        server.server_close()


if __name__ == "__main__":
    main()
