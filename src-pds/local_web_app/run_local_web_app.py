#!/usr/bin/env python3
"""Unified local web app — serves every Source PDS demo page from one process.

One Python process owns the ESP32 USB serial connection. Each demo page is
routed by URL:

    /            landing page (links to /mppt and /state)
    /mppt        MPPT convergence page
    /state       State transition page
    /static/...  static assets per page
    /api/...     shared and page-specific endpoints

Run:

    python src-pds/local_web_app/run_local_web_app.py

Then open:

    http://127.0.0.1:8000
"""

from __future__ import annotations

import argparse
import atexit
import json
import mimetypes
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

APP_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(APP_DIR))

# Import after sys.path patch so the sibling modules are reachable when this
# script is run as `python src-pds/local_web_app/run_local_web_app.py`.
from serial_bridge_shared_by_pages import (  # noqa: E402
    BRIDGE,
    DEFAULT_SERIAL_PORT,
    STATE,
    connect_to_serial_port,
    disconnect_from_serial_port,
    send_get_values,
    send_off_command,
)
import manual_control_page_actions as manual_actions  # noqa: E402
import mppt_convergence_page_actions as mppt_actions  # noqa: E402
import sensor_reads_page_actions as sensor_actions  # noqa: E402
import state_transition_page_actions as state_actions  # noqa: E402


STATIC_DIR = APP_DIR / "static"
DEFAULT_SERVER_HOST = "127.0.0.1"
DEFAULT_SERVER_PORT = 8000


# -----------------------------------------------------------------------------
# HTTP handler — routing layer only. Per-page logic lives in the actions
# modules and the shared serial bridge.
# -----------------------------------------------------------------------------


class LocalWebAppHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path == "/":
            self.send_static_file(STATIC_DIR / "index.html")
            return
        if path == "/mppt":
            self.send_static_file(
                STATIC_DIR / "mppt" / "mppt_convergence.html"
            )
            return
        if path == "/state":
            self.send_static_file(
                STATIC_DIR / "state" / "state_transition.html"
            )
            return
        if path == "/manual":
            self.send_static_file(
                STATIC_DIR / "manual" / "manual_control.html"
            )
            return
        if path == "/sensor-reads":
            self.send_static_file(
                STATIC_DIR / "sensor-reads" / "sensor_reads.html"
            )
            return
        if path == "/api/status":
            self.send_json(STATE.snapshot())
            return
        if path == "/api/state/scenarios":
            self.send_json({"scenarios": state_actions.SCENARIOS})
            return
        if path.startswith("/static/"):
            self.send_static_file(STATIC_DIR / path.removeprefix("/static/"))
            return
        self.send_error(404, "Not found")

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        try:
            payload = self.read_json_body()
            result = self.handle_api_post(path, payload)
            self.send_json({"ok": True, **result})
        except Exception as exc:
            self.send_json({"ok": False, "error": str(exc)}, status=400)

    def handle_api_post(
        self, path: str, payload: dict[str, Any]
    ) -> dict[str, Any]:
        if path == "/api/connect":
            port = str(payload.get("port", STATE.serial_port)).strip()
            connect_to_serial_port(port or DEFAULT_SERIAL_PORT)
            return {"status": "connected"}
        if path == "/api/disconnect":
            disconnect_from_serial_port()
            return {"status": "disconnected"}
        if path == "/api/off":
            send_off_command()
            return {"status": "off_command_sent"}
        if path == "/api/get_values":
            send_get_values()
            return {"status": "get_values_sent"}

        if path == "/api/mppt/start_mppt":
            command = mppt_actions.send_start_mppt_command(payload)
            return {"status": "mppt_started", "command": command}

        if path == "/api/state/enter_state_test_mode":
            command = state_actions.enter_state_test_mode()
            return {"status": "state_test_entered", "command": command}
        if path == "/api/state/run_scenario":
            scenario_name = str(payload.get("name", "")).strip()
            scenario = state_actions.find_scenario_by_name(scenario_name)
            if scenario is None:
                raise ValueError(f"Unknown scenario: {scenario_name}")
            return {
                "status": "scenario_complete",
                "result": state_actions.run_one_scenario(scenario),
            }
        if path == "/api/state/run_all_scenarios":
            summary = state_actions.run_all_scenarios()
            return {"status": "all_scenarios_complete", "summary": summary}

        if path == "/api/manual/enter_manual_mode":
            command = manual_actions.enter_manual_control_mode()
            return {"status": "manual_mode_entered", "command": command}
        if path == "/api/manual/set_pwm":
            command = manual_actions.send_set_manual_pwm(payload.get("duty", 0))
            return {"status": "manual_pwm_sent", "command": command}
        if path == "/api/manual/set_pv":
            command = manual_actions.send_set_manual_pv(bool(payload.get("on", False)))
            return {"status": "manual_pv_sent", "command": command}
        if path == "/api/manual/set_bat":
            command = manual_actions.send_set_manual_bat(bool(payload.get("on", False)))
            return {"status": "manual_bat_sent", "command": command}
        if path == "/api/manual/set_led":
            command = manual_actions.send_set_manual_led(bool(payload.get("on", False)))
            return {"status": "manual_led_sent", "command": command}

        if path == "/api/sensor-reads/enter":
            result = sensor_actions.enter_sensor_reads_view()
            return {"status": result}
        if path == "/api/sensor-reads/set_source":
            source = str(payload.get("source", "")).strip()
            command = sensor_actions.send_set_sensor_source(source)
            return {"status": "source_sent", "command": command}

        raise ValueError(f"Unknown API endpoint: {path}")

    # ── Low-level HTTP helpers ──────────────────────────────────────────────

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

    def send_static_file(self, path: Path) -> None:
        if not path.is_file() or STATIC_DIR not in path.resolve().parents:
            self.send_error(404, "Not found")
            return
        body = path.read_bytes()
        content_type = (
            mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        )
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format_text: str, *args: Any) -> None:
        return


# -----------------------------------------------------------------------------
# Lifecycle.
# -----------------------------------------------------------------------------


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
    server = ThreadingHTTPServer((args.host, args.port), LocalWebAppHandler)
    print(f"Source PDS local web app: http://{args.host}:{args.port}")
    print(f"  - MPPT page:        http://{args.host}:{args.port}/mppt")
    print(f"  - State page:       http://{args.host}:{args.port}/state")
    print(f"  - Manual page:      http://{args.host}:{args.port}/manual")
    print(f"  - Sensor Reads:     http://{args.host}:{args.port}/sensor-reads")
    print(f"ESP32 serial port default: {args.serial_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping local web app.")
    finally:
        shutdown_safely()
        server.server_close()


if __name__ == "__main__":
    main()
