"""
CHESS EPS — State Machine Simulation Web App (Flask backend)

Serves the dark-theme HTML/JS frontend and runs C simulations on demand.

Usage:
    pip install flask
    python tools/eps_webapp.py

Opens browser at http://localhost:5000
"""

import subprocess
import os
import sys
import csv
import io
import json
import webbrowser
import logging
from threading import Timer

from flask import Flask, send_from_directory, jsonify, request

# ── Logging ──────────────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("eps_webapp")

# ── Paths ────────────────────────────────────────────────────────────────────

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
STATIC_DIR = os.path.join(SCRIPT_DIR, "static")
EXECUTABLE = os.path.join(PROJECT_ROOT, "tests", "run_eps_simulation")

if os.name == "nt" and not EXECUTABLE.endswith(".exe"):
    EXECUTABLE += ".exe"

if not os.path.exists(EXECUTABLE):
    log.error("Simulation executable not found at: %s", EXECUTABLE)
    log.error("Build it first: make -f Makefile.eps_sim")
    sys.exit(1)

log.info("Executable: %s", EXECUTABLE)
log.info("Static dir: %s", STATIC_DIR)

# ── Flask app ────────────────────────────────────────────────────────────────

app = Flask(__name__, static_folder=STATIC_DIR)


@app.route("/")
def index():
    log.info("Serving index.html")
    return send_from_directory(STATIC_DIR, "index.html")


@app.route("/<path:path>")
def static_files(path):
    return send_from_directory(STATIC_DIR, path)


@app.route("/api/simulate", methods=["POST"])
def simulate():
    data = request.get_json(force=True)
    scenario = int(data.get("scenario", 1))

    log.info("API: simulate scenario %d", scenario)

    if scenario < 1 or scenario > 12:
        return jsonify({"error": "Scenario must be 1-12"}), 400

    try:
        timeout = 300 if scenario == 8 else 60
        result = subprocess.run(
            [EXECUTABLE, str(scenario)],
            capture_output=True,
            text=True,
            timeout=timeout,
        )

        if result.returncode != 0:
            log.error("Simulation failed: %s", result.stderr)
            return jsonify({"error": result.stderr}), 500

        # Parse CSV to JSON array
        lines = [
            line for line in result.stdout.splitlines()
            if line.strip() and not line.startswith("#")
        ]

        if len(lines) < 2:
            return jsonify({"error": "No data produced"}), 500

        reader = csv.DictReader(io.StringIO("\n".join(lines)))
        rows = []
        for row in reader:
            rows.append({
                "t": float(row["time_seconds"]),
                "mode": int(row["pcu_mode"]),
                "safe": int(row["safe_mode"]),
                "duty": float(row["duty_cycle_percent"]),
                "vbat": int(row["battery_voltage_mv"]),
                "ibat": int(row["battery_current_ma"]),
                "soc": float(row["battery_soc_percent"]),
                "vsol": int(row["solar_voltage_mv"]),
                "psol": float(row["panel_power_watts"]),
                "temp": int(row["temperature_decideg"]),
                "heater": int(row["heater_on"]),
                "efuse": int(row["panel_efuse_on"]),
                "loads": int(row["loads_enabled"]),
            })

        log.info("Simulation complete: %d data points", len(rows))
        return jsonify({"data": rows, "stderr": result.stderr.strip()})

    except subprocess.TimeoutExpired:
        log.error("Simulation timed out for scenario %d", scenario)
        return jsonify({"error": "Simulation timed out"}), 504
    except Exception as e:
        log.error("Simulation exception: %s", e)
        return jsonify({"error": str(e)}), 500


def open_browser():
    webbrowser.open("http://localhost:5000")


if __name__ == "__main__":
    log.info("Starting CHESS EPS Simulation webapp on http://localhost:5000")
    Timer(1.5, open_browser).start()
    app.run(host="127.0.0.1", port=5000, debug=False)
