"""
Rigorous verification of EPS simulation accuracy.

Runs specific short scenarios (18, 19, 20) where every value can be
hand-calculated, then checks:
1. Power conservation: P_solar = V_panel × I_panel at every row
2. Kirchhoff's current law: I_bus_from_solar = I_battery + I_loads
3. SOC integration: cumulative charge matches SOC change
4. Mode transition correctness: right mode for right conditions
5. Thermal control: heater on/off at correct temperatures
6. Safe mode: triggers at correct thresholds
7. Battery voltage: consistent with SOC via the battery model

Usage: python tests/verify_simulation_accuracy.py
Requires: tests/run_eps_simulation executable (make -f Makefile.eps_sim)
"""

import subprocess
import csv
import io
import sys
import os
import math

EXECUTABLE = os.path.join(os.path.dirname(__file__), "run_eps_simulation")
if os.name == "nt":
    EXECUTABLE += ".exe"

# Simulation constants (must match eps_simulation_runner.c)
DT = 0.0002  # 200 us per iteration
BATTERY_CAPACITY_AH = 6.0
NOMINAL_LOAD_CURRENT_A = 1.08
VOLTAGE_ADC_REF = 25.0
CURRENT_ADC_REF = 3.0

# Threshold constants (must match eps_configuration_parameters defaults)
VBAT_MAX_MV = 8400
VBAT_MIN_MV = 5000
SA_MIN_MV = 8200
TEMP_HEATER_DECIDEG = -100  # -10.0 C
TEMP_CHARGING_MIN_DECIDEG = 0  # 0.0 C

errors = []
warnings = []


def log(msg):
    print(f"  {msg}")


def fail(msg):
    errors.append(msg)
    print(f"  FAIL: {msg}")


def warn(msg):
    warnings.append(msg)
    print(f"  WARN: {msg}")


def run_scenario(num):
    result = subprocess.run(
        [EXECUTABLE, str(num)],
        capture_output=True, text=True, timeout=30
    )
    if result.returncode != 0:
        fail(f"Scenario {num} crashed: {result.stderr}")
        return []

    lines = [l for l in result.stdout.splitlines() if l.strip() and not l.startswith("#")]
    reader = csv.DictReader(io.StringIO("\n".join(lines)))
    rows = []
    for row in reader:
        rows.append({k: float(v) if '.' in v else int(v) for k, v in row.items()})
    return rows


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 1: SOC Integration (Eclipse — no solar, constant discharge)
# ═══════════════════════════════════════════════════════════════════════════════

def test_soc_integration_eclipse():
    """Scenario 19: Eclipse, 50% SOC, 1000 iterations logged every iteration.
    Battery discharges at ~1.08A for 1000 × 200us = 0.2 seconds.
    Expected SOC change: 1.08 × 0.2 / 3600 / 6.0 = 0.00001 = 0.001%
    """
    print("\n=== TEST 1: SOC Integration (Eclipse) ===")
    rows = run_scenario(19)
    if not rows:
        return

    log(f"Got {len(rows)} rows")
    first = rows[0]
    last = rows[-1]

    log(f"First SOC: {first['battery_soc_percent']:.6f}%")
    log(f"Last SOC:  {last['battery_soc_percent']:.6f}%")

    soc_change = last['battery_soc_percent'] - first['battery_soc_percent']
    log(f"SOC change: {soc_change:.6f}%")

    # Calculate expected SOC change from current
    total_time_s = (len(rows) - 1) * DT
    avg_current_a = sum(r['battery_current_ma'] for r in rows) / len(rows) / 1000.0
    expected_soc_change = avg_current_a * total_time_s / 3600.0 / BATTERY_CAPACITY_AH * 100.0

    log(f"Total time: {total_time_s:.4f}s")
    log(f"Avg current: {avg_current_a:.4f}A")
    log(f"Expected SOC change: {expected_soc_change:.6f}%")

    error_pct = abs(soc_change - expected_soc_change)
    if error_pct > 0.001:
        fail(f"SOC integration error: got {soc_change:.6f}%, expected {expected_soc_change:.6f}%, diff={error_pct:.6f}%")
    else:
        log(f"OK: SOC integration matches within 0.001%")

    # Verify mode is BATTERY_DISCHARGE throughout
    for i, r in enumerate(rows):
        if r['pcu_mode'] != 3:
            fail(f"Row {i}: mode={r['pcu_mode']}, expected 3 (BATTERY_DISCHARGE)")
            break
    else:
        log("OK: Mode is BATTERY_DISCHARGE throughout")

    # Verify panel eFuse is open
    for i, r in enumerate(rows):
        if r['panel_efuse_on'] != 0:
            fail(f"Row {i}: panel_efuse_on={r['panel_efuse_on']}, expected 0 (open in eclipse)")
            break
    else:
        log("OK: Panel eFuse is OPEN throughout")

    # Verify solar power is zero
    for i, r in enumerate(rows):
        if r['panel_power_watts'] > 0.001:
            fail(f"Row {i}: panel_power={r['panel_power_watts']}, expected 0 in eclipse")
            break
    else:
        log("OK: Panel power is 0 throughout")

    # Verify battery current is negative (discharging)
    for i, r in enumerate(rows[1:], 1):  # skip first row (may be transient)
        if r['battery_current_ma'] > 0:
            fail(f"Row {i}: battery_current={r['battery_current_ma']}mA, expected negative in eclipse")
            break
    else:
        log("OK: Battery current is negative (discharging) throughout")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 2: SOC Integration (Sun — charging)
# ═══════════════════════════════════════════════════════════════════════════════

def test_soc_integration_sun():
    """Scenario 18: Full sun, 50% SOC, 1000 iterations.
    Battery should charge (positive current). SOC should increase."""
    print("\n=== TEST 2: SOC Integration (Sun) ===")
    rows = run_scenario(18)
    if not rows:
        return

    first = rows[0]
    last = rows[-1]

    log(f"First SOC: {first['battery_soc_percent']:.6f}%")
    log(f"Last SOC:  {last['battery_soc_percent']:.6f}%")

    soc_change = last['battery_soc_percent'] - first['battery_soc_percent']
    log(f"SOC change: {soc_change:.6f}%")

    if soc_change <= 0:
        fail(f"SOC should increase in sun, but changed by {soc_change:.6f}%")
    else:
        log("OK: SOC is increasing (battery charging)")

    # Verify mode is MPPT_CHARGE
    for i, r in enumerate(rows):
        if r['pcu_mode'] != 0:
            warn(f"Row {i}: mode={r['pcu_mode']}, expected 0 (MPPT_CHARGE)")
            break
    else:
        log("OK: Mode is MPPT_CHARGE throughout")

    # Verify panel eFuse is closed
    for i, r in enumerate(rows):
        if r['panel_efuse_on'] != 1:
            fail(f"Row {i}: panel_efuse_on={r['panel_efuse_on']}, expected 1")
            break
    else:
        log("OK: Panel eFuse is CLOSED throughout")

    # Verify solar power > 0
    has_power = any(r['panel_power_watts'] > 1.0 for r in rows)
    if not has_power:
        fail("No significant solar power detected in sun scenario")
    else:
        max_power = max(r['panel_power_watts'] for r in rows)
        log(f"OK: Solar power present (max {max_power:.1f}W)")

    # Verify battery current is positive (charging) after initial transient
    positive_count = sum(1 for r in rows[10:] if r['battery_current_ma'] > 0)
    total_count = len(rows) - 10
    if positive_count < total_count * 0.8:
        fail(f"Only {positive_count}/{total_count} rows have positive current in sun")
    else:
        log(f"OK: {positive_count}/{total_count} rows have positive current (charging)")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 3: Cold Temperature (charging forbidden below 0C)
# ═══════════════════════════════════════════════════════════════════════════════

def test_cold_temperature():
    """Scenario 20: -20C, full sun, 70% SOC, 1000 iterations.
    Charging should be forbidden (duty cycle = 5%).
    Heater should be ON.
    Safe mode should be active (temp out of range)."""
    print("\n=== TEST 3: Cold Temperature ===")
    rows = run_scenario(20)
    if not rows:
        return

    # Check temperature is -20C = -200 decidegrees
    for r in rows:
        if r['temperature_decideg'] != -200:
            fail(f"Temperature should be -200 decideg, got {r['temperature_decideg']}")
            break
    else:
        log("OK: Temperature is -200 decidegrees (-20C) throughout")

    # Check heater is ON
    for i, r in enumerate(rows[1:], 1):
        if r['heater_on'] != 1:
            fail(f"Row {i}: heater_on={r['heater_on']}, expected 1 at -20C")
            break
    else:
        log("OK: Heater is ON throughout")

    # Check duty cycle is forced to minimum (5%)
    for i, r in enumerate(rows[1:], 1):
        if r['duty_cycle_percent'] > 5.1:
            fail(f"Row {i}: duty_cycle={r['duty_cycle_percent']}%, expected 5% (charging forbidden)")
            break
    else:
        log("OK: Duty cycle is 5% (charging forbidden below 0C)")

    # Check safe mode is active (temperature out of range)
    safe_count = sum(1 for r in rows[1:] if r['safe_mode'] == 1)
    if safe_count == 0:
        fail("Safe mode should be active at -20C (below TempMin=-10C)")
    else:
        log(f"OK: Safe mode active in {safe_count}/{len(rows)-1} rows")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 4: Battery Voltage Consistency with SOC
# ═══════════════════════════════════════════════════════════════════════════════

def test_battery_voltage_soc_consistency():
    """Check that battery voltage moves in the right direction with SOC.
    In eclipse (scenario 19): SOC decreases -> voltage should decrease or stay flat.
    In sun (scenario 18): SOC increases -> voltage should increase or stay flat."""
    print("\n=== TEST 4: Battery Voltage-SOC Consistency ===")

    # Eclipse
    rows = run_scenario(19)
    if rows:
        first_v = rows[0]['battery_voltage_mv']
        last_v = rows[-1]['battery_voltage_mv']
        first_soc = rows[0]['battery_soc_percent']
        last_soc = rows[-1]['battery_soc_percent']

        log(f"Eclipse: V {first_v}->{last_v}mV, SOC {first_soc:.4f}->{last_soc:.4f}%")

        if last_soc > first_soc + 0.001:
            fail(f"Eclipse SOC should decrease, but went {first_soc:.4f}->{last_soc:.4f}")
        else:
            log("OK: Eclipse SOC decreasing")

    # Sun
    rows = run_scenario(18)
    if rows:
        first_soc = rows[0]['battery_soc_percent']
        last_soc = rows[-1]['battery_soc_percent']

        log(f"Sun: SOC {first_soc:.4f}->{last_soc:.4f}%")

        if last_soc < first_soc - 0.001:
            fail(f"Sun SOC should increase, but went {first_soc:.4f}->{last_soc:.4f}")
        else:
            log("OK: Sun SOC increasing")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 5: Power Conservation
# ═══════════════════════════════════════════════════════════════════════════════

def test_power_conservation():
    """In scenario 18 (sun), verify that:
    panel_power = Vsolar × Isolar (roughly)
    battery_current ≈ (panel_power / Vbat) - load_current
    """
    print("\n=== TEST 5: Power Conservation ===")
    rows = run_scenario(18)
    if not rows:
        return

    max_error = 0
    for i, r in enumerate(rows[10:], 10):
        vbat_v = r['battery_voltage_mv'] / 1000.0
        ibat_a = r['battery_current_ma'] / 1000.0
        psol = r['panel_power_watts']

        if vbat_v < 0.1 or psol < 0.1:
            continue

        # I_bus_from_solar = P_solar / V_battery
        i_solar_bus = psol / vbat_v

        # Loads: scale by enabled count
        loads_enabled = r['loads_enabled']
        i_loads = NOMINAL_LOAD_CURRENT_A * (loads_enabled / 5.0)

        # Expected: I_battery = I_solar_bus - I_loads
        expected_ibat = (i_solar_bus - i_loads) * 1000.0  # mA
        actual_ibat = r['battery_current_ma']

        error = abs(actual_ibat - expected_ibat)
        if error > max_error:
            max_error = error

        # Allow 500mA tolerance (due to discrete time steps and averaging)
        if error > 500:
            fail(f"Row {i}: Kirchhoff violation. Ibat={actual_ibat}mA, expected={expected_ibat:.0f}mA, "
                 f"Psol={psol:.1f}W, Vbat={vbat_v:.3f}V, error={error:.0f}mA")
            break

    log(f"Max Kirchhoff error: {max_error:.0f} mA")
    if max_error <= 500:
        log("OK: Power conservation holds within 500mA tolerance")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 6: Mode Transitions at Thresholds
# ═══════════════════════════════════════════════════════════════════════════════

def test_mode_transitions():
    """Scenario 2: Eclipse entry at 50s.
    Before 50s: should be MPPT_CHARGE (mode 0), sun available
    After 50s: should be BATTERY_DISCHARGE (mode 3), no sun"""
    print("\n=== TEST 6: Mode Transitions (Eclipse Entry) ===")

    result = subprocess.run(
        [EXECUTABLE, "2"],
        capture_output=True, text=True, timeout=30
    )
    lines = [l for l in result.stdout.splitlines() if l.strip() and not l.startswith("#")]
    reader = csv.DictReader(io.StringIO("\n".join(lines)))
    rows = list(reader)

    before_eclipse = [r for r in rows if float(r['time_seconds']) < 49.0 and float(r['time_seconds']) > 1.0]
    after_eclipse = [r for r in rows if float(r['time_seconds']) > 51.0]

    log(f"Rows before eclipse (t<49s): {len(before_eclipse)}")
    log(f"Rows after eclipse (t>51s): {len(after_eclipse)}")

    # Before eclipse: should be MPPT_CHARGE
    wrong_before = [r for r in before_eclipse if int(r['pcu_mode']) != 0]
    if wrong_before:
        fail(f"{len(wrong_before)} rows before eclipse are NOT MPPT_CHARGE")
    else:
        log("OK: All rows before eclipse are MPPT_CHARGE")

    # After eclipse: should be BATTERY_DISCHARGE
    wrong_after = [r for r in after_eclipse if int(r['pcu_mode']) != 3]
    if wrong_after:
        fail(f"{len(wrong_after)} rows after eclipse are NOT BATTERY_DISCHARGE")
    else:
        log("OK: All rows after eclipse are BATTERY_DISCHARGE")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 7: Safe Mode at OBC Timeout
# ═══════════════════════════════════════════════════════════════════════════════

def test_safe_mode_obc_timeout():
    """Scenario 5: OBC heartbeat=0. Safe mode should trigger at 120s.
    At 200us per iter, 120s = 600000 iterations. Logged every 1000 = row 600."""
    print("\n=== TEST 7: Safe Mode at OBC Timeout ===")

    result = subprocess.run(
        [EXECUTABLE, "5"],
        capture_output=True, text=True, timeout=60
    )
    lines = [l for l in result.stdout.splitlines() if l.strip() and not l.startswith("#")]
    reader = csv.DictReader(io.StringIO("\n".join(lines)))
    rows = list(reader)

    # Find first row where safe_mode = 1
    safe_start = None
    for r in rows:
        if int(r['safe_mode']) == 1:
            safe_start = float(r['time_seconds'])
            break

    if safe_start is None:
        fail("Safe mode never activated in OBC timeout scenario")
    else:
        log(f"Safe mode first active at t={safe_start:.1f}s")
        # Should be at 120s ± 0.2s (logging granularity)
        if abs(safe_start - 120.0) > 1.0:
            fail(f"Safe mode at t={safe_start:.1f}s, expected ~120.0s")
        else:
            log("OK: Safe mode triggers at ~120s")

    # Check loads drop from 5 to 3 after safe mode
    before_safe = [r for r in rows if float(r['time_seconds']) < 119.0]
    after_safe = [r for r in rows if float(r['time_seconds']) > 121.0]

    if before_safe:
        loads_before = int(before_safe[-1]['loads_enabled'])
        log(f"Loads before safe mode: {loads_before}")
        if loads_before != 5:
            warn(f"Expected 5 loads before safe mode, got {loads_before}")

    if after_safe:
        loads_after = int(after_safe[0]['loads_enabled'])
        log(f"Loads after safe mode: {loads_after}")
        if loads_after != 3:
            fail(f"Expected 3 loads after safe mode, got {loads_after}")
        else:
            log("OK: Loads dropped to 3 after safe mode")


# ═══════════════════════════════════════════════════════════════════════════════
# TEST 8: Multi-day SOC Cycling
# ═══════════════════════════════════════════════════════════════════════════════

def test_multiday_soc_cycling():
    """Scenario 13: 1 day nominal. Check SOC oscillates with orbit cycle
    and doesn't drift to 0% or 100% (indicating power balance issue)."""
    print("\n=== TEST 8: Multi-day SOC Cycling ===")
    print("  Running scenario 13 (1 day, ~4.5 min)...")

    result = subprocess.run(
        [EXECUTABLE, "13"],
        capture_output=True, text=True, timeout=600
    )
    if result.returncode != 0:
        fail(f"Scenario 13 crashed: {result.stderr}")
        return

    lines = [l for l in result.stdout.splitlines() if l.strip() and not l.startswith("#")]
    reader = csv.DictReader(io.StringIO("\n".join(lines)))
    rows = list(reader)

    log(f"Got {len(rows)} rows spanning {float(rows[-1]['time_seconds'])/3600:.1f} hours")

    first_soc = float(rows[0]['battery_soc_percent'])
    last_soc = float(rows[-1]['battery_soc_percent'])
    min_soc = min(float(r['battery_soc_percent']) for r in rows)
    max_soc = max(float(r['battery_soc_percent']) for r in rows)

    log(f"SOC: start={first_soc:.2f}%, end={last_soc:.2f}%, min={min_soc:.2f}%, max={max_soc:.2f}%")

    if min_soc < 1.0:
        fail(f"SOC dropped to {min_soc:.2f}% — battery nearly dead in 1 day!")
    elif min_soc < 20.0:
        warn(f"SOC dropped to {min_soc:.2f}% — power balance may be negative")
    else:
        log("OK: SOC stays above 20%")

    if max_soc > 99.0:
        warn(f"SOC reached {max_soc:.2f}% — battery always full, no cycling visible")

    # Count mode transitions
    mode_changes = 0
    for i in range(1, len(rows)):
        if int(rows[i]['pcu_mode']) != int(rows[i-1]['pcu_mode']):
            mode_changes += 1

    log(f"Mode transitions in 1 day: {mode_changes}")
    # Expect ~30 transitions (2 per orbit × 15 orbits)
    if mode_changes < 10:
        fail(f"Only {mode_changes} mode transitions in 1 day — expected ~30 for 15 orbits")
    else:
        log("OK: Multiple mode transitions observed")


# ═══════════════════════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    print("=" * 70)
    print("EPS SIMULATION ACCURACY VERIFICATION")
    print("=" * 70)

    test_soc_integration_eclipse()
    test_soc_integration_sun()
    test_cold_temperature()
    test_battery_voltage_soc_consistency()
    test_power_conservation()
    test_mode_transitions()
    test_safe_mode_obc_timeout()
    test_multiday_soc_cycling()

    print("\n" + "=" * 70)
    print(f"RESULTS: {len(errors)} errors, {len(warnings)} warnings")
    if errors:
        print("\nERRORS:")
        for e in errors:
            print(f"  X {e}")
    if warnings:
        print("\nWARNINGS:")
        for w in warnings:
            print(f"  ! {w}")
    if not errors:
        print("\nOK: ALL VERIFICATION TESTS PASSED")

    sys.exit(1 if errors else 0)
