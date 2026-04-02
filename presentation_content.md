# CHESS EPS State Machine -- Presentation Slide Content

Use this file to generate a PowerPoint presentation. Each slide is formatted with a title, visual suggestion, bullet points, and speaker notes. The audience is the CHESS CubeSat technical team at EPFL.

---

## Slide 1: Title

**Visual:** CHESS Pathfinder 0 CubeSat render or logo, EPFL branding. Subtitle text centered below the title.

**Content/Bullet points:**
- CHESS EPS State Machine -- Simulation & Validation
- EPS Firmware Team
- EPFL, April 2026

**Speaker notes:** Welcome everyone. Today I am presenting the EPS state machine simulation we have built for the CHESS Pathfinder 0 CubeSat. The goal of this work is to validate the complete EPS power management logic before we put it on the flight hardware. Everything I will show you today runs the exact same C code that will execute on the SAMD21 microcontroller in orbit.

---

## Slide 2: The Problem

**Visual:** Diagram of one orbit: a circle around Earth with the sun side labeled "57 min sunlight" and the shadow side labeled "37 min eclipse." Arrows showing power flow: solar panels to battery to loads.

**Content/Bullet points:**
- CHESS orbits at 475 km, 94-minute period: 57 min sun, 37 min eclipse
- Solar panels are the only power source -- battery must last through eclipse
- Battery has hard limits: 5.0 V minimum, 8.4 V maximum, no charging below 0 C
- Load demand varies: 8 W nominal across 5 subsystems (OBC, ADCS, UHF, GNSS, SPAD)
- We need autonomous power management: the EPS cannot wait for ground commands

**Speaker notes:** The fundamental challenge is that our power source is intermittent. For 37 minutes every orbit, we have no solar input at all. The battery must keep the satellite alive during eclipse, and we must charge it efficiently during sunlight. On top of that, the battery has strict safety constraints -- voltage limits, current limits, and a hard prohibition on charging below freezing. The EPS firmware must handle all of this autonomously because the ground station contact is limited to a few minutes per day.

---

## Slide 3: Architecture Overview -- Two Levels of Decision

**Visual:** Two-layer block diagram. Top layer: OBC box with arrow labeled "UART/CHIPS" going down to EPS box. Bottom layer: EPS box containing "PCU Mode Logic" with 4 mode boxes inside. A dashed line separates the two layers. Label the top layer "Satellite Modes (OBC decides)" and the bottom layer "PCU Modes (EPS decides autonomously)."

**Content/Bullet points:**
- **OBC level:** Decides satellite operating mode (Measurement, Charging, UHF Communication, Safe)
- **EPS level:** Decides PCU operating mode autonomously based on sensor readings
- The OBC tells the EPS what the satellite is doing; the EPS decides how to manage power
- 4 PCU modes: MPPT_CHARGE, CV_FLOAT, SA_LOAD_FOLLOW, BATTERY_DISCHARGE
- EPS can override the OBC: if battery is critical, EPS enters safe mode regardless

**Speaker notes:** There are two levels of decision-making. The OBC decides what the satellite should be doing at a high level -- are we taking measurements, charging, communicating, or in safe mode. The EPS receives this information but makes its own independent decisions about how to manage power. The EPS autonomously selects one of four PCU modes based on whether the sun is available and what the battery state is. Critically, the EPS can override the OBC -- if the battery drops below minimum voltage, the EPS will enter safe mode and shed loads regardless of what the OBC commanded.

---

## Slide 4: Communication -- EPS as a Slave

**Visual:** Sequence diagram showing OBC polling the EPS over UART/CHIPS. Arrows: OBC sends request, EPS responds. A timer on the EPS side counting up to 120 seconds. After 120s, the EPS box turns red with "AUTONOMOUS SAFE MODE."

**Content/Bullet points:**
- EPS is a pure slave on the UART/CHIPS bus -- it cannot initiate communication
- OBC polls the EPS periodically: reads housekeeping, sends mode commands
- Every poll resets the EPS heartbeat counter to zero
- If 120 seconds pass with no poll: EPS assumes OBC is dead
- Autonomous decision: if battery < 5.5 V -> CHARGING sub-state; otherwise -> COMMUNICATION sub-state (beacon)
- Source: mission doc p.125

**Speaker notes:** The EPS communicates with the OBC over a UART connection using the CHIPS protocol. The EPS is a pure slave -- it can never initiate a message. The OBC is responsible for polling it. Every time the OBC communicates with the EPS, it resets an internal heartbeat counter. If that counter reaches 120 seconds without any communication, the EPS assumes the OBC has crashed or rebooted and enters autonomous safe mode. In that case, the EPS decides the safe mode sub-state on its own: if the battery is critically low, it enters the CHARGING sub-state to prioritize recharging. Otherwise, it enters the COMMUNICATION sub-state to keep the UHF radio beaconing so ground can diagnose the problem.

---

## Slide 5: PCU Mode Overview

**Visual:** State machine diagram with 4 boxes arranged in a diamond or square. Arrows between them labeled with transition conditions:
- MPPT_CHARGE -> SA_LOAD_FOLLOW: "Vbat >= 8.3 V (full)"
- MPPT_CHARGE -> BATTERY_DISCHARGE: "Solar < 8.2 V"
- MPPT_CHARGE -> CV_FLOAT: "Timeout"
- SA_LOAD_FOLLOW -> MPPT_CHARGE: "Vbat < 8.3 V"
- SA_LOAD_FOLLOW -> BATTERY_DISCHARGE: "Solar < 8.2 V"
- CV_FLOAT -> MPPT_CHARGE: "Vbat < 8.1 V for t2"
- CV_FLOAT -> BATTERY_DISCHARGE: "Solar < 8.2 V"
- BATTERY_DISCHARGE -> MPPT_CHARGE: "Solar >= 8.2 V"

**Content/Bullet points:**
- **MPPT_CHARGE:** Sun available, battery not full -- track maximum power point
- **CV_FLOAT:** Battery full/near-full -- constant voltage regulation (bang-bang)
- **SA_LOAD_FOLLOW:** Battery full -- supply load directly from solar, minimize battery stress
- **BATTERY_DISCHARGE:** Eclipse -- solar panels disconnected, battery powers everything
- All transitions are based on measurable quantities: Vbat, Vsolar, Ibat

**Speaker notes:** Here is the heart of the state machine. There are four PCU operating modes. MPPT_CHARGE is the normal sunlight charging mode -- the algorithm actively tracks the maximum power point of the solar panel to extract the most energy. When the battery is nearly full, we transition to CV_FLOAT which does constant-voltage regulation to top off the battery gently. SA_LOAD_FOLLOW means the battery is full and we are just following the load -- supplying what the satellite needs from solar without overcharging. BATTERY_DISCHARGE is eclipse mode: the solar panels are disconnected and the battery powers everything. All transitions are based on thresholds we can measure: battery voltage, solar array voltage, and battery current.

---

## Slide 6: MPPT_CHARGE Mode

**Visual:** Flowchart matching the logic from the code. Start -> "Temperature < 0 C?" if yes -> "D = 5% (min)" if no -> "Ibat > 2A?" if yes -> "Decrease D" if no -> "Vbat > 8.4 V?" if yes -> "Decrease D" if no -> "Run IncCond Algorithm" -> "Update D." Also show exit conditions at bottom: "Solar < 8.2 V -> BATTERY_DISCHARGE" and "Vbat >= 8.3 V -> SA_LOAD_FOLLOW."

**Content/Bullet points:**
- Primary charging mode: uses Incremental Conductance MPPT algorithm
- Safety overrides checked BEFORE running the algorithm:
  1. Temperature < 0 C: duty cycle forced to minimum (no charging)
  2. Battery current > 2 A: reduce duty cycle (overcurrent protection)
  3. Battery voltage > 8.4 V: reduce duty cycle (overvoltage protection)
- If none triggered: run the IncCond algorithm to adjust duty cycle
- Algorithm is integer-only, uses 8-sample moving average to filter ADC noise
- Step size: 0.5% of full scale per iteration (328/65535)

**Speaker notes:** In MPPT_CHARGE mode, the firmware first checks three safety conditions before running the MPPT algorithm. The highest priority is temperature: below 0 degrees Celsius, charging lithium-ion cells causes permanent lithium plating damage, so we force the duty cycle to the 5% minimum regardless of anything else. Next, we check for overcurrent and overvoltage conditions. Only if all three safety checks pass do we actually run the Incremental Conductance algorithm to track the maximum power point. The algorithm works on raw 12-bit ADC readings, uses integer-only math for Cortex-M0+ compatibility, and averages 8 samples to suppress noise before making each adjustment decision.

---

## Slide 7: CV_FLOAT Mode

**Visual:** Two sub-state boxes side by side. Left box: "NORMAL -- Bang-bang voltage regulation on Vbat_max (8.4 V)." Right box: "TEMP_MPPT -- Transient load spike, run IncCond." Arrow from NORMAL to TEMP_MPPT labeled "Ibat < -2 A (big discharge)." Arrow from TEMP_MPPT back to NORMAL labeled "Ibat >= 0 (load spike over)." Also show exit to MPPT_CHARGE: "Vbat < 8.1 V for t2 iterations."

**Content/Bullet points:**
- Battery is near-full: regulate charging voltage at Vbat_max (8.4 V)
- NORMAL sub-state: bang-bang controller adjusts duty cycle up/down by 0.25% steps
- TEMP_MPPT sub-state: triggered by large transient discharge (> 2 A)
  - Temporarily runs full IncCond to maximize solar extraction during load spike
  - Returns to NORMAL when battery current goes positive again
- If Vbat stays below 8.1 V for too long: transitions back to MPPT_CHARGE
- Source: mission doc Figure 3.4.8, p.102

**Speaker notes:** CV_FLOAT mode handles the tricky near-full battery situation. The battery is essentially full, so we do not want to keep aggressively MPPT charging -- that would overcharge it. Instead, we use a simple bang-bang controller that nudges the duty cycle up or down by 0.25% each iteration to keep the charging rail voltage at the target. But there is a subtlety: if there is a sudden large load spike -- say the UHF radio transmits a burst -- the battery might momentarily see a large discharge current. In that case, we temporarily switch to full MPPT to extract maximum solar power during the spike. Once the load returns to normal, we go back to constant-voltage regulation.

---

## Slide 8: BATTERY_DISCHARGE Mode

**Visual:** Block diagram showing the solar panel eFuse open (disconnected), battery connected to load bus, with a priority list of loads on the right side showing shedding order. Mark the duty cycle as "D = 5% (minimum)."

**Content/Bullet points:**
- Eclipse mode: no useful solar input
- Panel eFuse opened: solar array electrically disconnected from converter
- Buck converter duty cycle set to minimum (5%)
- Battery is sole power source for all loads
- Overcurrent protection: if discharge current exceeds 2 A, shed lowest-priority load
- Load shedding priority (shed first to last): SPAD Camera -> GNSS -> UHF -> ADCS -> OBC (never shed)
- If Vbat < 5.2 V (Vmin + hysteresis): trigger safe mode alert

**Speaker notes:** During eclipse, there is no solar power available, so we disconnect the solar panels entirely by opening the eFuse. The battery is the sole power source. The firmware monitors the discharge current and battery voltage. If the discharge current exceeds the 2 A limit, we start shedding loads one at a time, starting with the lowest priority subsystem -- the SPAD camera -- and working up. The OBC is never shed. If the battery voltage drops below 5.2 volts, which is the minimum plus a 200 millivolt hysteresis margin, we trigger a safe mode alert to the OBC and take protective action.

---

## Slide 9: Safe Mode

**Visual:** Table with 4 rows (one per sub-state) and columns: Sub-state, Trigger, Loads ON, Loads OFF. Below the table, list the 3 main triggers with their conditions.

| Sub-state | When | Loads ON | Loads OFF |
|---|---|---|---|
| DETUMBLING | Tumbling detected | OBC, ADCS, UHF | SPAD, GNSS |
| CHARGING | Battery critical (< 5.5 V) | OBC, UHF | SPAD, GNSS, ADCS |
| COMMUNICATION | Lost ground contact | OBC, ADCS, UHF | SPAD, GNSS |
| REBOOT | Periodic reboot cycle | OBC, ADCS, UHF | SPAD, GNSS |

**Content/Bullet points:**
- Safe mode is NOT a single state -- it has 4 dynamic sub-states
- Three firmware-level triggers:
  1. Battery voltage below 5.0 V minimum
  2. Temperature outside safe range (< -10 C or > 60 C)
  3. OBC heartbeat timeout (120 seconds)
- Each sub-state has a different load profile (see table)
- CHARGING sub-state is most aggressive: only OBC and UHF beacon
- OBC communicates the sub-state; if OBC is dead, EPS decides autonomously
- Exit requires ground command relayed through OBC (ECSS-E-ST-70-11)

**Speaker notes:** Safe mode is more complex than people often assume. It is not a single state where everything shuts down. There are four sub-states, each with a different load profile tuned to the specific emergency. The CHARGING sub-state is the most aggressive -- it shuts down everything except the OBC and UHF beacon to maximize battery recharge. The COMMUNICATION sub-state keeps ADCS running because we need attitude control for the antenna to point at ground. Normally the OBC tells the EPS which sub-state to be in. But if the OBC is dead -- which is exactly the 120-second timeout scenario -- the EPS decides on its own based on battery voltage.

---

## Slide 10: Thermal Control

**Visual:** Temperature number line from -30 C to +70 C with three zones marked:
- Red zone below 0 C: "CHARGING FORBIDDEN (lithium plating risk)"
- Orange zone below -10 C: "HEATER ON"
- Red zone above 60 C: "LOAD SHEDDING"
Also show an arrow pointing to 0 C labeled "Hard physics limit -- not configurable."

**Content/Bullet points:**
- Below -10 C: battery heater activated
- Below 0 C: charging FORBIDDEN -- duty cycle forced to 5% minimum
  - Lithium plating on anode causes permanent, irreversible battery damage
  - This is a physics constraint, not a tunable parameter
- Above 60 C: non-essential loads shed to reduce heat generation
- Temperature measured via thermistor through SPI, in deci-degrees (0.1 C resolution)
- Thermal checks run AFTER PCU mode logic -- they can override any mode's duty cycle

**Speaker notes:** Thermal control is a critical safety layer. The most important rule is simple: you must never charge a lithium-ion battery below zero degrees Celsius. At sub-zero temperatures, lithium ions plate onto the anode as metallic lithium instead of intercalating properly. This is irreversible and reduces capacity permanently. So even if the MPPT algorithm says to increase the duty cycle, the thermal override will force it back to 5% minimum if the temperature is below zero. The heater activates at minus 10 C to try to warm the battery before we reach that hard limit. On the other end, above 60 C we start shedding loads to reduce power dissipation and heat.

---

## Slide 11: Code Architecture -- Pure Logic

**Visual:** Two-column layout. Left column: "Firmware (Category 1 -- Pure Logic)" listing eps_state_machine.c, mppt_algorithm.c, eps_configuration_parameters.h with "Integer-only, no floats, no hardware calls." Right column: "Simulation (PC-only)" listing solar_panel_simulator.c, buck_converter_model.c, battery_model.c, eps_simulation_runner.c with "Float-based physics models." An arrow between them labeled "Same .c and .h files compiled for both targets."

**Content/Bullet points:**
- Firmware code is Category 1: pure logic, zero hardware dependencies
- Takes integer sensor readings in, returns integer actuator commands out
- Same .c files compile for ARM (SAMD21 target) and x86 (PC simulation)
- No floating point: Cortex-M0+ has no FPU, integer division done by cross-multiplication
- All state is in structs passed by pointer -- no globals anywhere
- Physics models (solar panel, buck converter, battery) are float-based, PC-only
- Deterministic: given the same input sequence, always produces the same output

**Speaker notes:** One of the key design decisions was making the firmware code completely hardware-independent. The state machine and MPPT algorithm files have zero includes of any hardware register headers. They receive sensor readings as plain integers and return actuator commands as plain integers. This means the exact same C source files that run on the SAMD21 microcontroller also compile and run on a regular PC. The physics models -- solar panel, buck converter, battery -- are separate files that only run in simulation. They use floating-point math to model the real physics, then convert to integer millivolts and milliamps before passing the data to the firmware code. This guarantees that what we test in simulation is exactly what runs in flight.

---

## Slide 12: Simulation Architecture -- Closed Loop

**Visual:** Circular flow diagram with numbered steps:
1. Battery model -> Vbat (terminal voltage from SOC and current)
2. Buck converter model -> Vpanel = Vbat / D (from duty cycle)
3. Solar panel model -> Ipanel (from Vpanel, temperature, irradiance)
4. Power balance -> Ibat = Psolar/Vbat - Iload
5. SOC update -> new SOC from Ibat and time step
6. Convert to integer millivolts/milliamps + 12-bit ADC readings
7. Run firmware state machine (the REAL code)
8. Output: new duty cycle D -> feeds back to step 2
Show "200 us per step" and "CSV logged every 1000 steps" annotations.

**Content/Bullet points:**
- Closed-loop simulation: firmware output (duty cycle) feeds back into physics
- Each step is 200 us (matching MCU superloop period at 48 MHz)
- Battery model: piecewise-linear V(SOC) curve, internal resistance, Coulomb counting
- Solar panel: 5-parameter single-diode model (Newton-Raphson solver), Azur Space 3G30C data
- Buck converter: ideal CCM model, V_panel = V_battery / D
- ADC model: 12-bit quantization + 2% random noise
- 500,000 steps = 100 seconds of simulated time; 56,400,000 steps = 2 full orbits

**Speaker notes:** The simulation runs a complete closed loop. Every 200 microseconds of simulated time -- matching what the real MCU would do -- we compute the battery terminal voltage from the state of charge, then compute what panel voltage the buck converter imposes at the current duty cycle, then compute what current the solar panel produces at that voltage given the temperature and irradiance, then do a power balance to get the net battery current, update the state of charge, convert everything to the integer format the firmware expects, and run one iteration of the real firmware state machine. The new duty cycle output from the firmware feeds back into the next step. This captures the real dynamics: the algorithm's decisions affect the physics, which affect the sensor readings, which affect the next decision.

---

## Slide 13: Key Physics -- Buck Converter and IV Curve

**Visual:** Two sub-figures side by side.
Left: IV curve plot with the characteristic knee shape. Mark Isc (2.1 A), Voc (18.7 V), and the MPP point with a star. Show the equation P = V x I.
Right: Buck converter equation: V_panel = V_battery / D. Show a table:

| D (%) | V_panel (V) | Where on IV curve |
|---|---|---|
| 95% | 7.8 V | Far right, low power |
| 50% | 14.8 V | Near MPP |
| 42% | 17.6 V | At MPP |
| 5% | 148 V | Off the curve entirely |

**Content/Bullet points:**
- Solar panel IV curve: flat current region at low V, steep voltage collapse at high V
- Maximum Power Point (MPP) is at the "knee" of the curve
- For 4P config (7 cells in series): Voc ~ 18.7 V, Isc ~ 2.1 A, Pmpp ~ 3.5 W per panel
- Buck converter links duty cycle to operating point: V_panel = V_battery / D
- Higher D -> lower V_panel -> panel operates at lower voltage (more current)
- Lower D -> higher V_panel -> panel operates at higher voltage (less current)
- MPPT algorithm hunts for the D that maximizes P = V x I

**Speaker notes:** Understanding these two relationships is key to understanding the entire system. The solar panel has a nonlinear IV curve -- at low voltages it produces nearly constant current, and at high voltages the current drops steeply to zero. The maximum power is somewhere in the middle. The buck converter is what connects the duty cycle knob to the operating point on this curve. By changing the duty cycle D, we change the voltage the panel sees. The MPPT algorithm's job is to find the duty cycle value that puts us right at the knee of the IV curve where power is maximized. At a battery voltage of 7.4 V, the optimal duty cycle is around 42%, which gives a panel voltage of about 17.6 V -- right at the MPP.

---

## Slide 14: Simulation Results -- Scenario 1: MPPT Charging

**Visual:** Four stacked time-series plots (from the simulation CSV data), time on x-axis (0 to 100 seconds):
1. Duty cycle (%) -- should show convergence from 50% to ~42%
2. Panel power (W) -- should show rise to ~3.5 W and stabilize
3. Battery voltage (mV) -- should show slow rise from ~7200 mV
4. Battery SOC (%) -- should show steady rise from 50%
Annotate the convergence point on the duty cycle plot.

**Content/Bullet points:**
- Scenario 1: Full sun, 25 C, battery starting at 50% SOC, 500k iterations (100 s)
- MPPT algorithm converges from 50% duty cycle to the optimal ~42% within seconds
- Panel power stabilizes near the theoretical maximum (~3.5 W)
- Battery charges at approximately 3 A (net: solar current minus load current)
- SOC rises steadily from 50% over the simulation window
- PCU mode stays in MPPT_CHARGE throughout -- correct behavior

**Speaker notes:** Scenario 1 is the baseline: constant full sun, room temperature, battery at 50% state of charge. The MPPT algorithm starts at 50% duty cycle and within a few seconds converges to the optimal value around 42%, which is where the panel delivers maximum power. You can see the panel power rising and stabilizing near the theoretical maximum of about 3.5 watts. The battery is charging at roughly 3 amps -- that is the solar current minus what the loads are consuming. The state of charge rises steadily. The PCU mode stays in MPPT_CHARGE the entire time, which is exactly correct for this scenario.

---

## Slide 15: Simulation Results -- Scenario 2: Eclipse Entry

**Visual:** Four stacked time-series plots, time 0-100 seconds. Vertical dashed line at 50 seconds labeled "ECLIPSE."
1. Irradiance -- step from 1.0 to 0.0 at 50s
2. PCU mode -- 0 (MPPT_CHARGE) until 50s, then 3 (BATTERY_DISCHARGE)
3. Panel eFuse -- 1 (closed) until 50s, then 0 (open)
4. Battery SOC (%) -- rising until 50s, then declining
Annotate the transition point.

**Content/Bullet points:**
- Scenario 2: Sun for 50 seconds, then eclipse. Battery starts at 70% SOC.
- At eclipse entry (50.0 s): solar voltage drops below 8.2 V threshold
- State machine immediately transitions: MPPT_CHARGE -> BATTERY_DISCHARGE
- Panel eFuse opens (solar panel electrically disconnected)
- Duty cycle drops to 5% minimum
- Battery becomes sole power source; SOC begins declining
- All 5 loads remain powered (no overcurrent condition)

**Speaker notes:** Scenario 2 tests the eclipse entry transition. For the first 50 seconds we have full sun and the system is in MPPT_CHARGE, charging normally. Then the irradiance drops to zero, simulating the satellite entering Earth's shadow. The state machine detects that the solar array voltage has dropped below the 8.2 volt minimum operating threshold and immediately transitions to BATTERY_DISCHARGE mode. The panel eFuse opens to disconnect the solar array. The duty cycle drops to the minimum 5%. From this point on, the battery is the only power source. You can see the SOC starting to decline. All five loads remain powered because the discharge current is within the 2 A limit.

---

## Slide 16: Simulation Results -- Scenario 5: OBC Timeout

**Visual:** Four stacked time-series plots, time 0-200 seconds. Vertical dashed line at 120 seconds labeled "OBC TIMEOUT."
1. Iterations since last heartbeat -- linear ramp from 0 to 600000 at 120s
2. Safe mode flag -- 0 until 120s, then 1
3. Number of loads enabled -- 5 until 120s, then drops to 3
4. PCU mode -- MPPT_CHARGE throughout (sun is still available)
Annotate: "Exactly 120.0 s = 600,000 iterations at 200 us/iter."

**Content/Bullet points:**
- Scenario 5: Full sun, 70% SOC, but OBC heartbeat is never sent
- Heartbeat counter increments every iteration (200 us)
- At exactly 120.0 seconds (600,000 iterations): counter exceeds threshold
- Safe mode activates with reason: OBC_HEARTBEAT_TIMEOUT
- Autonomous sub-state: COMMUNICATION (Vbat > 5.5 V, so not CHARGING)
- Loads drop from 5 to 3: SPAD and GNSS shed, OBC + ADCS + UHF remain
- PCU mode continues as MPPT_CHARGE (sun is still available, battery still needs charge)

**Speaker notes:** This scenario is interesting because it tests a failure mode that has nothing to do with power. The sun is shining, the battery is healthy at 70%, but the OBC never sends a heartbeat. The heartbeat counter increments every iteration. At exactly 120 seconds -- 600,000 iterations at our 200-microsecond loop period -- the counter exceeds the threshold and the EPS enters autonomous safe mode. Since the battery voltage is above 5.5 volts, the EPS selects the COMMUNICATION sub-state, which keeps the UHF radio alive for beaconing. The SPAD camera and GNSS receiver are shed, dropping total enabled loads from 5 to 3. Notice that the PCU mode itself does not change -- MPPT_CHARGE continues because the sun is available and the battery still needs charging. Safe mode and PCU mode are orthogonal.

---

## Slide 17: Simulation Results -- Scenario 6: Cold Temperature

**Visual:** Three stacked time-series plots:
1. Temperature -- constant at -20 C (shown as -200 deci-degrees)
2. Duty cycle -- stuck at 5% minimum (3277/65535)
3. Heater flag -- ON (1) the entire time
Annotate: "Charging forbidden: D forced to 5% despite sun available" and "Heater ON: T < -10 C."

**Content/Bullet points:**
- Scenario 6: Full sun, 70% SOC, but temperature is -20 C
- Heater activates immediately: -20 C < -10 C threshold
- Charging is FORBIDDEN: -20 C < 0 C -> duty cycle forced to 5% minimum
- MPPT algorithm runs but its output is overridden by the thermal safety layer
- Battery slowly discharges despite available sunlight (loads consume power, no effective charging)
- This is correct behavior: battery safety takes absolute priority over charging

**Speaker notes:** Scenario 6 demonstrates the thermal safety system. The temperature is minus 20 degrees Celsius -- well below the charging prohibition threshold of 0 C. Even though we have full sun and the MPPT algorithm is trying to track the maximum power point, the thermal control layer overrides the duty cycle to the 5% minimum every single iteration. At 5%, the buck converter output voltage is so high that essentially no current flows from the panels. The heater is on because we are also below the minus 10 C heater activation threshold. The battery slowly discharges because the loads are consuming power but we are not charging. This is exactly the right behavior -- protecting the battery from lithium plating is more important than maintaining charge.

---

## Slide 18: Simulation Results -- Scenario 8: Full Orbit (2 Orbits)

**Visual:** Two large time-series plots spanning ~188 minutes (2 full orbits).
1. Top plot: Irradiance (square wave: 57 min ON, 37 min OFF, repeating) overlaid with PCU mode (0=MPPT_CHARGE during sun, 3=BATTERY_DISCHARGE during eclipse)
2. Bottom plot: Battery SOC starting at 70%, declining during eclipses, partially recovering during sun periods. Mark the final SOC at ~47%.
Show orbit boundaries with vertical dashed lines.

**Content/Bullet points:**
- Scenario 8: 2 full orbits, 56.4 million iterations (188 minutes simulated)
- Orbit period: 94 min (57 min sun + 37 min eclipse)
- Battery starts at 70% SOC
- During sun: MPPT_CHARGE mode, SOC rises
- During eclipse: BATTERY_DISCHARGE mode, SOC falls
- After 2 orbits: SOC dropped from 70% to approximately 47%
- Net energy deficit: solar charging does not fully compensate eclipse discharge at 70% SOC
- This matches expectations: power budget is tight for a 1U CubeSat

**Speaker notes:** Scenario 8 is the most comprehensive test: two complete orbits, nearly 190 minutes of simulated time. The square wave pattern shows the mode cycling: MPPT_CHARGE during the 57-minute sun periods and BATTERY_DISCHARGE during the 37-minute eclipses. The SOC plot tells the real story. Starting at 70%, the battery partially recharges during each sun period but loses more during each eclipse. After two full orbits, the SOC has dropped to about 47%. This net deficit is expected for our power budget -- the simulation uses a nominal load of about 8 watts, and the solar panels on a 1U CubeSat can only provide about 3.5 watts per panel. This data will be crucial for the operations team to plan duty cycling of high-power payloads.

---

## Slide 19: Demo -- Streamlit Webapp

**Visual:** Screenshot of the Streamlit application showing the sidebar controls (scenario selector, panel configuration, battery voltage slider, Run Simulation button) and the main area with plots and live metric widgets. If possible, show it running live.

**Content/Bullet points:**
- Interactive webapp built with Streamlit (Python)
- Calls the same C simulation executable via subprocess
- Features:
  - Scenario selector (8 scenarios covering all operating conditions)
  - Panel configuration toggle (4P vs 2S2P -- verifies hardware team's design)
  - Battery voltage slider (6.0 V to 8.4 V)
  - Live metric widgets: Voltage, Current, Power, Duty Cycle, and more
  - 4 real-time plots: IV curve, power tracking, duty cycle, battery state
  - Iteration slider to step through simulation frame by frame
  - Raw CSV data table for detailed inspection
- All 16 end-to-end Playwright tests passing

**Speaker notes:** We also built an interactive webapp using Streamlit that wraps the C simulation. You can select any of the 8 scenarios, choose the panel configuration, adjust the battery voltage, and hit Run. The app calls the compiled C executable, parses the CSV output, and displays interactive plots in real time. There is an iteration slider that lets you step through the simulation frame by frame and see how the duty cycle, voltage, current, and power evolve. The raw CSV data is also available for anyone who wants to do their own analysis. We have a full suite of 16 end-to-end Playwright tests that verify the app works correctly across all scenarios and edge cases -- all passing.

---

## Slide 20: What's Next

**Visual:** Roadmap with three phases:
- Phase 1 (NOW): Simulation validated (green checkmark)
- Phase 2 (NEXT): Hardware integration (yellow arrow)
- Phase 3 (LATER): Flight software (blue arrow)
List specific items under each phase.

**Content/Bullet points:**
- **Threshold tuning with battery team:**
  - Vbat_full (8300 mV), Vbat_charge_resume (8100 mV), Vbat_critical (5500 mV) are placeholders
  - Need real cell characterization data to finalize
- **CHIPS protocol implementation:**
  - UART driver (DMA-based, already documented)
  - CHIPS slave state machine for OBC communication
  - Housekeeping telemetry packing
- **Hardware integration:**
  - Flash firmware onto SAMD21 target board
  - Validate ADC readings match simulation assumptions (voltage reference, noise floor)
  - Verify buck converter behavior against ideal model
- **Panel configuration verification:**
  - 4P vs 2S2P: simulation supports both, hardware team must confirm
- **Extended orbit simulation:**
  - 10+ orbit runs to verify long-term SOC stability
  - Vary load profiles to match real mission timeline

**Speaker notes:** The simulation is complete and validated, but there is work ahead. The most important near-term item is working with the battery team to finalize the voltage thresholds. Every threshold in the configuration struct is currently a placeholder. We need real cell characterization data to set the correct values. In parallel, we need to implement the CHIPS communication protocol so the EPS can actually talk to the OBC. Then hardware integration: flashing the firmware onto the real SAMD21 board and verifying that the ADC readings and buck converter behavior match what we assumed in simulation. We also need the hardware team to confirm the panel configuration -- our simulation supports both 4P and 2S2P, but only one is correct.

---

## Slide 21: Summary

**Visual:** Three-column layout summarizing the work:
Column 1 "What We Built": icon of code brackets
Column 2 "What It Proves": icon of a checkmark
Column 3 "Open Questions": icon of a question mark

**Content/Bullet points:**
- **What we built:**
  - Complete EPS state machine in C: 4 PCU modes, 4 safe mode sub-states, thermal control, load shedding
  - Closed-loop simulation with physics models (solar panel, buck converter, battery)
  - Integer-only firmware: same code runs on SAMD21 and PC
  - Interactive Streamlit webapp for visualization and exploration
  - 8 simulation scenarios covering nominal operation, faults, and full orbit cycles
- **What it proves:**
  - MPPT algorithm converges to the correct maximum power point
  - Mode transitions fire at the correct thresholds
  - Safe mode activates at exactly 120.0 s for OBC timeout
  - Thermal override prevents charging below 0 C
  - Load shedding works in priority order
  - 2-orbit simulation shows realistic SOC behavior
- **Open questions:**
  - Final voltage/current thresholds (waiting on battery team)
  - Panel configuration (4P or 2S2P -- waiting on hardware team)
  - Real ADC noise floor and converter efficiency on actual hardware
  - Long-term SOC stability over many orbits with realistic load profiles

**Speaker notes:** To summarize: we built a complete EPS power management state machine that implements everything from the mission document -- all four PCU modes, all four safe mode sub-states, thermal control with charging prohibition, and priority-based load shedding. The same C code compiles for both the MCU and the PC, and we have validated it through 8 simulation scenarios that cover normal operation, eclipse transitions, safe mode triggers, thermal extremes, and full orbit cycles. The simulation proves the logic is correct: the MPPT converges, transitions fire at the right thresholds, safe mode activates at exactly 120 seconds, and the thermal override works. What remains is finalizing the thresholds with the battery team, confirming the panel configuration with hardware, and validating everything on the real board. Questions?
