"""State Transition page actions.

Owns the 12 preset scenarios and the orchestration code that drives them
through the shared serial bridge: enter state-test mode (start_state_demo
with baseline inputs + stream_values on), then per-scenario inject_state
and compare the resulting telemetry snapshot against the expected fields.
"""

from __future__ import annotations

import time
from typing import Any

from serial_bridge_shared_by_pages import BRIDGE, STATE


STREAM_PERIOD_MS = 300


# -----------------------------------------------------------------------------
# Baseline injection used as the starting point for every scenario.
# -----------------------------------------------------------------------------

BASELINE_INPUTS = {
    "battery_voltage": 7400,         # mV
    "battery_current": 250,          # mA, positive = charging
    "panel_voltage": 12000,          # mV
    "panel_current": 2200,           # mA
    "rail_voltage": 7600,            # mV (kept for CHIPS contract)
    "temperature": 220,              # 0.1 °C (22.0 °C)
    "heartbeat": 1,
    "obc_mode": "charging",
    "safe_substate": "charging",
    "faults": 0,
}


# -----------------------------------------------------------------------------
# Scenario list — 12 entries that cover the dispatcher's reachable modes
# plus all safe-mode entry reasons and sub-state load masks.
# -----------------------------------------------------------------------------

SCENARIOS = [
    {
        "name": "MPPT_CHARGE typical day",
        "input_overrides": {},
        "expected": {
            "pcu_mode": "MPPT_CHARGE",
            "panel_efuse": 1,
            "heater": 0,
            "safe_alert": 0,
        },
        "settle_seconds": 1.5,
    },
    {
        "name": "CV_FLOAT near max not full",
        "input_overrides": {"battery_voltage": 8350, "battery_current": 200},
        "expected": {"pcu_mode": "CV_FLOAT", "safe_alert": 0},
        "settle_seconds": 1.5,
    },
    {
        "name": "SA_LOAD_FOLLOW battery full",
        "input_overrides": {"battery_voltage": 8400, "battery_current": 0},
        "expected": {"pcu_mode": "SA_LOAD_FOLLOW", "safe_alert": 0},
        "settle_seconds": 1.5,
    },
    {
        "name": "BATTERY_DISCHARGE eclipse",
        "input_overrides": {"panel_voltage": 0, "panel_current": 0},
        "expected": {"pcu_mode": "BATTERY_DISCHARGE", "panel_efuse": 0},
        "settle_seconds": 1.5,
    },
    {
        "name": "Safe — battery below minimum",
        "input_overrides": {"battery_voltage": 4800},
        "expected": {"safe_alert": 1, "safe_reason": 1},
        "settle_seconds": 1.5,
    },
    {
        "name": "Safe — overtemperature",
        "input_overrides": {"temperature": 700},
        "expected": {"safe_alert": 1, "safe_reason": 2},
        "settle_seconds": 1.5,
    },
    {
        "name": "Safe — undertemperature, heater on",
        "input_overrides": {"temperature": -150},
        "expected": {"safe_alert": 1, "safe_reason": 2, "heater": 1},
        "settle_seconds": 1.5,
    },
    {
        "name": "Safe — OBC heartbeat lost",
        "input_overrides": {"heartbeat": 0},
        "expected": {"safe_alert": 1, "safe_reason": 3},
        "settle_seconds": 6.0,
    },
    {
        "name": "Safe substate CHARGING — load mask",
        "input_overrides": {
            "obc_mode": "safe",
            "safe_substate": "charging",
        },
        "expected": {"safe_alert": 1, "load_mask": 0x14},
        "settle_seconds": 1.5,
    },
    {
        "name": "Safe substate COMMUNICATION — load mask",
        "input_overrides": {
            "obc_mode": "safe",
            "safe_substate": "communication",
        },
        "expected": {"safe_alert": 1, "load_mask": 0x1C},
        "settle_seconds": 1.5,
    },
    {
        "name": "Safe substate DETUMBLING — load mask",
        "input_overrides": {
            "obc_mode": "safe",
            "safe_substate": "detumbling",
        },
        "expected": {"safe_alert": 1, "load_mask": 0x1C},
        "settle_seconds": 1.5,
    },
    {
        "name": "Safe substate REBOOT — load mask",
        "input_overrides": {
            "obc_mode": "safe",
            "safe_substate": "reboot",
        },
        "expected": {"safe_alert": 1, "load_mask": 0x1C},
        "settle_seconds": 1.5,
    },
]


# -----------------------------------------------------------------------------
# Command formatting and orchestration.
# -----------------------------------------------------------------------------


def merge_inputs_with_overrides(overrides: dict[str, Any]) -> dict[str, Any]:
    merged = dict(BASELINE_INPUTS)
    merged.update(overrides)
    return merged


def format_state_command(verb: str, inputs: dict[str, Any]) -> str:
    return (
        f"{verb} battery_voltage={int(inputs['battery_voltage'])} "
        f"battery_current={int(inputs['battery_current'])} "
        f"panel_voltage={int(inputs['panel_voltage'])} "
        f"panel_current={int(inputs['panel_current'])} "
        f"rail_voltage={int(inputs['rail_voltage'])} "
        f"temperature={int(inputs['temperature'])} "
        f"heartbeat={int(inputs['heartbeat'])} "
        f"obc_mode={inputs['obc_mode']} "
        f"safe_substate={inputs['safe_substate']} "
        f"faults={int(inputs['faults'])}"
    )


def enter_state_test_mode() -> str:
    STATE.record_debug("Entering state-test mode with baseline injection")
    command = format_state_command("start_state_demo", BASELINE_INPUTS)
    BRIDGE.send("stream_values off")
    time.sleep(0.05)
    BRIDGE.send("off")
    time.sleep(0.05)
    BRIDGE.send(command)
    time.sleep(0.08)
    BRIDGE.send(f"stream_values on period={STREAM_PERIOD_MS} fields=all")
    time.sleep(0.05)
    BRIDGE.send("get_values fields=all")
    with STATE.lock:
        STATE.in_state_test_mode = True
    return command


def wait_for_fresh_telemetry(settle_seconds: float) -> dict[str, Any]:
    """Block for `settle_seconds`, then return the latest telemetry snapshot."""
    deadline = time.time() + settle_seconds
    while time.time() < deadline:
        time.sleep(0.1)
    with STATE.lock:
        return dict(STATE.telemetry)


def run_one_scenario(scenario: dict[str, Any]) -> dict[str, Any]:
    inputs = merge_inputs_with_overrides(scenario["input_overrides"])
    command = format_state_command("inject_state", inputs)

    STATE.record_debug(f"Running scenario: {scenario['name']}")
    BRIDGE.send(command)
    time.sleep(0.08)

    snapshot = wait_for_fresh_telemetry(scenario["settle_seconds"])
    expected = dict(scenario["expected"])
    mismatches: list[str] = []
    observed_subset: dict[str, Any] = {}
    for field, expected_value in expected.items():
        observed_value = snapshot.get(field)
        observed_subset[field] = observed_value
        if observed_value != expected_value:
            mismatches.append(
                f"{field}: expected {expected_value!r}, got {observed_value!r}"
            )

    passed = len(mismatches) == 0
    result = {
        "name": scenario["name"],
        "command": command,
        "merged_inputs": inputs,
        "expected": expected,
        "observed": observed_subset,
        "snapshot": snapshot,
        "passed": passed,
        "mismatches": mismatches,
        "time": time.time(),
    }
    with STATE.lock:
        STATE.scenario_results[scenario["name"]] = result
        STATE.last_scenario_run = result
    return result


def run_all_scenarios() -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    passed_count = 0
    for scenario in SCENARIOS:
        # Reset to baseline so each scenario starts clean. Baseline has
        # obc_mode="charging" which clears the SAFE latch on the way through.
        BRIDGE.send(format_state_command("inject_state", BASELINE_INPUTS))
        time.sleep(0.2)
        result = run_one_scenario(scenario)
        results.append(result)
        if result["passed"]:
            passed_count += 1

    return {
        "total": len(SCENARIOS),
        "passed": passed_count,
        "failed": len(SCENARIOS) - passed_count,
        "results": results,
    }


def find_scenario_by_name(name: str) -> dict[str, Any] | None:
    for scenario in SCENARIOS:
        if scenario["name"] == name:
            return scenario
    return None


__all__ = [
    "BASELINE_INPUTS",
    "SCENARIOS",
    "enter_state_test_mode",
    "find_scenario_by_name",
    "run_all_scenarios",
    "run_one_scenario",
]
