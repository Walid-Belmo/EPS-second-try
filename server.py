#!/usr/bin/env python3
"""Local dashboard for the EPS mainboard MPPT/state-machine demo.

Run from the repository root:

    python server.py

Then open:

    http://localhost:8000

The server talks to the ESP32 bridge on COM3 by default. Close Arduino Serial
Monitor, PuTTY, or any other program using COM3 before starting a demo.
"""

from __future__ import annotations

import atexit
import json
import math
import re
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import urlparse

try:
    import serial
except ImportError as exc:  # pragma: no cover - depends on local machine
    serial = None
    SERIAL_IMPORT_ERROR = str(exc)
else:
    SERIAL_IMPORT_ERROR = ""


DEFAULT_PORT = "COM3"
BAUD_RATE = 115200
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 8000

PANEL_ADC_TO_MILLIVOLTS = 5
PANEL_ADC_TO_MILLIAMPS = 2

SATELLITE_MODE_CHARGING = 1
SAFE_SUBSTATE_CHARGING = 1


FSM_SCENARIOS = [
    {
        "name": "charge_mppt_nominale",
        "source_name": "sun_charging_mppt",
        "purpose": "Soleil disponible et batterie pas pleine: la carte doit charger en MPPT.",
    },
    {
        "name": "batterie_pleine_suivi_charge",
        "source_name": "sun_full_battery_load_follow",
        "purpose": "Soleil disponible et batterie pleine: la carte doit passer en suivi de charge solaire.",
    },
    {
        "name": "absence_de_soleil_decharge",
        "source_name": "no_sun_battery_discharge",
        "purpose": "Tension panneau trop faible: la carte doit couper le panneau et arreter le PWM.",
    },
    {
        "name": "batterie_froide_chauffage",
        "source_name": "cold_battery_heater_and_temp_alert",
        "purpose": "Batterie froide: la carte doit demander le chauffage et signaler l'alerte temperature.",
    },
    {
        "name": "batterie_critique_mode_safe",
        "source_name": "critical_battery_safe_charging",
        "purpose": "Batterie sous le seuil minimum: la carte doit rester en charge safe avec charges reduites.",
    },
    {
        "name": "defaut_injecte_pwm_coupe",
        "source_name": "injected_fault_forces_pwm_off",
        "purpose": "Defaut injecte: la securite doit forcer le PWM applique a zero.",
    },
]


class DemoState:
    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.serial_port_name = DEFAULT_PORT
        self.connected = False
        self.connection_error = SERIAL_IMPORT_ERROR
        self.mppt_running = False
        self.fsm_running = False
        self.last_sent_command = ""
        self.command_history: deque[dict[str, Any]] = deque(maxlen=200)
        self.raw_lines: deque[str] = deque(maxlen=500)
        self.history: deque[dict[str, Any]] = deque(maxlen=500)
        self.telemetry: dict[str, Any] = {}
        self.simulation: dict[str, Any] = {
            "voc_mv": 18000,
            "isc_ma": 3000,
            "battery_mv": 7400,
            "irradiance": 1.0,
            "period_ms": 250,
            "temperature_decic": 220,
            "target_vmp_mv": int(18000 / math.sqrt(3)),
            "target_duty": int((7400 * 65535) / (18000 / math.sqrt(3))),
        }
        self.fsm: dict[str, Any] = {
            "running": False,
            "complete": False,
            "pass": 0,
            "fail": 0,
            "scenarios": [
                {
                    "name": item["name"],
                    "source_name": item["source_name"],
                    "purpose": item["purpose"],
                    "status": "attente",
                    "injected": "",
                    "result": "",
                }
                for item in FSM_SCENARIOS
            ],
        }

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {
                "connected": self.connected,
                "serial_port": self.serial_port_name,
                "connection_error": self.connection_error,
                "mppt_running": self.mppt_running,
                "fsm_running": self.fsm_running,
                "simulation": dict(self.simulation),
                "telemetry": dict(self.telemetry),
                "history": list(self.history),
                "commands": list(self.command_history),
                "lines": list(self.raw_lines)[-120:],
                "fsm": json.loads(json.dumps(self.fsm)),
            }


STATE = DemoState()


class SerialBridge:
    def __init__(self, state: DemoState) -> None:
        self.state = state
        self.lock = threading.RLock()
        self.port: Any = None
        self.reader_thread: threading.Thread | None = None
        self.reader_stop = threading.Event()

    def open(self, port_name: str = DEFAULT_PORT) -> None:
        if serial is None:
            raise RuntimeError("pyserial n'est pas installe: " + SERIAL_IMPORT_ERROR)

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
            self.reader_stop.clear()
            self.reader_thread = threading.Thread(
                target=self._read_loop,
                name="eps-dashboard-serial-reader",
                daemon=True,
            )
            self.reader_thread.start()

            with self.state.lock:
                self.state.serial_port_name = port_name
                self.state.connected = True
                self.state.connection_error = ""

    def close(self) -> None:
        with self.lock:
            self.reader_stop.set()
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
                self.open(self.state.serial_port_name)
            assert self.port is not None
            self.port.write((command + "\n").encode("utf-8"))
            self.port.flush()

        with self.state.lock:
            self.state.last_sent_command = command
            self.state.command_history.append(
                {"t": time.time(), "direction": "PC -> ESP32", "text": command}
            )

    def _read_loop(self) -> None:
        buffer = ""
        while not self.reader_stop.is_set():
            try:
                if self.port is None:
                    break
                chunk = self.port.read(256)
            except Exception as exc:  # pragma: no cover - hardware dependent
                with self.state.lock:
                    self.state.connected = False
                    self.state.connection_error = str(exc)
                break

            if not chunk:
                continue

            try:
                buffer += chunk.decode("utf-8", errors="replace")
            except Exception:
                buffer += repr(chunk)

            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                self._handle_line(line.strip("\r"))

    def _handle_line(self, line: str) -> None:
        if not line:
            return
        with self.state.lock:
            self.state.raw_lines.append(line)
            if line.startswith("[TX]"):
                self.state.command_history.append(
                    {"t": time.time(), "direction": "ESP32 -> SAMD21", "text": line}
                )
            elif line.startswith("[RX]"):
                self.state.command_history.append(
                    {"t": time.time(), "direction": "SAMD21 -> ESP32", "text": line}
                )

        parse_telemetry_line(line)
        parse_fsm_line(line)


BRIDGE = SerialBridge(STATE)


def parse_telemetry_line(line: str) -> None:
    with STATE.lock:
        telemetry = STATE.telemetry

        match = re.search(
            r"version=(\d+) uptime_ms=(\d+) status=([A-Z_]+)", line
        )
        if match:
            telemetry.update(
                {
                    "version": int(match.group(1)),
                    "uptime_ms": int(match.group(2)),
                    "status": match.group(3),
                }
            )

        match = re.search(
            r"source=(\w+) control=(\w+) sat=(\d+) safe=(\d+).*?"
            r"applied_duty=(\d+) pwm=(\d+).*?armed=(\d+)",
            line,
        )
        if match:
            telemetry.update(
                {
                    "source": match.group(1),
                    "control": match.group(2),
                    "satellite_mode": int(match.group(3)),
                    "safe_substate": int(match.group(4)),
                    "applied_duty": int(match.group(5)),
                    "pwm": int(match.group(6)),
                    "pwm_armed": int(match.group(7)),
                }
            )

        match = re.search(
            r"injected pv_adc=(\d+) pi_adc=(\d+) batt_mv=(\d+) batt_ma=(-?\d+)",
            line,
        )
        if match:
            telemetry.update(
                {
                    "panel_voltage_adc": int(match.group(1)),
                    "panel_current_adc": int(match.group(2)),
                    "battery_mv": int(match.group(3)),
                    "battery_ma": int(match.group(4)),
                }
            )

        match = re.search(
            r"control iter=(\d+) panel_mv=(\d+) panel_ma=(\d+) input_mw=(\d+)",
            line,
        )
        if match:
            telemetry.update(
                {
                    "control_iterations": int(match.group(1)),
                    "panel_voltage_mv": int(match.group(2)),
                    "panel_current_ma": int(match.group(3)),
                    "input_power_mw": int(match.group(4)),
                }
            )
            append_history_point_locked()

        match = re.search(
            r"duty mppt=(\d+) fsm=(\d+) applied=(\d+) pwm=(\d+)",
            line,
        )
        if match:
            telemetry.update(
                {
                    "mppt_duty": int(match.group(1)),
                    "fsm_duty": int(match.group(2)),
                    "applied_duty": int(match.group(3)),
                    "pwm": int(match.group(4)),
                }
            )

        match = re.search(
            r"pcu=([A-Z_]+) safe_active=(\d+) safe_reason=(\d+) alert=(\d+)",
            line,
        )
        if match:
            telemetry.update(
                {
                    "pcu_mode": match.group(1),
                    "safe_active": int(match.group(2)),
                    "safe_reason": int(match.group(3)),
                    "safe_alert": int(match.group(4)),
                }
            )

        match = re.search(r"pwm_armed=(\d+)", line)
        if match:
            telemetry["pwm_armed"] = int(match.group(1))


def append_history_point_locked() -> None:
    telemetry = STATE.telemetry
    simulation = STATE.simulation
    if "panel_voltage_mv" not in telemetry:
        return
    point = {
        "t": time.time(),
        "panel_voltage_mv": telemetry.get("panel_voltage_mv", 0),
        "panel_current_ma": telemetry.get("panel_current_ma", 0),
        "input_power_mw": telemetry.get("input_power_mw", 0),
        "mppt_duty": telemetry.get("mppt_duty", 0),
        "applied_duty": telemetry.get("applied_duty", 0),
        "target_duty": simulation.get("target_duty", 0),
        "vmp_mv": simulation.get("target_vmp_mv", 0),
        "pmax_mw": compute_power_mw(
            simulation.get("target_vmp_mv", 0),
            compute_panel_current_ma(
                simulation.get("target_vmp_mv", 0),
                simulation.get("voc_mv", 18000),
                simulation.get("isc_ma", 3000),
                simulation.get("irradiance", 1.0),
            ),
        ),
    }
    STATE.history.append(point)


def parse_fsm_line(line: str) -> None:
    with STATE.lock:
        fsm = STATE.fsm
        match = re.search(r"SIM FSM: scenario (\d+)/(\d+) (\S+)", line)
        if match:
            idx = int(match.group(1)) - 1
            if 0 <= idx < len(fsm["scenarios"]):
                fsm["scenarios"][idx]["status"] = "en cours"
            return

        match = re.search(r"SIM FSM: inject (.*)", line)
        if match:
            idx = current_fsm_index_locked()
            if idx is not None:
                fsm["scenarios"][idx]["injected"] = match.group(1)
            return

        match = re.search(r"SIM FSM: result (.*)", line)
        if match:
            idx = current_fsm_index_locked()
            if idx is not None:
                fsm["scenarios"][idx]["result"] = match.group(1)
            return

        if line == "SIM FSM: PASS":
            idx = current_fsm_index_locked()
            if idx is not None:
                fsm["scenarios"][idx]["status"] = "PASS"
            return

        if line == "SIM FSM: FAIL":
            idx = current_fsm_index_locked()
            if idx is not None:
                fsm["scenarios"][idx]["status"] = "FAIL"
            return

        match = re.search(r"SIM FSM: complete pass=(\d+) fail=(\d+)", line)
        if match:
            fsm["running"] = False
            fsm["complete"] = True
            fsm["pass"] = int(match.group(1))
            fsm["fail"] = int(match.group(2))


def current_fsm_index_locked() -> int | None:
    scenarios = STATE.fsm["scenarios"]
    for idx in range(len(scenarios) - 1, -1, -1):
        if scenarios[idx]["status"] == "en cours":
            return idx
    for idx in range(len(scenarios) - 1, -1, -1):
        if scenarios[idx]["status"] in ("attente",):
            return idx
    return None


def compute_panel_current_ma(
    panel_voltage_mv: int, voc_mv: int, isc_ma: int, irradiance: float
) -> int:
    effective_isc = max(0.0, float(isc_ma) * max(0.0, irradiance))
    if panel_voltage_mv >= voc_mv or voc_mv <= 0:
        return 0
    ratio = float(panel_voltage_mv) / float(voc_mv)
    current = effective_isc * (1.0 - ratio * ratio)
    return max(0, int(round(current)))


def compute_power_mw(voltage_mv: int, current_ma: int) -> int:
    return int((int(voltage_mv) * int(current_ma)) / 1000)


def compute_panel_voltage_from_duty(duty: int, battery_mv: int, voc_mv: int) -> int:
    if duty <= 0:
        duty = 32768
    voltage = int((int(battery_mv) * 65535) / duty)
    return min(max(voltage, 0), int(voc_mv))


def update_simulation_config(payload: dict[str, Any]) -> None:
    with STATE.lock:
        sim = STATE.simulation
        for key, minimum, maximum, cast in (
            ("voc_mv", 1000, 50000, int),
            ("isc_ma", 0, 20000, int),
            ("battery_mv", 1000, 20000, int),
            ("period_ms", 100, 2000, int),
            ("temperature_decic", -400, 800, int),
            ("irradiance", 0.0, 1.5, float),
        ):
            if key in payload:
                value = cast(payload[key])
                if value < minimum:
                    value = minimum
                if value > maximum:
                    value = maximum
                sim[key] = value
        sim["target_vmp_mv"] = int(sim["voc_mv"] / math.sqrt(3))
        if sim["target_vmp_mv"] > 0:
            sim["target_duty"] = int(
                (int(sim["battery_mv"]) * 65535) / int(sim["target_vmp_mv"])
            )


def send_injection_from_duty(duty: int) -> None:
    with STATE.lock:
        sim = dict(STATE.simulation)
    panel_mv = compute_panel_voltage_from_duty(
        duty, int(sim["battery_mv"]), int(sim["voc_mv"])
    )
    panel_ma = compute_panel_current_ma(
        panel_mv, int(sim["voc_mv"]), int(sim["isc_ma"]), float(sim["irradiance"])
    )
    panel_power_mw = compute_power_mw(panel_mv, panel_ma)
    battery_ma = int((panel_power_mw * 1000) / max(1, int(sim["battery_mv"])))
    battery_ma = max(-32768, min(32767, battery_ma))
    pv_adc = max(0, min(4095, int(round(panel_mv / PANEL_ADC_TO_MILLIVOLTS))))
    pi_adc = max(0, min(4095, int(round(panel_ma / PANEL_ADC_TO_MILLIAMPS))))
    command = (
        f"inject {pv_adc} {pi_adc} {int(sim['battery_mv'])} {battery_ma} "
        f"{int(sim['battery_mv'])} {int(sim['temperature_decic'])} "
        f"1 {SATELLITE_MODE_CHARGING} {SAFE_SUBSTATE_CHARGING} 0"
    )
    BRIDGE.send(command)


def mppt_loop() -> None:
    try:
        BRIDGE.open(STATE.serial_port_name)
        for command in ("pwm-disarm", "source injected", "stream off", "mode mppt"):
            BRIDGE.send(command)
            time.sleep(0.25)

        send_injection_from_duty(32768)
        time.sleep(0.25)
        BRIDGE.send("pwm-arm")

        while True:
            with STATE.lock:
                if not STATE.mppt_running:
                    break
                period_s = float(STATE.simulation["period_ms"]) / 1000.0
                latest_duty = int(
                    STATE.telemetry.get(
                        "applied_duty", STATE.telemetry.get("mppt_duty", 32768)
                    )
                )
                if latest_duty <= 0:
                    latest_duty = int(STATE.telemetry.get("mppt_duty", 32768))
                if latest_duty <= 0:
                    latest_duty = 32768

            BRIDGE.send("telemetry")
            time.sleep(max(0.08, period_s * 0.45))
            send_injection_from_duty(latest_duty)
            time.sleep(max(0.08, period_s * 0.55))
    finally:
        try:
            BRIDGE.send("pwm-disarm")
        except Exception:
            pass
        with STATE.lock:
            STATE.mppt_running = False


def fsm_start_sequence() -> None:
    try:
        BRIDGE.open(STATE.serial_port_name)
        BRIDGE.send("pwm-disarm")
        time.sleep(0.2)
        BRIDGE.send("test-timeouts fast")
        time.sleep(0.6)
        BRIDGE.send("sim-fsm run")
    except Exception as exc:  # pragma: no cover - hardware dependent
        with STATE.lock:
            STATE.connection_error = str(exc)
            STATE.fsm_running = False


def make_response(handler: BaseHTTPRequestHandler, status: int, payload: Any) -> None:
    body = json.dumps(payload).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def read_json_body(handler: BaseHTTPRequestHandler) -> dict[str, Any]:
    length = int(handler.headers.get("Content-Length", "0"))
    if length <= 0:
        return {}
    raw = handler.rfile.read(length)
    return json.loads(raw.decode("utf-8"))


class DashboardHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - stdlib method name
        path = urlparse(self.path).path
        if path == "/":
            body = HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/api/status":
            make_response(self, 200, STATE.snapshot())
            return
        self.send_error(404)

    def do_POST(self) -> None:  # noqa: N802 - stdlib method name
        path = urlparse(self.path).path
        try:
            payload = read_json_body(self)
            if path == "/api/serial/open":
                port = str(payload.get("port", DEFAULT_PORT))
                BRIDGE.open(port)
                make_response(self, 200, {"ok": True})
                return
            if path == "/api/command":
                BRIDGE.open(STATE.serial_port_name)
                BRIDGE.send(str(payload.get("command", "")))
                make_response(self, 200, {"ok": True})
                return
            if path == "/api/mppt/start":
                update_simulation_config(payload)
                with STATE.lock:
                    STATE.history.clear()
                    STATE.mppt_running = True
                    STATE.fsm_running = False
                thread = threading.Thread(target=mppt_loop, daemon=True)
                thread.start()
                make_response(self, 200, {"ok": True})
                return
            if path == "/api/mppt/stop":
                with STATE.lock:
                    STATE.mppt_running = False
                BRIDGE.send("pwm-disarm")
                make_response(self, 200, {"ok": True})
                return
            if path == "/api/fsm/start":
                with STATE.lock:
                    STATE.mppt_running = False
                    STATE.fsm_running = True
                    STATE.fsm = {
                        "running": True,
                        "complete": False,
                        "pass": 0,
                        "fail": 0,
                        "scenarios": [
                            {
                                "name": item["name"],
                                "source_name": item["source_name"],
                                "purpose": item["purpose"],
                                "status": "attente",
                                "injected": "",
                                "result": "",
                            }
                            for item in FSM_SCENARIOS
                        ],
                    }
                thread = threading.Thread(target=fsm_start_sequence, daemon=True)
                thread.start()
                make_response(self, 200, {"ok": True})
                return
            if path == "/api/fsm/stop":
                with STATE.lock:
                    STATE.fsm_running = False
                    STATE.fsm["running"] = False
                BRIDGE.send("sim-fsm stop")
                make_response(self, 200, {"ok": True})
                return
        except Exception as exc:
            with STATE.lock:
                STATE.connection_error = str(exc)
            make_response(self, 500, {"ok": False, "error": str(exc)})
            return
        self.send_error(404)

    def log_message(self, fmt: str, *args: Any) -> None:
        return


HTML = r"""<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>EPS - Démonstration MPPT</title>
  <style>
    :root {
      --bg: #f4f6f8;
      --panel: #ffffff;
      --ink: #19212a;
      --muted: #657282;
      --line: #d6dde5;
      --blue: #0b6fcb;
      --green: #23824b;
      --red: #b42318;
      --yellow: #b7791f;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      color: var(--ink);
      background: var(--bg);
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      padding: 14px 18px;
      background: #17202a;
      color: white;
    }
    header h1 { margin: 0; font-size: 20px; font-weight: 650; }
    header .status { font-size: 14px; color: #d7e1ea; }
    main {
      display: grid;
      grid-template-columns: 340px minmax(0, 1fr);
      gap: 12px;
      padding: 12px;
    }
    section, .card {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
    }
    h2 { margin: 0 0 10px; font-size: 16px; }
    h3 { margin: 12px 0 8px; font-size: 14px; color: #25313d; }
    label { display: block; margin-top: 8px; font-size: 13px; color: var(--muted); }
    input {
      width: 100%;
      height: 34px;
      margin-top: 3px;
      border: 1px solid var(--line);
      border-radius: 6px;
      padding: 6px 8px;
      font-size: 14px;
    }
    button {
      height: 36px;
      border: 1px solid var(--line);
      border-radius: 6px;
      padding: 0 11px;
      background: #fff;
      color: var(--ink);
      cursor: pointer;
      font-weight: 600;
    }
    button.primary { background: var(--blue); color: white; border-color: var(--blue); }
    button.danger { background: var(--red); color: white; border-color: var(--red); }
    button.safe { background: var(--green); color: white; border-color: var(--green); }
    .row { display: flex; gap: 8px; align-items: center; }
    .row > * { flex: 1; }
    .grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .grid3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; }
    .metric {
      border: 1px solid var(--line);
      border-radius: 6px;
      padding: 8px;
      min-height: 58px;
    }
    .metric .label { font-size: 12px; color: var(--muted); }
    .metric .value { font-size: 18px; font-weight: 700; margin-top: 3px; }
    canvas { width: 100%; height: 280px; border: 1px solid var(--line); border-radius: 6px; background: #fff; }
    .small-canvas { height: 185px; }
    .log {
      height: 260px;
      overflow: auto;
      background: #0f1720;
      color: #d9e7f2;
      padding: 10px;
      border-radius: 6px;
      font: 12px/1.35 Consolas, monospace;
      white-space: pre-wrap;
    }
    .scenario {
      border: 1px solid var(--line);
      border-radius: 6px;
      padding: 8px;
      margin-bottom: 8px;
    }
    .scenario .title { font-weight: 700; }
    .scenario .purpose { font-size: 13px; color: var(--muted); margin-top: 3px; }
    .badge {
      display: inline-block;
      padding: 2px 7px;
      border-radius: 999px;
      font-size: 12px;
      font-weight: 700;
      background: #edf2f7;
      color: #344054;
    }
    .badge.pass { background: #dcfce7; color: #166534; }
    .badge.fail { background: #fee2e2; color: #991b1b; }
    .badge.run { background: #fef3c7; color: #92400e; }
    .muted { color: var(--muted); }
    .warning { color: var(--red); font-weight: 700; }
    @media (max-width: 1000px) {
      main { grid-template-columns: 1fr; }
      .grid2, .grid3 { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <header>
    <h1>EPS - Démonstration MPPT et machine d'état</h1>
    <div class="status" id="connection">Connexion: inconnue</div>
  </header>
  <main>
    <aside>
      <section>
        <h2>Contrôle de la démonstration</h2>
        <label>Port série ESP32</label>
        <input id="port" value="COM3">
        <div class="row" style="margin-top: 10px;">
          <button onclick="openSerial()">Connecter</button>
          <button class="danger" onclick="stopMppt()">Stop PWM</button>
        </div>

        <h3>Modèle panneau solaire</h3>
        <label>Tension à vide Voc (mV)</label>
        <input id="voc" type="number" value="18000">
        <label>Courant court-circuit Isc (mA)</label>
        <input id="isc" type="number" value="3000">
        <label>Tension batterie simulée (mV)</label>
        <input id="battery" type="number" value="7400">
        <label>Irradiance relative</label>
        <input id="irr" type="number" step="0.05" value="1.0">
        <label>Période de boucle PC (ms)</label>
        <input id="period" type="number" value="250">
        <div class="row" style="margin-top: 10px;">
          <button class="primary" onclick="startMppt()">Démarrer MPPT</button>
          <button onclick="sendTelemetry()">Lire télémétrie</button>
        </div>
      </section>

      <section style="margin-top: 12px;">
        <h2>Machine d'état</h2>
        <p class="muted">Lance les scénarios injectés sur la vraie carte. La carte doit répondre PASS/FAIL.</p>
        <div class="row">
          <button class="safe" onclick="startFsm()">Lancer scénarios</button>
          <button onclick="stopFsm()">Arrêter</button>
        </div>
        <div id="fsmList" style="margin-top: 10px;"></div>
      </section>

      <section style="margin-top: 12px;">
        <h2>Commande manuelle</h2>
        <div class="row">
          <input id="manual" value="state" onkeydown="if(event.key==='Enter') sendManual()">
          <button onclick="sendManual()">Envoyer</button>
        </div>
      </section>
    </aside>

    <div>
      <section>
        <h2>Lecture immédiate</h2>
        <div class="grid3">
          <div class="metric"><div class="label">Mode carte</div><div class="value" id="mode">-</div></div>
          <div class="metric"><div class="label">PWM appliqué</div><div class="value" id="pwm">-</div></div>
          <div class="metric"><div class="label">Puissance panneau</div><div class="value" id="power">-</div></div>
          <div class="metric"><div class="label">Tension / courant envoyés</div><div class="value" id="sentVI">-</div></div>
          <div class="metric"><div class="label">Duty MPPT / appliqué</div><div class="value" id="duty">-</div></div>
          <div class="metric"><div class="label">Erreur vers MPP</div><div class="value" id="error">-</div></div>
        </div>
      </section>

      <div class="grid2" style="margin-top: 12px;">
        <section>
          <h2>Courbe I-V simulée</h2>
          <canvas id="iv"></canvas>
          <p class="muted">Point vert: maximum de puissance théorique. Point rouge: point d'opération imposé par le duty que la vraie carte commande.</p>
        </section>
        <section>
          <h2>Courbe P-V simulée</h2>
          <canvas id="pv"></canvas>
          <p class="muted">Le point rouge doit converger vers le sommet de la courbe.</p>
        </section>
      </div>

      <div class="grid2" style="margin-top: 12px;">
        <section>
          <h2>Historique puissance</h2>
          <canvas id="powerPlot" class="small-canvas"></canvas>
        </section>
        <section>
          <h2>Historique duty</h2>
          <canvas id="dutyPlot" class="small-canvas"></canvas>
        </section>
      </div>

      <section style="margin-top: 12px;">
        <h2>Transparence série: commandes et réponses</h2>
        <div class="log" id="log"></div>
      </section>
    </div>
  </main>

  <script>
    let snapshot = {};

    async function api(path, body = undefined) {
      const opts = body === undefined ? {} : {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body),
      };
      const res = await fetch(path, opts);
      if (!res.ok) {
        const text = await res.text();
        throw new Error(text);
      }
      return await res.json();
    }

    function configPayload() {
      return {
        port: document.getElementById('port').value,
        voc_mv: Number(document.getElementById('voc').value),
        isc_ma: Number(document.getElementById('isc').value),
        battery_mv: Number(document.getElementById('battery').value),
        irradiance: Number(document.getElementById('irr').value),
        period_ms: Number(document.getElementById('period').value),
      };
    }

    async function openSerial() { await api('/api/serial/open', {port: document.getElementById('port').value}); }
    async function startMppt() { await api('/api/mppt/start', configPayload()); }
    async function stopMppt() { await api('/api/mppt/stop', {}); }
    async function startFsm() { await api('/api/fsm/start', {}); }
    async function stopFsm() { await api('/api/fsm/stop', {}); }
    async function sendTelemetry() { await api('/api/command', {command: 'telemetry'}); }
    async function sendManual() {
      const value = document.getElementById('manual').value;
      await api('/api/command', {command: value});
    }

    function currentMa(v, sim) {
      if (!sim || v >= sim.voc_mv) return 0;
      const isc = sim.isc_ma * sim.irradiance;
      const r = v / sim.voc_mv;
      return Math.max(0, isc * (1 - r * r));
    }

    function powerMw(v, i) { return v * i / 1000; }

    function latestPoint() {
      const t = snapshot.telemetry || {};
      return {
        v: Number(t.panel_voltage_mv || 0),
        i: Number(t.panel_current_ma || 0),
        p: Number(t.input_power_mw || 0),
        applied: Number(t.applied_duty || 0),
        mppt: Number(t.mppt_duty || 0),
      };
    }

    function drawAxes(ctx, w, h, titleY) {
      ctx.clearRect(0, 0, w, h);
      ctx.strokeStyle = '#d6dde5';
      ctx.lineWidth = 1;
      for (let k = 0; k <= 5; k++) {
        const x = 40 + k * (w - 60) / 5;
        const y = 18 + k * (h - 48) / 5;
        ctx.beginPath(); ctx.moveTo(x, 18); ctx.lineTo(x, h - 30); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(40, y); ctx.lineTo(w - 20, y); ctx.stroke();
      }
      ctx.fillStyle = '#657282';
      ctx.font = '12px system-ui';
      ctx.fillText('V panneau', w - 92, h - 8);
      ctx.save();
      ctx.translate(12, 92);
      ctx.rotate(-Math.PI / 2);
      ctx.fillText(titleY, 0, 0);
      ctx.restore();
    }

    function drawIv() {
      const canvas = document.getElementById('iv');
      const ctx = canvas.getContext('2d');
      const rect = canvas.getBoundingClientRect();
      canvas.width = rect.width * devicePixelRatio;
      canvas.height = rect.height * devicePixelRatio;
      ctx.scale(devicePixelRatio, devicePixelRatio);
      const w = rect.width, h = rect.height;
      const sim = snapshot.simulation || {};
      const voc = sim.voc_mv || 18000;
      const isc = (sim.isc_ma || 3000) * (sim.irradiance || 1);
      drawAxes(ctx, w, h, 'I panneau');
      const xOf = v => 40 + (v / voc) * (w - 60);
      const yOf = i => (h - 30) - (i / Math.max(1, isc)) * (h - 48);
      ctx.strokeStyle = '#0b6fcb';
      ctx.lineWidth = 2;
      ctx.beginPath();
      for (let n = 0; n <= 160; n++) {
        const v = voc * n / 160;
        const i = currentMa(v, sim);
        if (n === 0) ctx.moveTo(xOf(v), yOf(i)); else ctx.lineTo(xOf(v), yOf(i));
      }
      ctx.stroke();
      const vmp = sim.target_vmp_mv || voc / Math.sqrt(3);
      const imp = currentMa(vmp, sim);
      drawPoint(ctx, xOf(vmp), yOf(imp), '#23824b', 'MPP');
      const p = latestPoint();
      drawPoint(ctx, xOf(p.v), yOf(p.i), '#b42318', 'carte');
    }

    function drawPv() {
      const canvas = document.getElementById('pv');
      const ctx = canvas.getContext('2d');
      const rect = canvas.getBoundingClientRect();
      canvas.width = rect.width * devicePixelRatio;
      canvas.height = rect.height * devicePixelRatio;
      ctx.scale(devicePixelRatio, devicePixelRatio);
      const w = rect.width, h = rect.height;
      const sim = snapshot.simulation || {};
      const voc = sim.voc_mv || 18000;
      const vmp = sim.target_vmp_mv || voc / Math.sqrt(3);
      const pmax = powerMw(vmp, currentMa(vmp, sim));
      drawAxes(ctx, w, h, 'P panneau');
      const xOf = v => 40 + (v / voc) * (w - 60);
      const yOf = p => (h - 30) - (p / Math.max(1, pmax)) * (h - 48);
      ctx.strokeStyle = '#b7791f';
      ctx.lineWidth = 2;
      ctx.beginPath();
      for (let n = 0; n <= 160; n++) {
        const v = voc * n / 160;
        const p = powerMw(v, currentMa(v, sim));
        if (n === 0) ctx.moveTo(xOf(v), yOf(p)); else ctx.lineTo(xOf(v), yOf(p));
      }
      ctx.stroke();
      drawPoint(ctx, xOf(vmp), yOf(pmax), '#23824b', 'MPP');
      const p = latestPoint();
      drawPoint(ctx, xOf(p.v), yOf(p.p), '#b42318', 'carte');
    }

    function drawPoint(ctx, x, y, color, label) {
      ctx.fillStyle = color;
      ctx.beginPath(); ctx.arc(x, y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.font = '12px system-ui';
      ctx.fillText(label, x + 7, y - 7);
    }

    function drawHistory(canvasId, key, color, maxValue, label) {
      const canvas = document.getElementById(canvasId);
      const ctx = canvas.getContext('2d');
      const rect = canvas.getBoundingClientRect();
      canvas.width = rect.width * devicePixelRatio;
      canvas.height = rect.height * devicePixelRatio;
      ctx.scale(devicePixelRatio, devicePixelRatio);
      const w = rect.width, h = rect.height;
      ctx.clearRect(0,0,w,h);
      ctx.strokeStyle = '#d6dde5';
      ctx.strokeRect(35, 12, w - 48, h - 34);
      const hist = snapshot.history || [];
      if (hist.length < 2) return;
      const values = hist.map(p => Number(p[key] || 0));
      const max = maxValue || Math.max(1, ...values);
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.beginPath();
      values.forEach((value, idx) => {
        const x = 35 + idx * (w - 48) / Math.max(1, values.length - 1);
        const y = (h - 22) - (value / max) * (h - 42);
        if (idx === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
      });
      ctx.stroke();
      ctx.fillStyle = '#657282';
      ctx.font = '12px system-ui';
      ctx.fillText(label, 42, 26);
    }

    function renderMetrics() {
      const t = snapshot.telemetry || {};
      const sim = snapshot.simulation || {};
      document.getElementById('connection').textContent =
        snapshot.connected ? `Connecté à ${snapshot.serial_port}` : `Déconnecté ${snapshot.connection_error || ''}`;
      document.getElementById('mode').textContent = `${t.control || '-'} / ${t.pcu_mode || '-'}`;
      document.getElementById('pwm').textContent = `pwm=${t.pwm ?? '-'} armé=${t.pwm_armed ?? '-'}`;
      document.getElementById('power').textContent = `${t.input_power_mw || 0} mW`;
      document.getElementById('sentVI').textContent = `${t.panel_voltage_mv || 0} mV / ${t.panel_current_ma || 0} mA`;
      document.getElementById('duty').textContent = `${t.mppt_duty || 0} / ${t.applied_duty || 0}`;
      const err = (Number(t.applied_duty || 0) - Number(sim.target_duty || 0));
      document.getElementById('error').textContent = `${err} counts`;
    }

    function renderLog() {
      const lines = snapshot.lines || [];
      const commands = snapshot.commands || [];
      const text = [
        '--- Commandes visibles ---',
        ...commands.slice(-40).map(c => `${c.direction}: ${c.text}`),
        '',
        '--- Sortie série brute ---',
        ...lines.slice(-80),
      ].join('\n');
      const el = document.getElementById('log');
      el.textContent = text;
      el.scrollTop = el.scrollHeight;
    }

    function renderFsm() {
      const fsm = snapshot.fsm || {scenarios: []};
      const el = document.getElementById('fsmList');
      el.innerHTML = '';
      (fsm.scenarios || []).forEach(s => {
        const div = document.createElement('div');
        div.className = 'scenario';
        const cls = s.status === 'PASS' ? 'pass' : (s.status === 'FAIL' ? 'fail' : (s.status === 'en cours' ? 'run' : ''));
        div.innerHTML = `<div class="title">${s.name} <span class="badge ${cls}">${s.status}</span></div>
          <div class="purpose">${s.purpose}</div>
          <div class="muted">${s.injected || ''}</div>
          <div>${s.result || ''}</div>`;
        el.appendChild(div);
      });
    }

    async function refresh() {
      try {
        snapshot = await api('/api/status');
        renderMetrics();
        renderLog();
        renderFsm();
        drawIv();
        drawPv();
        const sim = snapshot.simulation || {};
        const pmax = (sim.target_vmp_mv || 0) * currentMa(sim.target_vmp_mv || 0, sim) / 1000;
        drawHistory('powerPlot', 'input_power_mw', '#b7791f', pmax * 1.25, 'Puissance mesurée par la carte');
        drawHistory('dutyPlot', 'applied_duty', '#0b6fcb', 65535, 'Duty appliqué');
      } catch (err) {
        document.getElementById('connection').textContent = 'Erreur: ' + err;
      }
    }

    setInterval(refresh, 500);
    refresh();
  </script>
</body>
</html>
"""


def shutdown_safely() -> None:
    try:
        if STATE.connected:
            BRIDGE.send("pwm-disarm")
            time.sleep(0.2)
    except Exception:
        pass
    BRIDGE.close()


def main() -> None:
    atexit.register(shutdown_safely)
    server = ThreadingHTTPServer((SERVER_HOST, SERVER_PORT), DashboardHandler)
    print(f"Dashboard EPS disponible sur http://localhost:{SERVER_PORT}")
    print(f"Port serie par defaut: {DEFAULT_PORT} a {BAUD_RATE} bauds")
    print("Ferme Arduino Serial Monitor / PuTTY avant de demarrer une demo.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nArret demande, PWM desarme si possible.")
    finally:
        shutdown_safely()
        server.server_close()


if __name__ == "__main__":
    main()
