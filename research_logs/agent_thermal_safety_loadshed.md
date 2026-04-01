# CHESS Pathfinder 0 EPS -- Thermal Control, Load Shedding, Inhibits & Safety Modes

**Source document:** `chess sattelite main doc-1.pdf` (CP0_SATPF_2026-03-08 Issue 1 Revision 1)
**Research date:** 2026-04-01
**Researcher:** Deep-research agent (Claude)

---

## Table of Contents
1. [Heater Circuit (Battery Heater Unit -- BHU)](#1-heater-circuit-battery-heater-unit--bhu)
2. [Load Shedding](#2-load-shedding)
3. [Inhibit System](#3-inhibit-system)
4. [Safe Mode](#4-safe-mode)
5. [Operational Modes -- EPS Implications](#5-operational-modes--eps-implications)
6. [Radiation Mitigation (Firmware)](#6-radiation-mitigation-firmware)
7. [Battery Thermal Sensor](#7-battery-thermal-sensor)
8. [Open Questions & Gaps](#8-open-questions--gaps)

---

## 1. Heater Circuit (Battery Heater Unit -- BHU)

### 1.1 What is it?

The document calls it the **Battery Heater Unit (BHU)**. It is part of the Battery Pack, not a standalone subsystem. It consists of **temperature sensors** and **heating elements** mounted directly on the Battery Pack.

> "A heating system, referred to as the Battery Heater Unit (BHU), consists of temperature sensors and heating elements on the Battery Pack."
> -- **Page 94**, Section 3.4 EPS overview

### 1.2 Heater Type

**Resistive heater.** The EPS Quick Facts Table explicitly states:

| Parameter | Value |
|---|---|
| Heater Resistance | **22 Ohm** |
| Heater Power | **Typ. 6 W** |

> -- **Page 95**, Table 3.4.1: EPS Quick Facts Table

The document does NOT specify the exact physical form factor (Kapton flexible heater, wire-wound, thick-film, etc.). However, from the battery enclosure description on **page 115-116**:

> "There will be **two heaters**, one between each cell."

The enclosure has venting holes "incorporated at the top of the enclosure to facilitate the installation of heaters." The enclosure is designed for 21700 format batteries but will be scaled down to 18650. The heaters are physically placed between the cylindrical cells inside the metallic (Aluminum 6061-T6) enclosure.

**Confidence:** Verified resistive type from Table 3.4.1 (page 95). Physical form factor (e.g., Kapton pad vs. wire-wound) is NOT stated -- this is a gap.

### 1.3 How is the heater actuated?

The heater is **controlled by the EPS MCU on the PCDU** -- NOT by a separate thermal control system.

> "While these elements are part of the Battery Pack, their operation is controlled by the MCU on the PCDU."
> -- **Page 94**, paragraph 2

From the EPS Architecture diagram (**Figure 3.4.1, page 95**), the BHU block shows:
- A **Temperature Sensor** connected to the MCU
- A **Heater Circuit** with a switch controlled by the MCU
- The BHU is connected to the battery bus (V_bat)

The MCU role explicitly includes:
> "Control the heater on the Battery Pack (Digital I/O Pin)"
> -- **Page 95**, Section 3.4.2.1 MCU roles

**Actuation method: Digital I/O pin (GPIO on/off).** This is a binary on/off control, NOT PWM proportional control.

HOWEVER, the thermal control section describes a more sophisticated approach:

> "A closed-loop control system is required to continuously monitor each cell's temperature and dynamically adjust heating and current levels based on real-time feedback."
> -- **Page 225**, Section 3.11.2.3

> "The control strategy will focus on an **enhanced PID controller**... Additionally, a simpler **PI controller** may be sufficient given the slow response time of the thermal system."
> -- **Pages 225-226**, Section 3.11.2.3

**CRITICAL AMBIGUITY:** The MCU role description says "Digital I/O Pin" (implying on/off), but the thermal control section describes PID/PI control which requires proportional output. To do PID control with a resistive heater, you would need **PWM** on the GPIO pin (or a separate driver IC).

**Most likely interpretation:** The MCU controls the heater via a **GPIO pin driving a MOSFET switch** (visible in Figure 3.4.1 as the "Switch" in the BHU block). The PID/PI controller output can be implemented as PWM duty cycle on this GPIO. The document says "Digital I/O Pin" because the pin IS digital -- but it can be configured as a PWM output on the SAMD21.

**Confidence:** GPIO-controlled switch is verified (page 95). PID/PI control is the stated design intent (page 225-226). Whether the final implementation is simple on/off with hysteresis or true PWM-based PI is **unclear/TBD** -- the thermal design section itself says the battery pack design "is currently not finalized" (page 225).

### 1.4 Temperature Thresholds

The document uses placeholder variable names rather than exact values:

> "If temperature < TempMin -> Heaters activate"
> "If temperature > TempMax -> Non-essential systems shut down"
> -- **Page 225**, Section 3.11.2.1

From the component temperature limits table (**Table 3.11.2, page 227**):

| Component | Operating Range |
|---|---|
| EPS Batteries | **-20 C to 60 C** |
| EPS PCDU | -55 C to 150 C |
| EPS Solar Panels | -40 C to 105 C |

**Important footnote on page 227:**
> "Battery charging is not permitted below 0 C."

So:
- **TempMin (heater ON threshold):** Must be above -20 C for battery survival and above 0 C for charging. The exact value of TempMin is **TBD** -- not specified numerically.
- **TempMax (load shedding threshold):** Must be below 60 C. Exact value is **TBD**.

From the thermal simulation results (**Table 3.11.7, page 232**):
- EPS Batteries calculated range in hot case: **9.03 to 40.52 C**
- EPS Batteries predicted range (with +/- 15 C margin): **-5.97 to 55.52 C**

The note under Table 3.11.7 states:
> "The battery heater is actively controlled using measured battery temperature. The charging limit must therefore be interpreted separately from the global modelling uncertainty."

**Confidence:** Operating range is verified (-20 to 60 C). Exact heater on/off thresholds are NOT specified numerically -- they are TBD parameters (TempMin, TempMax). The 0 C charging prohibition is verified.

### 1.5 Which temperature sensor controls the heater?

The MCU reads **Battery Pack telemetry (Temperature via SPI)** -- see page 95, Section 3.4.2.1.

The temperature sensor type and part number are **NOT specified** in the document. The document says:
> "The battery pack will include temperature sensors that allow continuous monitoring of the battery temperature."
> -- **Page 225**

From the architecture diagram (Figure 3.4.1, page 95), the "Temperature Sensor" is shown inside the BHU block, connected to the MCU via a line (the SPI bus is mentioned in the MCU roles).

**Temperature Reference Points (Section 3.11.2.5, page 226):**
> "TRP Locations: TBD, but focused on batteries, transistors, and key electronics"
> "Routing of Thermocouples: Still under evaluation"

**Confidence:** SPI interface is verified. Exact sensor part number is NOT in the document -- this is a significant gap for firmware development.

### 1.6 Heater Power Consumption

**22 Ohm resistance, typically 6 W** (from Table 3.4.1, page 95).

At battery voltage (6V to 8.4V):
- At 6V: P = V^2/R = 36/22 = 1.64 W
- At 8.4V: P = V^2/R = 70.56/22 = 3.21 W

This does NOT reach 6W. The "Typ. 6W" implies the heater may be powered from a higher voltage rail, OR the resistance value and power value refer to the combined two heaters, OR there is a design discrepancy. **This needs clarification.**

If 6W at 22 Ohm: V = sqrt(P*R) = sqrt(132) = 11.5V, which would correspond to the solar array voltage (8.2-18.34V input range). This suggests the heater may be powered from the solar array side or a boosted rail, not from the battery bus directly.

**Confidence:** Values from Table 3.4.1 are verified but potentially inconsistent. The 6W / 22 Ohm combination needs design clarification.

---

## 2. Load Shedding

### 2.1 Physical Implementation -- How Loads Are Shed

Load shedding is implemented through the **Power Distribution Unit (PDU)** using **eFuse ICs** (electronic fuses) with MCU-controlled enable pins.

Two eFuse IC options are mentioned (**page 104**, Section 3.4.2.3):
- **MAX17523ATE+**: Current Limiter with OV, UV, Reverse Voltage Protection
- **TPS25940ARVCR**: eFuse for protection

> "The Power Distribution Unit (PDU) will implement its eFuse protection using either the MAX17523ATE+ (Current Limiter with OV, UV, Reverse Voltage Protection) or the TPS25940ARVCR (eFuse). In both cases, the microcontroller of the Power Conditioning Unit (PCU) directly controls the enable pins, eliminating the need for external p-channel MOSFET switches."
> -- **Page 104**, Section 3.4.2.3

**Key detail:** The MCU controls the eFuse enable pins **directly** -- no external MOSFET switches needed. The eFuses themselves contain integrated MOSFETs.

### 2.2 Which Power Lines Can Be Shed?

From the PC104 pinout (**Figure 3.4.16, page 112**) and PDU description (**page 104**):

| Bus | Voltage | Powers | Notes |
|---|---|---|---|
| 3.3V_Main | 3.27-3.33V | Main 3.3V supply output (OBC) | Primary bus |
| 3.3V_Backup | 3.27-3.33V | Backup 3.3V supply output (OBC) | Cold redundancy, disabled by default |
| 5V_Main | 4.75-5.25V | Main 5V supply output | Primary bus |
| 5V_Backup | (implied) | Backup 5V | Cold redundancy |
| V_Bat | 6-8.2V | Battery voltage bus output (ADCS, SatNOGS) | Unregulated |
| V_Bat_backup | 6-8.2V | Sleep Mode backup voltage supply | For safe mode |

From page 104:
> "The PDU features **two independent power lines for each voltage output**, along with a separate 3.3V line for non-critical subsystems. To minimize risk, the UHF subsystem is a Commercial Off-The-Shelf (COTS) component and shares a power line with the OBC."

> "Pull-up and pull-down resistors are included... These resistors ensure that the **primary power buses remain enabled** while the **backup buses stay disabled** until activation is required."

So each voltage rail has:
1. A primary line (enabled by default)
2. A backup line (disabled by default, activated if primary fails)
3. A separate 3.3V line for non-critical subsystems

### 2.3 When Is Load Shedding Triggered?

**Autonomous load shedding by the EPS MCU during BATTERY_DISCHARGE mode:**

> "The MCU continuously monitors the battery voltage and current: if the current exceeds the maximum discharge rate, the MCU diagnoses the condition and **adapts PDU load shedding accordingly**."
> -- **Page 103**, Section 3.4.2.2.4 (BATTERY_DISCHARGE mode)

**Safe mode also triggers load shedding:**
> "If the battery voltage drops below its minimum threshold plus a safety margin and the solar array remains unavailable, the system enters SAFE_MODE to protect the battery from deep discharge."
> -- **Page 103**

From the Safe Mode survival actions (**page 25**, Section 1.7.4.3):
> "Non-essential subsystems are turned off, including:
> - SPAD Camera scientific instrument
> - GNSS receiver"

**Temperature-triggered load shedding:**
> "If temperature > TempMax -> Non-essential systems shut down"
> -- **Page 225**

### 2.4 Priority Order

The PCU operating mode overview (**page 100**, Section 3.4.2.2.4) states priorities:
> 1. Supply the system loads via the solar array and batteries
> 2. Charge the battery
> 3. Extract maximum power from the solar array

From the power budget tables and Safe Mode diagram (**Figure 1.7.1, page 24** and **Tables 3.4.3-3.4.6, pages 117-118**), we can infer the shedding priority:

**Subsystems powered in each mode:**

| Subsystem | Measurement | Charging | UHF Comm | Safe Mode |
|---|---|---|---|---|
| ADCS | ON | ON | ON | ON |
| OBC | ON | ON | ON | ON |
| EPS | ON | ON | ON | ON |
| UHF/Transceiver | Beacon | Beacon | ON (full) | Beacon |
| GNSS | ON | ON | ON | **OFF** |
| Novoviz/SPAD Camera | ON | **OFF** | **OFF** | **OFF** |

**Inferred shedding priority (first shed to last shed):**
1. **SPAD Camera / Novoviz payload** -- shed first (non-essential science instrument)
2. **GNSS receiver** -- shed second (not needed for survival)
3. **UHF Transceiver** -- reduced to beacon mode but never fully off
4. **ADCS** -- reduced to sun-spin mode (magnetorquers only, no reaction wheels)
5. **OBC** -- always on (but can be rebooted)
6. **EPS** -- always on (last to be reset, only after TimeMax2 days without comms)

**Confidence:** The document does not provide an explicit numbered priority list for load shedding. The above is **inferred** from the mode tables and Safe Mode description. The actual firmware implementation of load shedding order needs to be determined during design.

### 2.5 Can the EPS Shed Loads Autonomously?

**YES.** Multiple passages confirm autonomous EPS load shedding:

1. During BATTERY_DISCHARGE: the MCU "adapts PDU load shedding accordingly" -- **page 103**
2. The EPS enters SAFE_MODE autonomously when battery voltage drops below minimum -- **page 103**
3. Safe Mode entry trigger #2: "Critical Power Level: The battery level falls below the minimum operational limit Bmin" -- **page 23**

The EPS can also receive commands from the OBC to shed loads, but it can act autonomously when battery conditions require it.

**Confidence:** Verified from pages 23, 25, 103.

---

## 3. Inhibit System

### 3.1 What Are Inhibits?

The inhibit system ensures **complete electrical isolation** during ground processing, launch, and storage. It prevents premature activation before deployment.

> "The inhibit system of the CubeSat is designed to ensure complete electrical isolation during ground processing, launch, and storage, preventing premature activation before deployment."
> -- **Page 108**, Section 3.4.4

### 3.2 Two Types of Inhibit Switches

There are **two** mechanical switches:

1. **RBF (Remove Before Flight) pin** -- manually removed by hand before integration into dispenser
2. **KS (Kill Switch)** -- triggered by the dispenser mechanism; remains engaged until satellite is ejected

> "The remove before flight (RBF) and Kill switch (KS) are two mechanical switches. Both of them need to be released to ground and therefore activate the pMOS transistors, which are separating the batteries and the solar arrays from the rest of the system."
> -- **Pages 108-109**

### 3.3 How Inhibits Work (Electrical)

The inhibit system has **three main components** (**page 109**):

1. **pMOS transistors** -- placed at the input of the PCU, acting as electrical inhibit switches. They prevent ANY current from flowing from solar panels and battery to the rest of the system.
2. **Micro-switch** -- mounted on the EPS board, controls the gates of the pMOS transistors.
3. **RBF pin** -- an aluminum rod inserted into the CubeSat structure that physically presses against the micro-switch.

**Operation:**
- When RBF pin is inserted: micro-switch is pressed -> pMOS gates held in non-conducting state -> NO power flows
- When RBF pin is removed: micro-switch releases -> pMOS transistors conduct -> power flows from solar panels to EPS

The KS works similarly: inside the dispenser, the CubeSat structure compresses the KS pins, keeping power off. Upon ejection, the springs push the pins outward, releasing the micro-switches and allowing power to flow.

### 3.4 Are Any Inhibits Software-Controlled?

**NO.** The inhibits are **purely hardware-based mechanical switches**. The EPS MCU has no role in the inhibit system.

> "The RBF switch is pressed with a pin which can be removed by hand, while the KS are triggered by the dispenser mechanism."
> -- **Page 109**

**Confidence:** Verified -- purely hardware. No firmware involvement. (Pages 108-110)

### 3.5 Deployment Sequence

From **page 110**, Section 3.4.4.2:

1. **RBF pin must be manually removed** before integration into the dispenser (ground operation)
2. The satellite is placed into the dispenser -- this overrides the electrical inhibits once the KS springs are compressed
3. The **KS remains engaged** until the satellite is fully ejected from the dispenser
4. Upon ejection, the compression mechanism releases -> KS micro-switches release -> **pMOS transistors conduct** -> EPS powers up

**Both RBF and KS must be released** for the spacecraft to activate.

### 3.6 Kill Switch Physical Details (Pages 220-223)

The deployment switches (KS) are spring-loaded mechanisms at the base of the structure, positioned diagonally in corners per FDS requirements. Each consists of:
- A threaded pin-shaped component
- A screw-on cap
- A compression spring (k = 1.15 N/mm, max force = 2.3 N)
- A micro-switch (reference TBD)

The micro-switch connects to a transistor that regulates current flow in the main circuit.

### 3.7 Location

- **RBF pin:** X+ side of the satellite, between the 3rd and 4th solar cells (**page 109**)
- **KS (Kill Switches):** Base of the structure (-Z face), positioned diagonally in corners (**page 109, Figure 3.4.13**)

**Confidence:** Fully verified from pages 108-110, 220-223.

---

## 4. Safe Mode

### 4.1 Entry Triggers

Safe Mode is triggered automatically if ANY of these conditions occur (**page 23**, Section 1.7.4.1):

1. **Loss of Attitude Control:** Tumbling rate exceeds threshold TumbMax
2. **Critical Power Level:** Battery level falls below minimum operational limit Bmin
3. **Loss of Communication:** No commands/acknowledgments from ground for TimeMax
4. **Housekeeping Data Failure:** System fails to retrieve or transmit telemetry
5. **Power System Failures:**
   - Failure in the main power system (EPS)
   - UHF antenna deployment failure
   - Solar panel deployment failure
6. **Temperature Anomalies:** Temperature outside safe operational range (above TempMax or below TempMin)
7. **Flight Software Failure:** Any bug, fault, assert detected in flight software
8. **Manual Ground Command:** Ground station can command Safe Mode entry

### 4.2 What the EPS Does in Safe Mode

From Figure 1.7.1 (**page 24**) -- subsystems active in Safe Mode:
- **OBC:** ON
- **EPS:** ON
- **ADCS:** Sun spin mode (magnetorquers only)
- **UHF:** Beacon mode

Subsystems **OFF** in Safe Mode:
- SPAD Camera / Novoviz payload
- GNSS receiver

From Safe Mode power budget (**Table 3.4.4, page 117**):
| Component | Mode | Average Power | Peak Power |
|---|---|---|---|
| ADCS | ON | 1.52 W | 1.88 W |
| Novoviz | OFF | 0.00 W | 0.00 W |
| EPS | ON | 0.20 W | 0.30 W |
| GNSS | OFF | 0.00 W | 0.00 W |
| OBC | ON | 1.42 W | 1.68 W |
| Transceiver | ON | 2.10 W | 5.50 W |
| **Total** | | **5.24 W** | **9.36 W** |

Safe Mode total energy over 5 hours: **31.09 Wh** (from Table 3.4.7, page 118).

### 4.3 Safe Mode Sub-States

Safe Mode is NOT a single static state -- it has **four dynamic sub-modes** (**page 24**, Section 1.7.4.2):

1. **Detumbling Safe Mode:** If tumbling uncontrollably, prioritizes restoring attitude stability using magnetorquers
2. **Charging Safe Mode:** If battery is critically low (BminCrit), ensures solar panels point toward Sun for maximum power generation
3. **Communication Safe Mode:** If communication lost, prioritizes UHF transmission to re-establish contact
4. **Reboot Safe Mode:** If no ground contact for TimeMax days, key subsystems (UHF, ADCS, OBC) are periodically rebooted. If still no contact after TimeMax2 days, the **EPS is rebooted**, followed by full satellite reset

The CubeSat autonomously transitions between these sub-modes based on system conditions.

### 4.4 Thermal Control in Safe Mode

(**Page 25**, Section 1.7.4.3):
- If temperature falls below TempMin -> heaters are activated
- If temperature exceeds TempMax -> non-essential systems remain off to prevent overheating

### 4.5 Safe Mode Exit Conditions

Exit from Safe Mode requires **ALL** of the following (**page 26**, Section 1.7.4.4):

1. **Ground Station Uplink Command:** The ONLY method -- must receive explicit command from ground station (per ECSS-E-ST-70-11)
2. **Resolution of Anomalies:** The triggering issue must be resolved
3. **Housekeeping Data Validation:** Must successfully downlink HK telemetry
4. **Stable Power Levels:** Battery above Bmin
5. **Restored Attitude Control:** Tumbling rate below TumbMin

If these conditions are not met, Safe Mode persists indefinitely.

**CRITICAL FOR EPS FIRMWARE:** Safe Mode exit requires ground command -- the EPS cannot autonomously exit Safe Mode. However, the EPS can be rebooted autonomously during Reboot Safe Mode sub-state.

**Confidence:** Fully verified from pages 22-26.

---

## 5. Operational Modes -- EPS Implications

### 5.1 Measurement Mode (Section 1.7.1, pages 21)

- **Peak power budget: 4 W** for payload
- EPS must ensure battery has sufficient energy AND science data storage is not full
- Total power consumption: average **8.01 W**, peak **11.58 W** (Table 3.4.3, page 117)
- All subsystems ON including SPAD Camera, GNSS
- Duration: 176 minutes per session (Table 3.4.7)

**EPS role:** Supply full power to all subsystems. MPPT charging when in sunlight, battery discharge during eclipse. Monitor battery SoC to ensure it stays above thresholds.

### 5.2 Charging Mode (Section 1.7.2, page 22)

- **Default state** when no mission operations are required
- Mode transitions depend on current battery levels
- SoC values are pre-calculated with power budget before initiating any mode change
- ADCS in sun-spin mode (Z+ face toward Sun)
- Novoviz/SPAD Camera and GNSS OFF during this mode (visible in Table 3.4.6, page 118 -- though charge mode table has incomplete energy values)
- Total power: average **3.70 W**, peak **6.50 W** (Table 3.4.6)

**EPS role:** PCU operates in MPPT_CHARGE or CV_FLOAT mode. Battery charging is the primary objective. Load shedding of non-essential systems.

### 5.3 UHF Communication Mode (Section 1.7.3, page 22)

- Enabled during communication windows (ground station passes)
- Requires sufficient battery charge to sustain communication
- Transceiver at full power: **4.00 W average, 12.50 W peak** in UHF mode (Table 3.4.5, page 118 -- note: "Transceiver (UHF mode)" row shows 4.00 avg, 12.50 peak in communication mode)
- Total power: average **7.70 W**, peak **12.50 W** (Table 3.4.5)
- Highest instantaneous power demand of all modes

**EPS role:** Must handle peak power demand. The communication mode peak (28.38 W from Table 3.4.7) is the highest across all modes. EPS must ensure battery can supply transient peaks.

### 5.4 PCU Internal Operating Modes

The EPS PCU has its own state machine (**pages 100-103**, Section 3.4.2.2.4):

1. **MPPT_CHARGE:** Solar array available, battery not full. Runs Incremental Conductance MPPT. Powers loads + charges battery.
2. **CV_FLOAT:** Battery full or nearly full. Holds constant voltage at battery maximum. Powers loads from solar array.
3. **SA_LOAD_FOLLOW:** Battery full, solar available. Tracks load demand rather than MPPT. Prevents overcharging.
4. **BATTERY_DISCHARGE:** Eclipse mode, no solar power. Opens solar panel eFuse. Battery powers all loads through PDU. Monitors discharge rate, performs load shedding if needed. Enters SAFE_MODE if battery drops below Vbat_min + safety margin.

**Confidence:** Verified from pages 100-103 with flowcharts (Figures 3.4.6 through 3.4.10).

---

## 6. Radiation Mitigation (Firmware)

### 6.1 Overall Strategy

From **page 106**, Section 3.4.3:

> "Given that CHESS is a short-duration mission in Low Earth Orbit, Total Ionizing Dose and displacement damage effects are expected to have limited impact. **Single Event Effects** therefore represent the dominant risk and are addressed primarily through architectural decisions."

The core principle is **functional decomposition**: charging, power routing, protection, measurement, and supervision are distributed across discrete building blocks, reducing the probability that a single radiation event can compromise multiple safety mechanisms.

### 6.2 Watchdog Timer

**YES -- the MCU uses its internal watchdog timer.**

> "The microcontroller uses its internal watchdog timer to detect software failures and trigger a reboot, ensuring recovery from radiation-induced upsets without ground intervention."
> -- **Page 106-107**, Section 3.4.3 (Fault Detection and Recovery)

**Confidence:** Verified. The SAMD21 has a built-in WDT peripheral.

### 6.3 Cold-Start Capability

> "Cold-start capability is treated as a design requirement: dedicated LDO regulators provide stable bias rails independently of the PDU, and a voltage supervisor holds the MCU in reset until all critical rails are within nominal limits, guaranteeing a deterministic startup sequence."
> -- **Page 107**

This means a **voltage supervisor IC** (part number not specified) holds the MCU RESET line low until power rails are stable. This prevents undefined behavior during radiation-induced brownouts.

### 6.4 Hardware Cutoff Circuit (Independent of Firmware)

> "The hardware cutoff circuit provides an independent layer of radiation mitigation that operates entirely independently of firmware execution. It remains permanently active, enforcing overcurrent (OCP), overvoltage (OVP), undervoltage (UVP), and undervoltage lockout (UVLO) protection even if the MCU is frozen or executing corrupted code."
> -- **Page 107**

This is implemented using:
- **LM139DR** quad comparator for over/undervoltage detection
- **LT6108AIMS8** high-side current sense amplifier for overcurrent detection
- **eFuse enable signal logic** (Figure 3.4.5, page 100) using open-collector diode OR-ing

**KEY FIRMWARE IMPLICATION:** Even if the MCU crashes due to an SEU, the hardware protection circuit will cut off power paths independently. The eFuse enable node is **pulled down by default** for failsafe behavior.

### 6.5 Triple Sensing (Voting Algorithm)

> "For current and voltage measurements at the PDU input, a triple sensing method is employed: **three independent sensors simultaneously measure the same electrical quantity**, and the MCU applies a **voting algorithm** to determine the most reliable reading."
> -- **Page 106**

The three sensors per power path (**page 98**):
1. **INA226**: Current sense IC on ESAPL, communicating over I2C
2. **LT6108**: High-side current sense amplifier, analog output to MCU ADC
3. **TPS2590 eFuse**: Provides analog current monitoring via IMON pin to MCU

**FIRMWARE REQUIREMENT:** Implement a majority-vote or median-select algorithm on these three sensor readings. This provides SEU resilience for critical measurements.

### 6.6 Memory Scrubbing

**NOT explicitly mentioned** in the EPS section. The document does not discuss memory scrubbing or ECC for the EPS MCU (SAMD21).

For the OBC, the quick facts table (**page 123**) mentions:
> "Radiation Mitigation: Internal Watchdog (1 per Computer), eFuses (1 per computer) for SEL detection and protection"

**Confidence:** Memory scrubbing is NOT mentioned for EPS. This is a gap -- the SAMD21 has no hardware ECC on SRAM. Consider implementing periodic CRC checks on critical NVM data.

### 6.7 Triple Modular Redundancy (TMR) for Critical Variables

**NOT explicitly mentioned** in the document for firmware variables. The triple sensing for hardware measurements is described (Section 6.5 above), but TMR for firmware variables (e.g., storing critical state in triplicate in RAM) is not discussed.

**Confidence:** Not mentioned. This is a firmware design decision to be made during implementation.

### 6.8 MCU Selection for Radiation Tolerance

The selected MCU is **SAMD21RT** (Radiation Tolerant variant) (**page 96**):
- Full wafer lot traceability
- TQFP64 package
- Space-grade screening (QML and ESCC flows)
- **TID > 50 krad (Si)**
- **SEL immune with LET > 78 MeV.cm2/mg**
- Temperature range: -40 C to +125 C

**Confidence:** Verified from page 96. Note: the document says "SAMD21RT" -- this is the Microchip radiation-tolerant variant of the SAMD21.

---

## 7. Battery Thermal Sensor

### 7.1 Part Number and Interface

**Part number: NOT specified** in the document. This is a significant gap.

**Interface: SPI** -- confirmed from the MCU roles:
> "Read Battery Pack telemetry (Temperature via SPI)"
> -- **Page 95**

### 7.2 What We Know

- Temperature sensors are part of the BHU, mounted on the Battery Pack
- Connected to MCU via SPI
- Two heaters, one between each pair of cells (page 116)
- TRP locations focused on "batteries, transistors, and key electronics" but exact positions are TBD (page 226)
- The thermal section mentions "thermocouples" are under evaluation (page 226), but the EPS architecture clearly shows a digital SPI sensor

**Possible sensor types** (inference, NOT from document):
- MAX31855/MAX31856 (thermocouple-to-SPI) -- if thermocouples are used
- TMP125 or similar SPI temperature sensor
- AD7415 or similar

### 7.3 Temperature Range and Accuracy

**Not specified** for the sensor itself. The battery operating range is -20 C to 60 C (Table 3.11.2, page 227). The sensor must cover at least this range with sufficient accuracy for the PI/PID controller.

**Confidence:** SPI interface verified. Everything else about the sensor is TBD/gap.

---

## 8. Open Questions & Gaps

These are items that the document either leaves as TBD or does not address, which are critical for EPS firmware development:

### 8.1 Critical Gaps (Must Resolve Before Firmware)

| # | Gap | Where Expected | Status |
|---|---|---|---|
| 1 | **Temperature sensor part number** | BHU design | NOT in document |
| 2 | **TempMin / TempMax exact values** | Thermal team | TBD placeholders |
| 3 | **Bmin, BminCrit exact voltage values** | EPS team | TBD placeholders |
| 4 | **Heater control method: on/off vs PWM** | BHU + firmware design | Contradictory info (GPIO vs PID) |
| 5 | **Load shedding priority order** | EPS/system design | Inferred only, not explicit |
| 6 | **eFuse IC final selection** (MAX17523ATE+ vs TPS25940ARVCR) | PDU design | Either/or stated |
| 7 | **TimeMax, TimeMax2 values** | Flight software team | TBD placeholders |
| 8 | **TumbMax, TumbMin values** | ADCS team | TBD placeholders |
| 9 | **KS micro-switch part number** | Mechanisms team | TBD (page 221) |
| 10 | **Heater power inconsistency** (6W at 22 Ohm doesn't match battery voltage) | EPS design | Needs clarification |
| 11 | **Memory scrubbing / TMR strategy** | Firmware design | Not discussed |
| 12 | **Voltage supervisor IC** part number | PCU design | Not specified |

### 8.2 Ambiguities to Resolve

1. **Communication protocol:** Document says I2C for EPS-OBC communication (pages 96, 112), but the actual implementation uses UART. The document footnote on page 112 hints at this: "We are considering two different communication protocols for EPS: either staying with RS-422 lines or switching to an s." The CHIPS protocol (page 124) is described as "CHESS Internal Protocol over Serial" and is used by EPS.

2. **Heater power source:** Is the heater powered from V_bat (6-8.4V) or from the solar array input? The 6W/22 Ohm specs don't match V_bat range. Need schematic clarification.

3. **Backup EPS (SOLO EPS-4):** Section 3.4.9 (pages 120-121) describes a COTS backup plan. If the in-house EPS is not ready in time, the SOLO EPS-4 from 2NDSpace would be used. This has completely different firmware requirements (I2C/RS485 command protocol, different power architecture). Firmware team should understand the risk of this swap.

### 8.3 Key Figures and Tables Referenced

| Reference | Page | Content |
|---|---|---|
| Table 3.4.1 | 94-95 | EPS Quick Facts (heater specs, battery capacity, bus voltages) |
| Figure 3.4.1 | 95 | EPS Architecture block diagram (BHU, PCU, PDU, MCU) |
| Figure 3.4.5 | 100 | Enable signal OR-ing circuit logic |
| Figure 3.4.6 | 100 | PCU Operating modes state diagram |
| Figure 3.4.10 | 103 | BATTERY_DISCHARGE mode flowchart (load shedding logic) |
| Figure 3.4.16 | 112 | PC104 pinout (all power and signal lines) |
| Table 3.4.2 | 108 | EEE Component Selection (all IC part numbers) |
| Table 3.11.2 | 227 | Component temperature limits |
| Table 3.11.7 | 232 | Thermal simulation results |
| Figure 1.7.1 | 24 | Spacecraft Operating Modes diagram |
| Tables 3.4.3-3.4.6 | 117-118 | Power budgets per mode |

---

## Summary for Firmware Developer

**What you need to implement in EPS firmware:**

1. **Heater control:** Read battery temperature via SPI sensor. Implement PI (or PID) controller or simpler hysteresis on/off control with TempMin/TempMax thresholds. Drive heater GPIO (potentially as PWM for proportional control).

2. **Load shedding:** Monitor battery voltage and current. During BATTERY_DISCHARGE, if discharge current exceeds maximum, disable non-essential PDU eFuse channels by de-asserting their enable pins. Priority: payload first, then GNSS, then reduce UHF to beacon.

3. **Safe Mode transition:** When battery voltage drops below Vbat_min + margin and solar array is unavailable, enter SAFE_MODE. Disable non-essential loads. Set flag for OBC.

4. **Watchdog:** Configure SAMD21 internal WDT. Pet it in the main loop. If the MCU hangs, WDT will reset it. Hardware protection circuit continues operating independently.

5. **Triple sensor voting:** Read INA226 (I2C), LT6108 (ADC), TPS2590 IMON (ADC) for each power path. Implement median-select or majority-vote algorithm.

6. **PCU state machine:** MPPT_CHARGE -> CV_FLOAT -> SA_LOAD_FOLLOW -> BATTERY_DISCHARGE. Transitions based on solar array availability, battery voltage, and battery charge state.

7. **Inhibits:** No firmware involvement -- purely hardware. But firmware should read the deployment switch status (GNSS over current detection pin H2-6 labeled PWR_OC in the PC104 pinout is related) to confirm deployment.

8. **Communication:** Implement CHIPS protocol over serial (UART) for OBC commands. Be ready to receive mode commands, telemetry requests, and load shedding commands from OBC.
