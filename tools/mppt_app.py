"""
CHESS EPS — MPPT Incremental Conductance Simulation
Streamlit visualization app for the MPPT algorithm simulation.

Runs the C simulation executable with user-selected parameters,
parses the CSV output, and displays interactive plots showing
the algorithm's convergence behavior.

Usage:
    pip install -r requirements.txt
    streamlit run tools/mppt_app.py
"""

import subprocess
import os
import io

import streamlit as st
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

# ── Page config ───────────────────────────────────────────────────────────────

st.set_page_config(
    page_title="CHESS EPS — MPPT Simulation",
    layout="wide",
    initial_sidebar_state="expanded",
)

st.title("CHESS EPS — MPPT Incremental Conductance Simulation")

# ── Find the simulation executable ────────────────────────────────────────────

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
EXECUTABLE = os.path.join(PROJECT_ROOT, "tests", "run_mppt_simulation")

# On Windows, add .exe
if os.name == "nt" and not EXECUTABLE.endswith(".exe"):
    EXECUTABLE += ".exe"

if not os.path.exists(EXECUTABLE):
    st.error(
        f"Simulation executable not found at: {EXECUTABLE}\n\n"
        "Build it first with: `make -f Makefile.sim`"
    )
    st.stop()

# ── Sidebar controls ──────────────────────────────────────────────────────────

st.sidebar.header("Simulation Controls")

scenario = st.sidebar.selectbox(
    "Scenario",
    options=[1, 2, 3, 4, 5, 6],
    format_func=lambda x: {
        1: "1: Steady state, 100% irradiance, 25C",
        2: "2: Steady state, 50% irradiance, 25C",
        3: "3: Steady state, 100% irradiance, 80C",
        4: "4: Step change 100% -> 50% irradiance",
        5: "5: Temperature ramp 25C -> 80C",
        6: "6: Eclipse entry (irradiance -> 0)",
    }[x],
)

panel_config = st.sidebar.selectbox(
    "Panel Configuration",
    options=["4p", "2s2p"],
    format_func=lambda x: {
        "4p": "4P — All 4 panels in parallel (likely correct)",
        "2s2p": "2S2P — 2 series, 2 parallel (per document text)",
    }[x],
)

battery_voltage = st.sidebar.slider(
    "Battery Voltage (V)",
    min_value=6.0,
    max_value=8.4,
    value=7.4,
    step=0.1,
    help="2S Li-ion battery: 6.0V (empty) to 8.4V (full)",
)

st.sidebar.markdown("---")
run_button = st.sidebar.button("Run Simulation", type="primary", use_container_width=True)

# ── Disclaimer ────────────────────────────────────────────────────────────────

st.warning(
    "**Panel electrical configuration (4P vs 2S2P) is unverified.** "
    "The CHESS document says '2x2 (two parallel, two in series)' but the "
    "system-level voltage of 18.34V only matches a 4P configuration "
    "(7 cells in series x 2.669V/cell = 18.68V). "
    "Verify with the hardware team before relying on these results."
)

# ── Run simulation ────────────────────────────────────────────────────────────


@st.cache_data
def run_simulation(scenario_num, panel_cfg, battery_v):
    """Run the C simulation and return parsed DataFrame + stderr."""
    cmd = [
        EXECUTABLE,
        str(scenario_num),
        panel_cfg,
        f"{battery_v:.1f}",
    ]
    result = subprocess.run(
        cmd, capture_output=True, text=True, timeout=30
    )

    # Parse CSV from stdout (skip comment lines starting with #)
    csv_lines = [
        line for line in result.stdout.splitlines()
        if not line.startswith("#") and line.strip()
    ]
    csv_text = "\n".join(csv_lines)

    df = pd.read_csv(io.StringIO(csv_text))
    return df, result.stderr.strip()


# Auto-run on first load or when button clicked
if run_button or "df" not in st.session_state:
    try:
        df, stderr = run_simulation(scenario, panel_config, battery_voltage)
        st.session_state["df"] = df
        st.session_state["stderr"] = stderr
    except Exception as e:
        st.error(f"Simulation failed: {e}")
        st.stop()

df = st.session_state.get("df")
stderr = st.session_state.get("stderr", "")

if df is None or df.empty:
    st.info("Click 'Run Simulation' to start.")
    st.stop()

# ── Parse verdict from stderr ─────────────────────────────────────────────────

if "PASS" in stderr:
    st.success(stderr)
elif "FAIL" in stderr:
    st.error(stderr)
else:
    st.info(stderr)

# ── Iteration slider ──────────────────────────────────────────────────────────

max_iter = int(df["iteration"].max())
display_up_to = st.slider(
    "Show iterations up to:",
    min_value=1,
    max_value=max_iter,
    value=max_iter,
    step=1,
)

df_display = df[df["iteration"] <= display_up_to]
current_row = df_display.iloc[-1]

# ── Layout: two columns ──────────────────────────────────────────────────────

col_left, col_right = st.columns([1, 2])

# ── Left column: Live values ──────────────────────────────────────────────────

with col_left:
    st.subheader("Live Values")

    st.metric("Panel Voltage", f"{current_row['panel_voltage_volts']:.2f} V")
    st.metric("Panel Current", f"{current_row['panel_current_amps']:.3f} A")
    st.metric("Panel Power", f"{current_row['panel_power_watts']:.2f} W")
    st.metric("Duty Cycle", f"{current_row['duty_cycle_percent']:.1f}%")
    st.metric("Temperature", f"{current_row['temperature_celsius']:.0f} C")
    st.metric("Irradiance", f"{current_row['irradiance_percent']:.0f}%")

    error = current_row["power_error_percent"]
    delta_color = "normal" if abs(error) < 2.0 else "inverse"
    st.metric(
        "Error from MPP",
        f"{abs(error):.1f}%",
        delta=f"{'WITHIN' if abs(error) < 2 else 'OUTSIDE'} 2% target",
        delta_color=delta_color,
    )

    decision = current_row["algorithm_decision"]
    if decision == "INCREASE_D":
        st.info("Decision: INCREASE D (lower voltage)")
    elif decision == "DECREASE_D":
        st.info("Decision: DECREASE D (raise voltage)")
    else:
        st.success("Decision: HOLD (at MPP)")

# ── Right column: Plots ──────────────────────────────────────────────────────

with col_right:
    # ── Power vs Iteration ────────────────────────────────────────────────
    st.subheader("Power vs Iteration")

    fig1, ax1 = plt.subplots(figsize=(10, 4))
    ax1.plot(
        df_display["iteration"],
        df_display["panel_power_watts"],
        color="#2196F3",
        linewidth=0.8,
        label="Algorithm Power",
    )
    ax1.plot(
        df_display["iteration"],
        df_display["theoretical_mpp_power"],
        color="#4CAF50",
        linewidth=1.5,
        linestyle="--",
        label="Theoretical MPP",
    )

    # Shade the 2% band around MPP
    mpp_power = df_display["theoretical_mpp_power"]
    ax1.fill_between(
        df_display["iteration"],
        mpp_power * 0.98,
        mpp_power * 1.02,
        alpha=0.15,
        color="#4CAF50",
        label="2% band",
    )

    ax1.set_xlabel("Iteration")
    ax1.set_ylabel("Power (W)")
    ax1.legend(loc="lower right")
    ax1.grid(True, alpha=0.3)
    ax1.set_xlim(0, max_iter)
    st.pyplot(fig1)
    plt.close(fig1)

    # ── Duty Cycle vs Iteration ───────────────────────────────────────────
    st.subheader("Duty Cycle vs Iteration")

    fig2, ax2 = plt.subplots(figsize=(10, 3))
    ax2.plot(
        df_display["iteration"],
        df_display["duty_cycle_percent"],
        color="#FF9800",
        linewidth=0.8,
    )
    ax2.set_xlabel("Iteration")
    ax2.set_ylabel("Duty Cycle (%)")
    ax2.grid(True, alpha=0.3)
    ax2.set_xlim(0, max_iter)
    st.pyplot(fig2)
    plt.close(fig2)

    # ── Voltage and Current vs Iteration ──────────────────────────────────
    st.subheader("Voltage & Current vs Iteration")

    fig3, (ax3a, ax3b) = plt.subplots(2, 1, figsize=(10, 5), sharex=True)

    ax3a.plot(
        df_display["iteration"],
        df_display["panel_voltage_volts"],
        color="#9C27B0",
        linewidth=0.8,
        label="Panel Voltage",
    )
    ax3a.plot(
        df_display["iteration"],
        df_display["theoretical_mpp_voltage"],
        color="#4CAF50",
        linewidth=1.5,
        linestyle="--",
        label="MPP Voltage",
    )
    ax3a.set_ylabel("Voltage (V)")
    ax3a.legend(loc="upper right")
    ax3a.grid(True, alpha=0.3)

    ax3b.plot(
        df_display["iteration"],
        df_display["panel_current_amps"],
        color="#E91E63",
        linewidth=0.8,
        label="Panel Current",
    )
    ax3b.plot(
        df_display["iteration"],
        df_display["theoretical_mpp_current"],
        color="#4CAF50",
        linewidth=1.5,
        linestyle="--",
        label="MPP Current",
    )
    ax3b.set_xlabel("Iteration")
    ax3b.set_ylabel("Current (A)")
    ax3b.legend(loc="upper right")
    ax3b.grid(True, alpha=0.3)
    ax3b.set_xlim(0, max_iter)

    st.pyplot(fig3)
    plt.close(fig3)

# ── Hypotheses & Sources ──────────────────────────────────────────────────────

st.markdown("---")
st.header("Hypotheses & Sources")

with st.expander("What is a solar cell?"):
    st.markdown("""
A solar cell is a semiconductor device (a special type of electronic component)
that converts light into electricity.

When a photon (a particle of light) hits the cell with enough energy, it knocks
an electron free from the crystal lattice of the semiconductor material. This
free electron can flow through a circuit as electrical current.

The cell is essentially a diode (a one-way valve for electricity) that has been
engineered to produce current when illuminated. In the dark, it behaves like a
normal diode. In sunlight, the photon-generated current flows out of the cell.

**The CHESS solar cells** are **triple-junction GaAs** (Gallium Arsenide) cells
made by **Azur Space** (model 3G30C). "Triple-junction" means three layers of
different semiconductor materials are stacked on top of each other, each optimized
to absorb a different range of light wavelengths. This gives higher efficiency
(~29%) than single-layer cells (~20%).
    """)

with st.expander("Why does the I-V curve have that shape?"):
    st.markdown("""
The relationship between a solar cell's voltage (V) and current (I) follows
the **single-diode equation**:

```
I = Iph - Isat * (exp((V + I*Rs) / (n*Ns*Vt)) - 1) - (V + I*Rs) / Rsh
```

In plain English:
- **Iph** (photocurrent): the current generated by light. More light = more Iph.
- **Isat** (saturation current): a tiny leakage current through the diode.
  Increases dramatically with temperature.
- **exp(...)**: the exponential diode characteristic. As voltage increases, the
  diode "turns on" and starts consuming photocurrent internally.
- **Rs** (series resistance): internal resistance of the cell's wiring/contacts.
- **Rsh** (shunt resistance): parasitic leakage paths within the cell.

The result is a curve that starts flat (at short-circuit current Isc), stays
mostly flat as voltage increases, then drops sharply near the open-circuit
voltage (Voc) as the diode turns on.

**Power = V * I**. Since current drops sharply near Voc, there's a single peak
in the power curve — this is the **Maximum Power Point (MPP)**.

**Source:** Azur Space 3G30C datasheet, page 2, "Electrical Data" table.
- Look for: `Voc typ = 2669 mV` (open-circuit voltage per cell)
- Look for: `Isc typ = 525 mA` (short-circuit current per cell)
- [Datasheet link](https://www.azurspace.com/media/uploads/file_links/file/bdb_00010891-01-00_tj3g30-advanced_4x8.pdf)
    """)

with st.expander("How does the buck converter change the operating point?"):
    st.markdown("""
The **buck converter** is like a gear ratio for electricity. It steps voltage
down while stepping current up (conserving power, minus losses).

The key equation for an ideal buck converter:

```
V_out = D * V_in
```

Where **D** is the duty cycle (0% to 100%) — the fraction of time the switch
is ON.

In the CHESS EPS, V_out is connected to the battery, which acts like a
fixed voltage source (its voltage changes very slowly as it charges). So:

```
V_panel = V_battery / D
```

**This is how the MCU controls the solar panel's operating point:**
- Increase D → decrease V_panel → panel operates at lower voltage, higher current
- Decrease D → increase V_panel → panel operates at higher voltage, lower current

The MPPT algorithm's job is to find the duty cycle D that makes the panel
operate at exactly the Maximum Power Point.

**Hardware:** EPC2152 GaN half-bridge, 300 kHz switching frequency.
**Source:** CHESS mission document, section 3.4.2.2.1, page 96.
    """)

with st.expander("What is Incremental Conductance?"):
    st.markdown("""
Imagine you're blindfolded on a hill, trying to find the top. You can't see,
but you can feel the slope under your feet.

- If the ground slopes upward in front of you → keep walking forward
- If the ground slopes downward → you've gone past the top, step back
- If the ground is flat → you're at the top!

**Incremental Conductance** works the same way with the power curve:
- It measures the **slope** of the power curve (dP/dV)
- If dP/dV > 0: power is increasing with voltage → we're LEFT of the peak → increase voltage
- If dP/dV < 0: power is decreasing → we're RIGHT of the peak → decrease voltage
- If dP/dV = 0: we're AT the peak → hold steady

The clever part: instead of computing dP/dV directly (which requires division),
the algorithm uses a mathematical trick called **cross-multiplication** to
compare ΔI/ΔV with -I/V without any division. This matters because the
SAMD21RT microcontroller has no hardware divider.

**Source:** CHESS mission document, section 3.4.2.2.2, page 97.
    """)

with st.expander("Temperature effects on the solar panel"):
    st.markdown("""
When a solar cell gets **hotter**:
- **Voltage decreases significantly** (about -6 mV per degree C per cell)
- **Current increases slightly** (about +0.35 mA per degree C per cell)
- **Net effect: less power** (voltage drop dominates)

For the CHESS array (7 cells in series, 4P):
- At 25C: Voc = 18.7V, Isc = 2.1A, Pmpp = ~37W
- At 80C: Voc = ~16.5V, Isc = ~2.2A, Pmpp = ~30W

This means the **MPP shifts to lower voltage** when the panel gets hot.
The algorithm must track this shift — otherwise it operates at the wrong
point and wastes power.

In orbit, solar panels can reach 80-100C in direct sunlight and drop to
-40C in eclipse. These temperature swings happen every 94-minute orbit.

**Source:** Azur Space 3G30C datasheet, page 2, "Temperature Coefficients":
- dVoc/dT = -6.0 mV/C per cell
- dIsc/dT = +0.35 mA/C per cell
    """)

with st.expander("Panel Configuration Analysis (4P vs 2S2P)"):
    st.markdown("""
**The discrepancy:**

The CHESS document (section 3.4.6.2, page 114) says the array configuration
is "2x2 (two solar panels in parallel, two in series)". But the system-level
parameters in Table 3.4.1 (page 94) show a maximum input voltage of 18.34V.

**The math:**

Using Azur Space 3G30C cells with Voc = 2.669V per cell:

| Configuration | Voc (V) | Isc (A) | Matches 18.34V? |
|---|---|---|---|
| **4P** (4 panels parallel, 7 cells series) | 7 x 2.669 = **18.68** | 4 x 0.525 = **2.10** | Yes |
| **2S2P** (14 cells series, 2 strings parallel) | 14 x 2.669 = **37.37** | 2 x 0.525 = **1.05** | No |

**Conclusion:** The electrical configuration is almost certainly **4P**. The
"2x2" description in the document likely refers to the physical/mechanical
flower arrangement, not the electrical wiring. Or the document has an error.

**Action required:** Verify with the hardware team (Loris Bregy) or check
the actual PCB schematic.
    """)

# ── Raw data ──────────────────────────────────────────────────────────────────

with st.expander("Raw CSV Data"):
    st.dataframe(df_display, use_container_width=True, height=400)
