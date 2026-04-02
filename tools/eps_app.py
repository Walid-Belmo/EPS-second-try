"""
CHESS EPS — State Machine Simulation Visualization
Streamlit app for the EPS state machine closed-loop simulation.

Runs the C simulation executable with user-selected scenarios,
parses the CSV output, and displays interactive plots showing
PCU mode transitions, battery state, load shedding, thermal control,
and safe mode behavior.

Usage:
    pip install -r requirements.txt
    streamlit run tools/eps_app.py
"""

import subprocess
import os
import io

import streamlit as st
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

# ── Page config ──────────────────────────────────────────────────────────────

st.set_page_config(
    page_title="CHESS EPS — State Machine Simulation",
    layout="wide",
    initial_sidebar_state="expanded",
)

st.title("CHESS EPS — State Machine Simulation")
st.caption("Closed-loop simulation of the EPS power management state machine")

# ── Find the simulation executable ───────────────────────────────────────────

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
EXECUTABLE = os.path.join(PROJECT_ROOT, "tests", "run_eps_simulation")

if os.name == "nt" and not EXECUTABLE.endswith(".exe"):
    EXECUTABLE += ".exe"

if not os.path.exists(EXECUTABLE):
    st.error(
        f"Simulation executable not found at: {EXECUTABLE}\n\n"
        "Build it first with: `make -f Makefile.eps_sim`"
    )
    st.stop()

# ── PCU mode names and colors ────────────────────────────────────────────────

PCU_MODE_NAMES = {
    0: "MPPT_CHARGE",
    1: "CV_FLOAT",
    2: "SA_LOAD_FOLLOW",
    3: "BATTERY_DISCHARGE",
}

PCU_MODE_COLORS = {
    0: "#4CAF50",  # green
    1: "#2196F3",  # blue
    2: "#FF9800",  # orange
    3: "#F44336",  # red
}

# ── Sidebar controls ─────────────────────────────────────────────────────────

st.sidebar.header("Simulation Controls")

SCENARIO_DESCRIPTIONS = {
    1: "1: Full sun, battery 50% SOC — normal charging",
    2: "2: Eclipse entry — sun disappears at 50s",
    3: "3: Eclipse exit — sun appears at 50s",
    4: "4: Battery critically low — safe mode trigger",
    5: "5: OBC heartbeat lost — 120s autonomy timeout",
    6: "6: Cold temperature (-20C) — heater + charging forbidden",
    7: "7: Eclipse with high load — overcurrent shedding",
    8: "8: Full orbit (57 min sun + 37 min eclipse) x2",
}

scenario = st.sidebar.selectbox(
    "Scenario",
    options=list(SCENARIO_DESCRIPTIONS.keys()),
    format_func=lambda x: SCENARIO_DESCRIPTIONS[x],
)

st.sidebar.markdown("---")
run_button = st.sidebar.button(
    "Run Simulation", type="primary", use_container_width=True
)

# ── Run simulation ───────────────────────────────────────────────────────────


@st.cache_data
def run_eps_simulation(scenario_num):
    """Run the C simulation and return parsed DataFrame + stderr."""
    cmd = [EXECUTABLE, str(scenario_num)]
    timeout = 120 if scenario_num == 8 else 30
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)

    csv_lines = [
        line
        for line in result.stdout.splitlines()
        if not line.startswith("#") and line.strip()
    ]
    csv_text = "\n".join(csv_lines)

    df = pd.read_csv(io.StringIO(csv_text))
    return df, result.stderr.strip()


if run_button or "eps_df" not in st.session_state:
    try:
        with st.spinner(f"Running scenario {scenario}..."):
            df, stderr = run_eps_simulation(scenario)
        st.session_state["eps_df"] = df
        st.session_state["eps_stderr"] = stderr
        st.session_state["eps_scenario"] = scenario
    except subprocess.TimeoutExpired:
        st.error("Simulation timed out. Scenario 8 may take longer.")
        st.stop()
    except Exception as e:
        st.error(f"Simulation failed: {e}")
        st.stop()

df = st.session_state.get("eps_df")
stderr = st.session_state.get("eps_stderr", "")

if df is None or df.empty:
    st.info("Click 'Run Simulation' to start.")
    st.stop()

st.success(stderr)

# ── Time range slider ────────────────────────────────────────────────────────

max_time = float(df["time_seconds"].max())
time_range = st.slider(
    "Time range (seconds)",
    min_value=0.0,
    max_value=max_time,
    value=(0.0, max_time),
    step=max(0.1, max_time / 1000),
)

df_display = df[
    (df["time_seconds"] >= time_range[0]) & (df["time_seconds"] <= time_range[1])
]
if df_display.empty:
    st.warning("No data in selected range.")
    st.stop()

current_row = df_display.iloc[-1]

# ── Top metrics row ──────────────────────────────────────────────────────────

st.markdown("---")
col1, col2, col3, col4, col5, col6 = st.columns(6)

mode_name = PCU_MODE_NAMES.get(int(current_row["pcu_mode"]), "UNKNOWN")
with col1:
    st.metric("PCU Mode", mode_name)
with col2:
    safe = "YES" if int(current_row["safe_mode"]) == 1 else "No"
    st.metric("Safe Mode", safe)
with col3:
    st.metric("Battery", f"{current_row['battery_voltage_mv']} mV")
with col4:
    st.metric("SOC", f"{current_row['battery_soc_percent']:.1f}%")
with col5:
    st.metric("Duty Cycle", f"{current_row['duty_cycle_percent']:.1f}%")
with col6:
    heater = "ON" if int(current_row["heater_on"]) == 1 else "OFF"
    loads = int(current_row["loads_enabled"])
    st.metric("Heater / Loads", f"{heater} / {loads}/5")

# ── Plot 1: PCU Mode Timeline ───────────────────────────────────────────────

st.subheader("PCU Mode Over Time")

fig1, ax1 = plt.subplots(figsize=(14, 2.5))

# Color the background by mode
times = df_display["time_seconds"].values
modes = df_display["pcu_mode"].values

for i in range(len(times) - 1):
    mode = int(modes[i])
    color = PCU_MODE_COLORS.get(mode, "#999999")
    ax1.axvspan(times[i], times[i + 1], alpha=0.6, color=color, linewidth=0)

# Legend
patches = [
    mpatches.Patch(color=c, label=n, alpha=0.6)
    for n, c in zip(
        ["MPPT_CHARGE", "CV_FLOAT", "SA_LOAD_FOLLOW", "BATTERY_DISCHARGE"],
        ["#4CAF50", "#2196F3", "#FF9800", "#F44336"],
    )
]
ax1.legend(handles=patches, loc="upper right", fontsize=8, ncol=4)
ax1.set_xlabel("Time (seconds)")
ax1.set_yticks([])
ax1.set_xlim(time_range)
ax1.set_title("PCU Operating Mode")
st.pyplot(fig1)
plt.close(fig1)

# ── Plot 2: Battery Voltage and SOC ─────────────────────────────────────────

st.subheader("Battery State")

fig2, (ax2a, ax2b) = plt.subplots(2, 1, figsize=(14, 6), sharex=True)

ax2a.plot(
    df_display["time_seconds"],
    df_display["battery_voltage_mv"],
    color="#9C27B0",
    linewidth=0.8,
)
ax2a.set_ylabel("Battery Voltage (mV)")
ax2a.axhline(y=8400, color="red", linestyle="--", linewidth=0.8, label="Vbat_max (8400)")
ax2a.axhline(y=5000, color="orange", linestyle="--", linewidth=0.8, label="Vbat_min (5000)")
ax2a.legend(fontsize=8)
ax2a.grid(True, alpha=0.3)

ax2b.plot(
    df_display["time_seconds"],
    df_display["battery_soc_percent"],
    color="#2196F3",
    linewidth=0.8,
)
ax2b.set_xlabel("Time (seconds)")
ax2b.set_ylabel("State of Charge (%)")
ax2b.set_ylim(0, 100)
ax2b.grid(True, alpha=0.3)
ax2b.set_xlim(time_range)

st.pyplot(fig2)
plt.close(fig2)

# ── Plot 3: Duty Cycle and Solar Power ───────────────────────────────────────

st.subheader("Duty Cycle & Solar Power")

fig3, (ax3a, ax3b) = plt.subplots(2, 1, figsize=(14, 6), sharex=True)

ax3a.plot(
    df_display["time_seconds"],
    df_display["duty_cycle_percent"],
    color="#FF9800",
    linewidth=0.8,
)
ax3a.set_ylabel("Duty Cycle (%)")
ax3a.set_ylim(0, 100)
ax3a.grid(True, alpha=0.3)

ax3b.plot(
    df_display["time_seconds"],
    df_display["panel_power_watts"],
    color="#4CAF50",
    linewidth=0.8,
)
ax3b.set_xlabel("Time (seconds)")
ax3b.set_ylabel("Panel Power (W)")
ax3b.grid(True, alpha=0.3)
ax3b.set_xlim(time_range)

st.pyplot(fig3)
plt.close(fig3)

# ── Plot 4: Battery Current ──────────────────────────────────────────────────

st.subheader("Battery Current")

fig4, ax4 = plt.subplots(figsize=(14, 3.5))

ax4.plot(
    df_display["time_seconds"],
    df_display["battery_current_ma"],
    color="#E91E63",
    linewidth=0.8,
)
ax4.axhline(y=0, color="gray", linestyle="-", linewidth=0.5)
ax4.set_xlabel("Time (seconds)")
ax4.set_ylabel("Battery Current (mA)")
ax4.grid(True, alpha=0.3)
ax4.set_xlim(time_range)

# Annotate charging vs discharging
ymin, ymax = ax4.get_ylim()
if ymax > 0:
    ax4.fill_between(
        df_display["time_seconds"],
        0,
        df_display["battery_current_ma"].clip(lower=0),
        alpha=0.1,
        color="green",
        label="Charging",
    )
if ymin < 0:
    ax4.fill_between(
        df_display["time_seconds"],
        df_display["battery_current_ma"].clip(upper=0),
        0,
        alpha=0.1,
        color="red",
        label="Discharging",
    )
ax4.legend(fontsize=8)

st.pyplot(fig4)
plt.close(fig4)

# ── Plot 5: Load Shedding and Safe Mode ──────────────────────────────────────

st.subheader("Load Shedding & Safe Mode")

fig5, (ax5a, ax5b) = plt.subplots(2, 1, figsize=(14, 4), sharex=True)

ax5a.plot(
    df_display["time_seconds"],
    df_display["loads_enabled"],
    color="#795548",
    linewidth=1.0,
    drawstyle="steps-post",
)
ax5a.set_ylabel("Loads Enabled (of 5)")
ax5a.set_ylim(-0.5, 5.5)
ax5a.set_yticks([0, 1, 2, 3, 4, 5])
ax5a.grid(True, alpha=0.3)

ax5b.fill_between(
    df_display["time_seconds"],
    0,
    df_display["safe_mode"],
    alpha=0.4,
    color="#F44336",
    step="post",
)
ax5b.set_xlabel("Time (seconds)")
ax5b.set_ylabel("Safe Mode")
ax5b.set_ylim(-0.1, 1.5)
ax5b.set_yticks([0, 1])
ax5b.set_yticklabels(["Normal", "SAFE"])
ax5b.grid(True, alpha=0.3)
ax5b.set_xlim(time_range)

st.pyplot(fig5)
plt.close(fig5)

# ── Plot 6: Temperature and Heater ───────────────────────────────────────────

st.subheader("Temperature & Heater")

fig6, ax6 = plt.subplots(figsize=(14, 3))

temp_celsius = df_display["temperature_decideg"] / 10.0
ax6.plot(
    df_display["time_seconds"],
    temp_celsius,
    color="#FF5722",
    linewidth=0.8,
    label="Battery Temp",
)
ax6.axhline(y=0, color="blue", linestyle="--", linewidth=0.8, label="Charging forbidden below 0C")
ax6.axhline(y=-10, color="cyan", linestyle="--", linewidth=0.8, label="Heater ON below -10C")

# Shade heater active regions
heater_on = df_display["heater_on"].values
for i in range(len(times) - 1):
    if heater_on[i] == 1:
        ax6.axvspan(times[i], times[i + 1], alpha=0.15, color="orange", linewidth=0)

ax6.set_xlabel("Time (seconds)")
ax6.set_ylabel("Temperature (C)")
ax6.legend(fontsize=8)
ax6.grid(True, alpha=0.3)
ax6.set_xlim(time_range)

st.pyplot(fig6)
plt.close(fig6)

# ── Scenario explanation ─────────────────────────────────────────────────────

st.markdown("---")
st.header("Scenario Details")

scenario_num = st.session_state.get("eps_scenario", 1)

SCENARIO_EXPLANATIONS = {
    1: """
**Scenario 1: Normal Charging (Full Sun, 50% SOC)**

Initial conditions: Full irradiance, battery at 50% state of charge, 25C.
Starting mode: MPPT_CHARGE.

Expected behavior:
- MPPT algorithm tracks the solar panel maximum power point
- Battery charges at ~3A, SOC gradually rises
- When battery reaches Vbat_full (8300 mV), transitions to SA_LOAD_FOLLOW
- Duty cycle converges to ~45% (near the MPP)
""",
    2: """
**Scenario 2: Eclipse Entry**

Initial conditions: Full sun, battery at 70% SOC, 25C.
Sun disappears at t=50s (iteration 250000).

Expected behavior:
- Initially in MPPT_CHARGE, actively charging
- At t=50s: solar voltage drops to 0 → transitions to BATTERY_DISCHARGE
- Panel eFuse opens (disconnects solar array)
- Battery current goes negative (discharging to power loads)
- SOC begins declining
""",
    3: """
**Scenario 3: Eclipse Exit**

Initial conditions: No sun (eclipse), battery at 70% SOC.
Sun appears at t=50s.

Expected behavior:
- Initially in BATTERY_DISCHARGE, battery powering everything
- At t=50s: solar voltage rises → transitions to MPPT_CHARGE
- Panel eFuse closes, MPPT algorithm starts tracking
- Battery current goes positive (charging resumes)
""",
    4: """
**Scenario 4: Battery Critically Low**

Initial conditions: Full sun, battery at only 5% SOC.

Expected behavior:
- Battery voltage is near/below Bmin (5000 mV)
- Safe mode triggers immediately (battery below minimum)
- Load shedding activates (SPAD Camera and GNSS shed)
- MPPT still charges the battery to recover
""",
    5: """
**Scenario 5: OBC Heartbeat Lost**

Initial conditions: Full sun, battery at 70% SOC. OBC heartbeat = 0 every iteration.

Expected behavior:
- Normal MPPT_CHARGE operation for first 120 seconds
- At exactly t=120s: OBC heartbeat timeout triggers
- Safe mode activates, loads drop from 5 to 3
- EPS continues charging autonomously
""",
    6: """
**Scenario 6: Cold Temperature (-20C)**

Initial conditions: Full sun, battery at 70% SOC, temperature = -20C.

Expected behavior:
- Temperature below TempMin (-10C) → heater ON
- Temperature below 0C → charging FORBIDDEN (duty cycle forced to minimum 5%)
- Safe mode triggers (temperature out of range)
- Load shedding active
""",
    7: """
**Scenario 7: Eclipse with Overcurrent**

Initial conditions: No sun, battery at 30% SOC.

Expected behavior:
- BATTERY_DISCHARGE mode, battery powering everything
- If discharge current exceeds Ibat_max → load shedding triggers
- Loads shed one at a time until current is within limits
""",
    8: """
**Scenario 8: Full Orbit Cycle (2 orbits)**

Initial conditions: Starting in eclipse, battery at 70% SOC.
Orbit: 57 minutes sun + 37 minutes eclipse = 94 minutes, repeated twice.

Expected behavior:
- Alternating MPPT_CHARGE (sunlit) ↔ BATTERY_DISCHARGE (eclipse)
- SOC rises during sun, falls during eclipse
- Net SOC change over 2 orbits shows power balance
- ~28 million iterations per orbit, runs in seconds on PC
""",
}

st.markdown(SCENARIO_EXPLANATIONS.get(scenario_num, ""))

# ── Architecture explanation ─────────────────────────────────────────────────

with st.expander("How the simulation works"):
    st.markdown("""
### Closed-Loop Architecture

The simulation runs the **exact same C firmware code** that will run on the
SAMD21 MCU. The only difference is where sensor readings come from (physics
models vs real ADC) and where commands go (CSV log vs real PWM register).

Each iteration:
1. **Battery model** computes terminal voltage from SOC and previous current
2. **Buck converter model** computes panel voltage from duty cycle: `V_panel = V_battery / D`
3. **Solar panel model** computes panel current from voltage and irradiance (Newton-Raphson on 5-parameter diode equation)
4. **Power balance**: `I_battery = P_panel/V_battery - I_loads`
5. **SOC update**: `SOC += I_battery × dt / (capacity × 3600)`
6. Convert float values to integer millivolts/milliamps and 12-bit ADC readings
7. Call `eps_state_machine_run_one_iteration()` — the firmware
8. Extract new duty cycle → feeds back to step 2

**Time step**: 200 µs per iteration (matching expected MCU superloop rate at 48 MHz).
A 94-minute orbit = ~28 million iterations = a few seconds on PC.
""")

with st.expander("PCU Operating Modes"):
    st.markdown("""
| Mode | When | What it does |
|------|------|-------------|
| **MPPT_CHARGE** | Sun + battery not full | Track Maximum Power Point, charge battery |
| **CV_FLOAT** | Sun + battery full | Hold voltage constant at Vbat_max |
| **SA_LOAD_FOLLOW** | Sun + battery full + no charge needed | Follow load demand |
| **BATTERY_DISCHARGE** | Eclipse (no sun) | Battery powers everything, load shed if needed |

Source: CHESS Mission Document, Section 3.4.2.2.4, Figures 3.4.6-3.4.10.
""")

# ── Raw data ─────────────────────────────────────────────────────────────────

with st.expander("Raw CSV Data"):
    st.dataframe(df_display, use_container_width=True, height=400)
