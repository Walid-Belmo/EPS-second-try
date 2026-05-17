"""Sensor Reads page actions.

Two responsibilities:

1. When the page first loads, kick the firmware into streaming so live
   values flow into the shared `STATE.telemetry`. Mirrors the existing
   per-page convention (the State and Manual pages do the same thing
   from their own enter_* helpers).

2. Switch the firmware's global sensor source between INJECTED (operator
   types numbers on the State page; the legacy default) and
   REAL_BOARD_HARDWARE (mainboard chips). The choice is firmware-side
   global state and applies to every read_*() call from the next loop
   iteration onward.

This page does NOT enter or leave any firmware mode. The sensor reads
block in the status reply is mode-agnostic — whatever mode is active,
the per-sensor numbers are appended to every reply.
"""

from __future__ import annotations

from serial_bridge_shared_by_pages import BRIDGE


STREAM_PERIOD_MS = 250


def enter_sensor_reads_view() -> str:
    """Asks the firmware to stream every status field at 250 ms cadence,
    then immediately requests one more snapshot so the page has fresh
    data before the first stream tick arrives. Safe to call repeatedly —
    `stream_values on` overrides any previous stream config."""
    BRIDGE.send(f"stream_values on period={STREAM_PERIOD_MS} fields=all")
    BRIDGE.send("get_values fields=all")
    return "stream_started"


def send_set_sensor_source(source: str) -> str:
    """Translates the human label to the text command the ESP32 bridge
    understands. Raises ValueError if the label is unrecognised so the
    HTTP handler can return a 400 instead of silently sending garbage."""
    if source not in ("injected", "real"):
        raise ValueError("source must be 'injected' or 'real'")
    command = f"set_sensor_source {source}"
    BRIDGE.send(command)
    return command
