# General Scanner Agent -- EPS Firmware Research Log

**Status:** COMPLETE

**PDF:** CHESS Pathfinder 0 Satellite Project File (CP0_SATPF_2026-03-08 Issue 1 Rev 1)

**Sections scanned:** Pages 14-18, 20-27, 28-34, 37-41, 43-46, 48-52, 64-66, 111-113, 117-121, 133-136, 140-154, 159-162, 217-220, 231-234, 263-266, 278-302

---

## HIGH PRIORITY FINDINGS

---

### [Section 1.5.1 -- Launch and Early Operations (LEOP)] (PDF pages 14-15)

**What was found:** The EPS boots FIRST after kill switch release. The boot order is explicitly: EPS -> OBC -> ADCS (ADCS boots in parallel but is inactive for 30 min). The EPS must "manage and distribute power to other satellite systems" immediately upon boot.

**Why this matters for EPS firmware:** The EPS firmware is the FIRST thing that runs on the entire satellite. It must be rock-solid from the very first instruction. It must initialize power rails in the correct sequence and provide stable power before the OBC can boot. There is NO supervision from the OBC during this critical early phase. The EPS must autonomously manage power distribution for at least 30 minutes before any ground contact is possible.

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 14-15, 26-27

**How to find it in the PDF:** Section 1.5.1 "EPS Booting" paragraph, and Section 1.8.1 "Activation Sequence" (t0 = Separation)

---

### [Section 1.5.1 -- Battery Consumption During Detumbling] (PDF page 15, footnote 1)

**What was found:** Footnote 1 states: "The detumbling phase also takes place before the solar panel deployment, meaning that an extended duration on this phase may lead to critical battery consumption before batteries could begin to get charged. For this reason, we will study the battery evolution when the solar panels are not yet deployed, to obtain an estimated safe time margin on the detumbling phase duration."

**Why this matters for EPS firmware:** The EPS firmware must track battery level during the LEOP phase when solar panels are NOT deployed. It needs a time-based safety mechanism -- if the battery drops below a certain threshold before panels deploy, the EPS may need to limit subsystem power to prevent complete discharge. This is a unique LEOP-only constraint.

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF page 15 footnote 1

**How to find it in the PDF:** Footnote 1 at bottom of page 15

---

### [Section 1.5.1 -- Beacon Transmission Before Solar Panels] (PDF page 15, footnote 2)

**What was found:** Footnote 2 states: "Early beacon transmission is desirable for initial S/C monitoring, but this also contributes to battery consumption. Since this phase takes place before the solar panel deployment, we are currently evaluating the power consumption of the beacon transmission to obtain a safe activation time given the battery constraints."

**Why this matters for EPS firmware:** The EPS must be aware that during LEOP, the UHF transceiver may be transmitting while solar panels are stowed. The EPS needs to ensure the battery can sustain both detumbling AND beacon transmission without depletion. This suggests the EPS firmware needs a LEOP-specific power budget mode that is more conservative than normal operation.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 15 footnote 2

**How to find it in the PDF:** Footnote 2 at bottom of page 15

---

### [Section 1.6 -- Orbital Parameters Affecting Power] (PDF page 19)

**What was found:** The orbit is SSO at 475 +/- 25 km altitude, LTAN 00/12h. Eclipse time is a MAXIMUM of 37 minutes per orbit (39% of orbit period), and minimum of 23 minutes. The orbit period is approximately 94 minutes. Mean daily communication time is 779.6 seconds (~13 minutes).

**Why this matters for EPS firmware:** These numbers directly define the EPS duty cycles:
- Maximum eclipse duration = 37 min (no solar power for this long)
- Maximum sunlit duration = ~71 min
- The EPS must ensure the battery can sustain the satellite for 37 minutes of eclipse with no solar input
- Communication windows are very short (~5-6 min per pass, 2 passes/day), so the EPS must handle high peak power (UHF transmission) for short bursts

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF page 19

**How to find it in the PDF:** Section 1.6 "Mission Analysis Report Summary", orbital parameters and eclipse analysis

---

### [Section 1.7 -- Satellite Operational Modes Overview] (PDF pages 20-25)

**What was found:** There are 3 nominal modes + Safe Mode:
1. **Measurement Mode** -- 4W peak power budget, SPAD camera active, 5-minute imaging windows
2. **Charging Mode** -- default/idle state, sun-pointing, minimum power consumption
3. **UHF Communication Mode** -- ground passes, high transceiver power
4. **Safe Mode** -- 4 sub-states (Detumbling, Charging, Communication, Reboot)

Each mode has a specific attitude (ConXYZwheel, ConZSpin, ConGGboom) which affects solar panel orientation and thus power generation.

**Why this matters for EPS firmware:** The EPS does NOT directly switch satellite modes (the OBC does via EventActionSM), but the EPS:
- Must report battery SoC to OBC so mode transitions can be validated
- Must enforce power limits for each mode (e.g., 4W peak for measurement)
- In Safe Mode, the EPS must independently shed non-essential loads
- The EPS must verify sufficient power reserves BEFORE high-demand modes (measurement, communication)
- The mode directly affects power generation due to attitude changes

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 20-25

**How to find it in the PDF:** Section 1.7 and Figure 1.7.1 "Spacecraft Operating Modes"

---

### [Section 1.7.2 -- Charging Mode Details] (PDF page 22)

**What was found:** Charging Mode is the DEFAULT state when no mission operations are required. "The transition to or from charging mode depends on the current battery levels. To avoid interrupting ongoing measurements or communication, the ability to perform a mode change is conditioned on ensuring that the state of charge (SoC) of the battery is sufficiently high. These SoC values are pre-calculated with the power budget before initiating any actions."

**Why this matters for EPS firmware:** The EPS must implement SoC-based gating logic. Specific SoC thresholds (pre-calculated) determine whether the satellite can transition to measurement or communication modes. The EPS firmware needs to accurately calculate and report SoC, and potentially enforce minimum SoC requirements before allowing high-power operations.

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF page 22

**How to find it in the PDF:** Section 1.7.2 "Charging Mode" first paragraph

---

### [Section 1.7.4 -- Safe Mode Entry Triggers] (PDF page 23)

**What was found:** Safe Mode is triggered by 8 conditions, several of which the EPS must detect or contribute to:
1. Tumbling rate > TumbMax (not EPS, but EPS needs this from ADCS)
2. **Battery level < Bmin** (EPS MUST detect this)
3. No ground contact for TimeMax
4. HK data failure
5. **Power system failures** (EPS failure, antenna deployment failure, solar panel deployment failure)
6. **Temperature anomalies** (above TempMax or below TempMin -- EPS must detect for its own components)
7. Flight software failure
8. Manual ground command

**Why this matters for EPS firmware:** The EPS must autonomously detect conditions 2, 5, and 6 and communicate them to the OBC. The EPS is both a trigger source AND a responder in Safe Mode. Key parameters: Bmin (minimum battery level), TempMax, TempMin. These are referenced but their actual VALUES are not specified in this document.

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF page 23

**How to find it in the PDF:** Section 1.7.4.1 "Safe Mode Entry Triggers"

**GAP IDENTIFIED:** The actual values of Bmin, BminCrit, TempMax, TempMin, TimeMax, TimeMax2, TumbMax, TumbMin are NOT defined in this document. These must be defined elsewhere or need to be determined.

---

### [Section 1.7.4.2 -- Safe Mode Sub-States] (PDF page 24)

**What was found:** Safe Mode has 4 dynamic sub-states:
1. **Detumbling Safe Mode** -- tumbling too fast, prioritize attitude stability
2. **Charging Safe Mode** -- battery critically low (BminCrit), maximize solar generation
3. **Communication Safe Mode** -- lost ground contact, prioritize UHF
4. **Reboot Safe Mode** -- no contact > TimeMax days, reboot UHF/ADCS/OBC every TimeMax days; if > TimeMax2 days, REBOOT THE EPS followed by full satellite reset

**Why this matters for EPS firmware:** The EPS firmware must support being rebooted as part of the Reboot Safe Mode escalation (TimeMax2). This means:
- The EPS needs persistent storage for critical state (battery cycle count, error logs) that survives reboot
- The EPS must boot into a safe default state
- After EPS reboot, the entire satellite resets -- the EPS must handle the full LEOP-like boot sequence again
- The EPS must have a timer or counter mechanism for TimeMax/TimeMax2 thresholds, OR it must receive this information from the OBC

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF page 24-25

**How to find it in the PDF:** Section 1.7.4.2 "Safe Mode Sub-States" and 1.7.4.5 "Duration Without Ground Contact"

---

### [Section 1.7.4.3 -- Safe Mode Survival Activities] (PDF page 25)

**What was found:** During Safe Mode, the EPS must:
- Turn off non-essential subsystems (SPAD Camera, GNSS receiver)
- Activate heaters if temperature falls below TempMin
- Turn off non-essential systems if temperature exceeds TempMax
- Handle "EPS failure detected" -> initiate power system reset
- If Safe Mode cannot recover, await ground commands

**Why this matters for EPS firmware:** The EPS firmware must implement load shedding logic for Safe Mode. It must know WHICH power rails correspond to "non-essential" subsystems and be able to disable them. It must also have thermal management capability (heater control). And it must handle self-reset as a recovery mechanism.

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF page 25

**How to find it in the PDF:** Section 1.7.4.3 "Safe Mode Operations and Survival Activities"

---

### [Section 1.7.4.4 -- Safe Mode Exit Conditions] (PDF page 26)

**What was found:** Safe Mode can ONLY be exited by ground station uplink command. Additional conditions:
1. Ground command received (mandatory)
2. Anomaly resolved
3. HK data successfully downlinked
4. **Battery above Bmin** (EPS must confirm)
5. Tumbling rate below TumbMin

**Why this matters for EPS firmware:** The EPS cannot autonomously exit Safe Mode. But it MUST provide the battery level data (Bmin check) that is one of the exit prerequisites. The OBC needs to read this from the EPS to confirm readiness to exit Safe Mode.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 26

**How to find it in the PDF:** Section 1.7.4.4 "Safe Mode Exit Conditions"

---

### [Section 1.8.1 -- Activation Sequence Timeline] (PDF pages 26-28)

**What was found:** The LEOP timeline is structured:
- t0 = Separation: KS released, EPS powers up
- [t0, t0+30min] = LEOP Phase 1: EPS + OBC boot, health checks, ADCS boots but inactive
- [t0+30min, t_commissioning] = LEOP Phase 2: Detumbling, solar panel deploy, UHF deploy + beacon, attitude adjust, sun pointing
- > t_commissioning: UHF handshake, subsystem checkout, payload commissioning

Figure 1.8.1 shows the LEOP planning Gantt chart.

**Why this matters for EPS firmware:** The EPS must support a LEOP-specific operational mode during the first 30+ minutes. During this time:
- Solar panels are stowed (no/minimal solar power)
- Battery is the ONLY power source
- The EPS must power the OBC, ADCS, and potentially UHF beacon
- Power budget is extremely tight
- The EPS may need to enforce stricter power limits during LEOP than during nominal operations

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 26-28, Figure 1.8.1

**How to find it in the PDF:** Section 1.8.1 and Figure 1.8.1 "LEOP Planning"

---

### [Section 1.8.3 -- Mission-Critical Periods] (PDF pages 31-33)

**What was found:** Several mission-critical risks with EPS implications:
1. **EPS Mode Transition Challenges**: "The EPS must support multiple operational modes, including peak power demand during communication windows and science operations. Incorrect transitions between power states can lead to temporary power instability."
2. Mitigation strategies include:
   - **Power Budget Monitoring and Predictive Load Management**: OBC monitors EPS telemetry (battery charge, solar output, subsystem consumption)
   - **Preemptive Mode Switching Logic**: Before entering high-power modes, the EPS verifies sufficient power reserves are available; if battery is low, prioritize charging before power-intensive activities

**Why this matters for EPS firmware:** The EPS firmware must implement preemptive mode switching logic -- checking whether power reserves are sufficient BEFORE allowing mode transitions. This is an autonomous firmware function, not just reporting to the OBC. The EPS must also be robust against incorrect mode transitions causing power instability.

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 32-33

**How to find it in the PDF:** Section 1.8.3 bullet point "EPS Mode Transition Challenges"

---

### [Section 1.8.4 -- Mission Disposal] (PDF page 33)

**What was found:** "During Mission Disposal, every subsystem is turned on except the solar panels in order to empty the batteries and pacify the S/C before re-entry."

**Why this matters for EPS firmware:** The EPS must support a mission disposal mode where solar panels are disconnected but all subsystems are powered ON to drain the battery. This is a unique end-of-life mode that the firmware must implement. Reference document: CP0_SDMR_2026-03-08_v1.1.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 33

**How to find it in the PDF:** Section 1.8.4 "Mission Disposal"

---

### [Section 2.1 -- Adaptability Table: EPS MCU] (PDF page 34)

**What was found:** The EPS MCU entry states: "The EPS firmware isn't updatable since there is no bootloader implemented in the MCU that would allow for such and adding one is too complex and might be unreliable. The EPS is still in communication with the OBC, allowing us to adapt some predefined parameters like safety thresholds for example."

**Why this matters for EPS firmware:** This is CRITICAL:
1. The EPS firmware is NOT field-updatable -- it must be correct before launch
2. However, the EPS must support RUNTIME PARAMETER UPDATES from the OBC via UART
3. Specifically, "safety thresholds" can be updated -- this means Bmin, TempMax, TempMin, etc. must be stored in a way that the OBC can modify them
4. This implies the EPS needs either non-volatile storage for parameters or RAM-based configuration that the OBC re-sends after each boot

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF page 34, Table 2.1.1

**How to find it in the PDF:** Table 2.1.1 "Adaptability table", row "EPS MCU"

---

### [Section 2.2.1 -- Telemetry Database: EPS Entries] (PDF pages 35-36)

**What was found:** The EPS must provide the following telemetry packets:
- **HK_EPS_VOLT** (16 bytes) -- Voltage for each power line
- **HK_EPS_AMP** (16 bytes) -- Current for each power line
- **HK_EPS_BAT_INFO** (40 bytes) -- Battery info: State of charge, depth of discharge, charge and discharge cycles
- **HK_EPS_SP_INFO** (8 bytes) -- Voltage and current from each solar panel
- **HK_EPS_SP_HDRM** (1 byte) -- Status of HDRM deployment for each panel
- **HK_EPS_MCU_TEMP** (4 bytes) -- MCU temperature
- **HK_EPS_BP_TEMP** (16 bytes) -- Battery pack temperature
- **HK_EPS_SP_TEMP** (8 bytes) -- Solar panels temperature
- **HK_EPS_COMMAND_LOG** (100 bytes) -- Commands executed by EPS
- **HK_EPS_ALERT_LOG** (10 bytes) -- Alerts from internal components (e.g., current sensor)

Total EPS telemetry: ~219 bytes per housekeeping frame. The OBSW data production table (page 153) confirms EPS telemetry is 210 bytes.

**Why this matters for EPS firmware:** This is the definitive list of what the EPS firmware must measure, compute, and transmit. Each entry defines a specific data size that must be packed into the CHIPS protocol frames. Key observations:
- 16 bytes for voltage = likely 8 power lines x 2 bytes each (16-bit ADC values)
- 16 bytes for current = same structure
- 40 bytes for battery info is substantial -- SoC calculation, DoD, cycle counting
- Temperature sensing for MCU, battery pack, and solar panels
- Alert log and command log need internal circular buffers

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 35-36, Table 2.2.1

**How to find it in the PDF:** Table 2.2.1 "House-keeping data generation", EPS rows

---

### [Section 2.2.2 -- Telecommands] (PDF page 37)

**What was found:** The system supports these generic telecommands:
- REBOOT [SUBSYSTEM] -- Reboots a referenced subsystem
- SET [SUBSYSTEM] [PROPERTY] -- Updates a register parsed as argument
- READ [SUBSYSTEM] [PROPERTY] -- Asks for downlink of a register value
- PROGRAM [SUBSYSTEM] -- Re-programs software of a subsystem
- SET MODE -- Manually switches the satellite mode

**Why this matters for EPS firmware:** The EPS must handle at minimum:
- REBOOT EPS -- self-reboot capability
- SET EPS [PROPERTY] -- update runtime parameters (thresholds, etc.)
- READ EPS [PROPERTY] -- respond with current register/parameter values
- Possibly REBOOT [other subsystem] if the EPS controls power to subsystems

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 37

**How to find it in the PDF:** Section 2.2.2 "Telecommands" table

---

### [Section 2.3.1.4 -- End of Life Procedures] (PDF page 38)

**What was found:** EOL procedures include:
- Passivate battery
- Disconnect solar arrays
- Disconnect battery

**Why this matters for EPS firmware:** The EPS firmware must support these three distinct shutdown operations as telecommand-triggered actions. "Passivate battery" likely means putting the battery into a safe state. "Disconnect solar arrays" and "disconnect battery" imply the EPS has switchable paths for both, which the firmware must control.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 38

**How to find it in the PDF:** Section 2.3.1.4 "EOL procedures"

---

### [Section 3.1.1 -- Physical Architecture Block Diagram] (PDF page 43)

**What was found:** Figure 3.1.1 shows the EPS block contains: PDU, 4x Solar Panels, BMU, PMU, HDRM, BHU. The EPS connects to the OBC via I2C through the PC104 stack. The diagram shows the EPS provides 3.3V and 5V power buses. There is also an I2C connection through a MUX.

**Why this matters for EPS firmware:** This confirms the EPS functional blocks the firmware must manage:
- **PDU** (Power Distribution Unit) -- switch/control power to subsystems
- **PMU** (Power Management Unit) -- MPPT, battery charging
- **BMU** (Battery Management Unit) -- battery monitoring
- **BHU** (Battery Heater Unit) -- thermal management
- **HDRM** (Hold Down and Release Mechanism) -- solar panel deployment control
The diagram shows I2C to OBC, but we know this was changed to UART.

**Relevance:** RELEVANT (mostly confirms known architecture)

**Confidence:** Verified from PDF page 43, Figure 3.1.1

**How to find it in the PDF:** Figure 3.1.1 "CHESS Pathfinder 0 physical architecture block diagram"

---

### [Section 3.1.4 -- Functional Architecture Diagram] (PDF page 46)

**What was found:** Figure 3.1.6 shows the EPS functional flow for Measurement Mode:
1. Battery temperature sensing -> Cold? -> Battery heating
2. Electrical power generation -> Electrical Power Conditioning -> Electrical Power storage -> Electrical Power distribution
3. Subsystem Selection -> Subsystem Power Supply
4. Mode switch condition check -> Mode switch

This is a flowchart showing the EPS internal decision logic.

**Why this matters for EPS firmware:** This confirms the EPS firmware must implement:
- Temperature check loop (battery temp -> heater activation)
- Power conditioning pipeline (solar -> MPPT -> battery -> distribution)
- Subsystem power switching (selective enable/disable)
- Mode transition validation (check if mode switch conditions are met)
- The complete set of functional diagrams for ALL modes is in Section 8 (Figures 8.2-8.5)

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 46, Figure 3.1.6

**How to find it in the PDF:** Figure 3.1.6 "Functional Architecture - Measurement Mode"

---

### [Section 3.4.4.2 -- Inhibit Switch Activation Logic] (PDF page 110)

**What was found:** The spacecraft activation relies on coordinated disengagement of the RBF (Remove Before Flight) pin and the KS (Kill Switch). The RBF must be removed before integration into the dispenser. The KS remains engaged until ejection, at which point the compression mechanism releases, permitting current flow through the EPS.

**Why this matters for EPS firmware:** The EPS firmware must handle the initial power-up sequence correctly when the KS releases. There is no software involvement in the KS/RBF mechanism itself (it's purely hardware), but the EPS must detect the transition from unpowered to powered state and begin its initialization sequence correctly. This is the very first moment the EPS firmware runs.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 110

**How to find it in the PDF:** Section 3.4.4.2

---

### [Section 3.4.4.3 -- EPS Physical Description: USB-C and RBF] (PDF page 110)

**What was found:** "The ESP is split into two PCBs, which are connected via the PC104 headers. The PCDU is a board that contains the PCU and PDU circuits as well as the MCU. The board also contains the Remove Before Flight (RBF) switch." Also: "there is a USB-C connector, allowing battery charging and OBD software update while on ground, without the need of disassembling the CubeSat."

**Why this matters for EPS firmware:** 
1. The MCU is on the PCDU board, not a separate board
2. The USB-C connector can be used for ground-based battery charging AND software updates -- this is the development/test interface
3. The term "OBD software update" likely refers to On-Board Data / firmware update via USB-C, confirming there is SOME programming path even if there's no in-flight bootloader

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 110

**How to find it in the PDF:** Section 3.4.4.3 "Physical description and arrangement"

---

### [Section 3.4.5 -- Key Interfaces: PC104 Pinout] (PDF pages 111-112)

**What was found:** The PC104 interface pinout (Figure 3.4.16) defines the following EPS-related pins:

**Communication pins:**
- H1 pin 41: I2C-SDA (I2C serial data, 3.3V)
- H1 pin 43: I2C-SCL (I2C serial clock, 3.3V)
- H2 pin 6: PWR_OC (GNSS over-current detection GND=OC event, 0 to 3.3V)
- H2 pin 4: Antenna release/OUT2 (1.5 to 6V)
- H2 pin 8: Antenna release B/OUT6 (1.5 to 6V)
- H2 pin 14: BOOT (Low-level bootloader pin for EPS, 0 to 5.0V)

**Power pins:**
- H2 pins 29,30,31,32: GND (0V)
- H2 pins 45,46: V_Bat (Battery voltage bus, 6 to 8.2V)
- H2 pins 25,26: 5V_Main (Main 5V output, 4.75 to 5.25V)
- H2 pin 42: V_Bat_backup (Sleep Mode Backup voltage, 6 to 8.2V)
- H2 pin 27: 3.3V_Main (Main 3.3V output for OBC, 3.27 to 3.33V)
- H2 pin 28: 3.3V_Backup (Backup 3.3V output for OBC, 3.27 to 3.33V)

**Why this matters for EPS firmware:**
1. **BOOT pin** (H2 pin 14, 0-5V): This is a low-level bootloader pin for the EPS -- confirms a hardware mechanism exists for programming
2. **PWR_OC pin**: The EPS detects GNSS over-current events via this pin (GND = over-current). Firmware must monitor this GPIO.
3. **Antenna release pins**: The EPS controls UHF antenna deployment via OUT2 and OUT6 -- firmware must drive these GPIOs for deployment
4. **V_Bat_backup**: There is a SEPARATE "Sleep Mode Backup" voltage supply -- the EPS must maintain this even in low-power states
5. **I2C pins are listed** but we know the actual implementation uses UART. This is a doc discrepancy.
6. Footnote 6 on page 112: "We are considering two different communication protocols for EPS: either staying with the RS-422 lines or switching to an s." -- confirms the communication protocol was being reconsidered

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 111-112, Figure 3.4.16

**How to find it in the PDF:** Figure 3.4.16 "PC104 pinout" and surrounding text

---

### [Section 3.4.5.3 -- Data and Signal Routing] (PDF page 112)

**What was found:** "The EPS communicates with the OBC via an I2C interface, routed through the PC104 connectors."

**Why this matters for EPS firmware:** The document says I2C but we know this was changed to UART. This is a known discrepancy to track. The UART implementation needs to handle the same data as originally planned for I2C.

**Relevance:** RELEVANT (document discrepancy noted)

**Confidence:** Verified from PDF page 112

**How to find it in the PDF:** Section 3.4.5.3 "Data and Signal Routing"

---

### [Section 3.4.5.4 -- Umbilical Cord Interface] (PDF page 112)

**What was found:** The umbilical cord uses a USB-C connector with 18 wires:
- 12 programming wires for the OBC
- 6 power wires for battery charging and power distribution

Placed on the X+ side of the CubeSat.

**Why this matters for EPS firmware:** The 6 power wires confirm that ground-based battery charging flows through the EPS. The EPS firmware may need to detect when the umbilical is connected (ground mode vs. flight mode) to handle charging differently.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 112

**How to find it in the PDF:** Section 3.4.5.4 "Umbilical Cord Interface"

---

### [Section 3.4.8 -- Power and Energy Budget] (PDF pages 117-118)

**What was found:** Detailed power budget tables for each mode:

**Measurement Mode (1 min active):**
- EPS: Average 0.20W, Peak 0.30W, 20% margin, Voltage unspecified
- Total system: Average 8.01W, Peak 11.58W

**Safe Mode (5 hours):**
- EPS: Average 0.20W, Peak 0.30W, 20% margin
- Total system: Average 5.24W, Peak 9.16W

**Communication Mode (10 min):**
- EPS: Average 0.20W, Peak 0.30W, 20% margin
- Total system: Average 7.70W, Peak 12.50W (UHF mode only)

**Charge Mode:**
- EPS: Average 0.20W, Peak 0.30W, 20% margin
- Total system: Average 3.70W, Peak 6.50W

Key requirement: "The EPS is required to supply an average power of minimum 10W and each power bus 3.3V and 5V is able to handle a minimum peak of 2A for a duration of 2s."

Solar panel maximum expected power: 18.34V at 1.8A.

**Simulation parameters (Table 3.4.7):**
- Measurement mode: 14.34W avg, 18.48W peak, 37.15 Wh total (176 min)
- Safe mode: 5.64W avg, 7.28W peak, 31.09 Wh total (5 hours)
- Communication mode: 23.9W avg, 28.38W peak, 2.33 Wh total (5 min)
- Idle/Charging: 3.70W avg, 4.5W peak

**Why this matters for EPS firmware:**
1. The EPS itself consumes only 0.20W average -- very low power budget for the MCU + sensors
2. Peak system power demand is 28.38W during communication -- the EPS must handle this
3. Each bus must handle 2A peak for 2 seconds -- firmware must have overcurrent protection logic
4. The 20% margin on EPS power is tight -- firmware must be power-efficient
5. These numbers define the ADC thresholds and current limits the firmware must enforce

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 117-118, Tables 3.4.3-3.4.7

**How to find it in the PDF:** Tables 3.4.3 through 3.4.7 and surrounding text

---

### [Section 3.4.9 -- EPS Backup Plan: SOLO EPS-4] (PDF pages 120-121)

**What was found:** The SOLO EPS-4 by 2NDSpace is the backup if the in-house EPS is not ready. Key specs:
- Up to 12 independently controllable outputs across 3 PDUs (3.3V, 5V, 12V)
- Six independent MPPT inputs
- Fully integrated BMS
- Hardware-level overcurrent, overvoltage, and undervoltage protection
- Communicates via I2C, CAN, RS485, plus UART debug
- 50 Wh energy storage, 104W peak output
- 4S1P 18650 Li-ion battery, 16.8V charge voltage
- Supports KS, RBF, OBF, GND-CHG functions

The in-house EPS targeted 3.3V, 5V, and 8.4V buses; the SOLO provides 3.3V, 5V, and 12V. The 8.4V bus is not natively available from SOLO.

**Why this matters for EPS firmware:** If the backup EPS is used, our custom firmware is NOT needed (the SOLO has its own firmware). However, this section reveals what our in-house EPS must match:
- The custom EPS was targeting 3.3V, 5V, and 8.4V buses (8.4V for ADCS)
- The ADCS accepts 5-17V input, so voltage flexibility exists
- Our firmware must implement equivalent functionality: MPPT, BMS, OCP/OVP/UVP, load switching
- The SOLO communicates via I2C/RS485/CAN -- our custom uses UART -- this is a key difference

**Relevance:** RELEVANT (defines feature parity requirements)

**Confidence:** Verified from PDF pages 120-121

**How to find it in the PDF:** Section 3.4.9 "EPS Backup Plan -- SOLO EPS-4 (2NDSpace)"

---

### [Section 3.6.2 -- EPS Radiation Protection] (PDF page 136)

**What was found:** Section 3.6.2 for EPS is extremely brief. The full text of the EPS radiation section is essentially contained in the earlier OBC section. The key information is:
- The OBC uses TPS25940 e-fuses for undervoltage/overvoltage protection (UV lockout at 4.15V/2.7V, OV lockout at 5.3V/3.55V for 5V/3.3V buses)
- Power path controllers select between main and backup power sources
- Both MPU and MCU have internal watchdogs with separate reference clocks
- Watchdog timeout values are yet to be settled, will be fine-tuned during testing

**Why this matters for EPS firmware:**
1. The EPS firmware must configure and service the internal watchdog timer on the SAMD21G17D
2. The e-fuses (TPS25940) are on the OBC side, but the EPS must be aware of their UV/OV thresholds since it supplies the buses
3. The EPS must keep 5V_Main above 4.15V and 3.3V_Main above 2.7V to avoid triggering the OBC's UV lockout
4. If the EPS bus droops below these thresholds, the OBC will lose power -- this is a hard constraint

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 135-136

**How to find it in the PDF:** Section 3.6.1 (OBC protection applies to EPS-supplied buses) and Figure 3.6.1

---

### [Section 3.5.2 -- OBC Communication with EPS: RS-422/UART] (PDF page 132)

**What was found:** "For communication, RS-422 is used for low-data-rate links due to its differential and full-duplex nature, while UART serves as a backup to minimize pin usage on the PC104. I2C is also chosen for the low data-rate lines or backup lines to some COTS systems like the Gnomespace EPS and the SatNogs transceiver."

**Why this matters for EPS firmware:** This confirms the communication hierarchy:
- RS-422 is the PRIMARY for low-data-rate links (differential, robust)
- UART is BACKUP
- I2C is for COTS systems
- The mention of "Gnomespace EPS" suggests an earlier EPS design reference (Gnomespace is now GomSpace)
- Our EPS uses UART (as per our implementation decision), but RS-422 was the original plan

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 132

**How to find it in the PDF:** Section 3.5.2 last paragraph before Section 3.6

---

### [Section 3.6 -- OBC Cold Redundancy and Heartbeat] (PDF pages 133-134)

**What was found:** The OBC has two independent computers (A and B) with a heartbeat-based failover. Computer A sends a GPIO heartbeat to Computer B's M4 microcontroller. If B detects heartbeat loss, it switches the MUX from A to B. This means the EPS must supply power to BOTH computers, though only one is active at a time.

**Why this matters for EPS firmware:** The EPS must provide:
- Separate power to Computer A and Computer B (both need to stay powered even when inactive, at least the M4 on B)
- The EPS may need to detect which computer is active (via some signal or by which UART port is responsive)
- If the MUX switches from A to B, the EPS's UART partner changes -- the EPS firmware must handle this transparently
- The EPS may need to support power cycling individual OBC computers as a recovery mechanism

**Relevance:** RELEVANT

**Confidence:** Verified from PDF pages 133-134

**How to find it in the PDF:** Section 3.6.1 "OBC radiation mitigation strategies" - Cold redundancy subsection

---

## MEDIUM PRIORITY FINDINGS

---

### [Section 3.7.3.1 -- OBSW Hardware Interaction: CHIPS Protocol] (PDF page 142)

**What was found:** "UART-based subsystem communication is implemented via CHIPS, which provides a master-initiated, transaction-based communication scheme with sequence control, CRC-protected framing and deterministic retransmission."

The OBC uses the manager-driver pattern:
- Application layer: EventActionSM (mode FSM)
- Manager layer: Subsystem managers (one per subsystem including EPS)
- Driver layer: UART/I2C hardware drivers

**Why this matters for EPS firmware:** The EPS is a SLAVE/RESPONDER in the CHIPS protocol. The OBC initiates all transactions. The EPS firmware must:
- Wait for incoming CHIPS frames
- Parse and validate CRC
- Respond with requested data or ACK/NACK
- Never initiate communication unsolicited (master-initiated)
- Handle sequence numbers for retransmission tracking

**Relevance:** RELEVANT (other agents cover CHIPS in detail, but this confirms the master-slave architecture)

**Confidence:** Verified from PDF page 142

**How to find it in the PDF:** Section 3.7.3.1.3 "Hardware Interaction Architecture"

---

### [Section 3.7.3.3 -- EventActionSM States] (PDF page 146)

**What was found:** The flight software FSM has the following states:
- LEOP
- CHARGE
- MEASURE
- COM
- SAFE (with sub-states: SAFE_BASE, SAFE_COM, SAFE_CHARGE, SAFE_DETUMBLE, SAFE_REBOOT)

"EventAction has been designed to never take control of, or individually command, a sub-system. Instead it only broadcasts the global operating mode of the satellite and lets the individual sub-system managers align their internal state machines with this mode."

**Why this matters for EPS firmware:** The OBC broadcasts mode changes to the EPS. The EPS does NOT receive direct commands for mode-specific actions -- instead, it receives the mode identifier and must internally decide what to do. This means the EPS firmware needs its OWN internal state machine that maps:
- LEOP -> EPS LEOP behavior (conservative power)
- CHARGE -> EPS charging behavior (maximize solar harvest)
- MEASURE -> EPS measurement behavior (enable payload power)
- COM -> EPS communication behavior (enable UHF power)
- SAFE_* -> EPS safe behaviors (load shedding, etc.)

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 146-147

**How to find it in the PDF:** Section 3.7.3.3.3 "Spacecraft and Subsystem Modes Implementation"

---

### [Section 3.7.3.3 -- SAFE_REBOOT State] (PDF page 147)

**What was found:** "SAFE_REBOOT: reboots the key sub-systems periodically until ground intervention."

"Furthermore, EventAction has been designed to never take control of, or individually command, a sub-system... However, the SAFE state sequences have a fall-back if the essential tasks (such as, charging in SAFE_CHARGE state or detumbling in SAFE_DETUMBLE state) are not completed by a sub-system manager in time Tmax, which when crossed puts the CubeSat in SAFE_COM state and waits for the ground to intervene."

**Why this matters for EPS firmware:** When the satellite enters SAFE_REBOOT, the EPS may need to power-cycle other subsystems (OBC, ADCS, UHF) on command. If SAFE_CHARGE times out (charging not completed in Tmax), it escalates to SAFE_COM. The EPS must handle these timeouts even if the OBC is being rebooted.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 147

**How to find it in the PDF:** Section 3.7.3.3.3, EventAction description

---

### [Section 3.10.3 -- UHF Antenna Deployment Mechanism] (PDF pages 218-220)

**What was found:** The UHF antenna deployment uses a burn-wire release mechanism:
- Supply voltage: 5V, current: 250mA during deployment, 1mA idle
- Two independent deployment output lines (redundant)
- Two deployment-status inputs (switches/sensors for verification)
- The EPS controls the burn-wire circuit via the SatNOGS-COMMS antenna deployment connector
- Deployment sequence: Primary burn -> verify via sensors -> if fail, secondary burn -> verify -> if fail, retry at predefined intervals

**Why this matters for EPS firmware:** The EPS firmware directly controls the antenna deployment:
- Must drive the burn-wire GPIO (5V, 250mA) for a controlled duration
- Must read deployment verification sensors
- Must implement retry logic with predefined intervals
- Must coordinate with PC104 pins OUT2 (H2 pin 4) and OUT6 (H2 pin 8) as identified in the pinout
- This is a one-shot destructive operation -- firmware must protect against accidental triggering

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 218-219

**How to find it in the PDF:** Section 3.10.3.1 "ADM Quick Facts" and 3.10.3.2 "ADM Functional description"

---

### [Section 3.11 -- Thermal Analysis: EPS Component Temperature Ranges] (PDF pages 230-232)

**What was found:** Table 3.11.7 shows thermal simulation results for EPS components:

| Component | Operational Range | Calculated Range | Predicted Range (with +/-15C uncertainty) |
|-----------|-------------------|------------------|------------------------------------------|
| EPS Batteries | -20 to 60C | 9.03 to 40.52C | -5.97 to 55.52C |
| EPS PCDU Board | -55 to 150C | 9.93 to 42.58C | -5.07 to 57.58C |
| EPS Solar Panels | -40 to 105C | -68.07 to 105.71C | -83.07 to 120.71C |

Note: Solar panels predicted range EXCEEDS operational limits (red highlighted in the table). Battery predicted range is within limits but close to upper bound.

Table 3.11.6 shows EPS heat generation by mode:
- EPS Battery: 160 mW in all modes
- EPS PCDU Board: 200 mW in all modes

The note states: "The battery heater is actively controlled using measured battery temperature. The charging limit must therefore be interpreted separately from the global modelling uncertainty."

**Why this matters for EPS firmware:**
1. Battery temperature is the CRITICAL thermal parameter (must stay -20 to 60C)
2. The EPS must activate heaters when battery temp approaches lower limit (-20C, though predicted minimum is -5.97C)
3. Solar panel temperatures can reach extreme values (-83C to +121C predicted) -- the EPS must handle these through the temperature sensors
4. The EPS PCDU has wide margins (-55 to 150C operational, -5 to 58C predicted)
5. The charging limit is temperature-dependent -- the EPS firmware must gate battery charging based on temperature

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 230-232, Tables 3.11.6 and 3.11.7

**How to find it in the PDF:** Tables 3.11.6 "Heat Generation depending on Mode" and 3.11.7 "Temperature ranges under hot case conditions"

---

### [Section 3.12 -- EMC: Grounding Scheme] (PDF pages 231-234)

**What was found:** "Grounding follows a star configuration, where all ground pins converge at a single node, which is then connected to the satellite structure." The PC104 uses 4 ground pins. No specific EMC timing constraints or switching frequency limitations were mentioned for the EPS.

**Why this matters for EPS firmware:** The star grounding scheme means the EPS must be careful about ground loops and noise injection. The EPS switching converters (buck/MPPT) generate EMI that could affect other subsystems. However, no specific firmware-level EMC constraints were identified -- this is primarily a hardware concern.

**Relevance:** LOW RELEVANCE (hardware design concern, not firmware)

**Confidence:** Verified from PDF pages 111, 231-234

**How to find it in the PDF:** Section 3.4.5.1 and Section 3.12

---

### [Section 3.8.1 -- TT&C: UHF Radio Power Requirements] (PDF pages 160-161)

**What was found:** UHF transceiver specs:
- Uplink: 19.2 kbps, 435-438 MHz, GMSK
- Downlink: 19.2 kbps, 435 MHz
- Beacon: 9.6 kbps, 435 MHz

The transceiver (SatNOGS-COMMS) power consumption varies by mode per the power budget tables:
- Communication mode: Transceiver at 4.00W avg, 6.00W peak (UHF only)
- Charge mode: Transceiver at 2.00W avg, 2.80W peak (beacon only)

**Why this matters for EPS firmware:** The UHF transceiver is a significant power consumer (up to 6W peak). The EPS must:
- Provide stable 5V or 6V to the transceiver (see power budget voltage column)
- Handle the 2A peak transient when the transmitter keys up
- Track transceiver power state to adjust power budget calculations

**Relevance:** RELEVANT

**Confidence:** Verified from PDF pages 160-161, power budget tables on page 117-118

**How to find it in the PDF:** Tables 3.8.1/3.8.2 and power budget Tables 3.4.3-3.4.6

---

### [Section 4.3/4.4 -- System Verification Plan: EPS] (PDF pages 262-265)

**What was found:** Table 4.2 shows EPS subsystem status:
- EPS Battery Pack (COTS BackUp): Heritage Yes, In progress, 24 weeks lead time
- EPS PCDU: Heritage No, Design iteration ongoing, 1 month lead time

Platform testing models:
- **Platform-EFM**: FlatSat comprising EPS, OBC, UHF Antenna, SatNOGS transceiver, ADCS -- for electrical functional testing
- **Platform-QM**: QMs of OBC, EPS, and outer structure for qualification (environmental testing, EMC, vibration, TVT)
- **Platform-FM**: FMs of all subsystems

The System Verification Plan (Figure 4.2) shows: Engineering Development Model -> Functional testing -> Electronics update -> CHESS CONOPS (software testing, FlatSat testing, operations at EPFL)

**Why this matters for EPS firmware:** 
1. The EPS PCDU has NO heritage -- it is a new design. Extra testing is needed.
2. The FlatSat (Platform-EFM) will be the primary firmware development platform
3. The firmware must be ready for functional testing on the FlatSat before qualification
4. The CHESS CONOPS phase involves operations testing -- the firmware must support this

**Relevance:** RELEVANT

**Confidence:** Verified from PDF pages 262-265

**How to find it in the PDF:** Table 4.2 and Figure 4.2

---

### [Section 3.7.6 -- NEST Simulation Framework] (PDF pages 158-159)

**What was found:** NEST (Numerical Environment for Software Testing) uses QEMU virtual machines and WebAssembly-based digital twins to emulate the satellite. The test setup includes "an ARM M4 microcontroller monitoring a heartbeat signal coming from the main SoC." The system can emulate "I2C, UART and GPIO devices."

**Why this matters for EPS firmware:** The EPS firmware can be tested using the NEST framework with a digital twin. This means we should design the EPS firmware to be testable via:
- UART interface (CHIPS protocol)
- GPIO monitoring (heartbeat, deployment signals)
- The digital twin should replicate EPS sensor readings and power state
- The EPS firmware might need a simulation/test mode that provides synthetic sensor data

**Relevance:** RELEVANT (testing infrastructure)

**Confidence:** Verified from PDF pages 158-159

**How to find it in the PDF:** Section 3.7.6.3 and Figure 3.7.8

---

## LOW PRIORITY FINDINGS

---

### [Section 3.2.1 -- Novoviz SPAD Camera Power Requirements] (PDF pages 48-50)

**What was found:** The SPAD Camera:
- Supply voltage: 5V Main, 5V Backup
- Peak power: 4W, Average power: 1W
- Communication: UART
- Active imaging windows limited to 5 minutes to prevent battery depletion
- Measurement mode total sequence: ~14 minutes, most at 2W, imaging at 4W peak

**Why this matters for EPS firmware:** The EPS must supply 5V to the camera and be prepared for 4W peak draw during 10-minute imaging windows. The EPS must be able to cut power to the camera in Safe Mode. The 5-minute active imaging limit is enforced by the OBC/OBSW, not the EPS directly.

**Relevance:** RELEVANT (power provisioning)

**Confidence:** Verified from PDF pages 48-50, Table 3.2.1 and 3.2.2

**How to find it in the PDF:** Table 3.2.1 "Novoviz Payload quick facts table" and Table 3.2.2

---

### [Section 3.2.2 -- GNSS Payload Power Requirements] (PDF pages 64-65)

**What was found:** GNSS payload:
- Supply: 5V Main via PC104 (4.75 to 5.25V)
- Power consumption: 52-308 mA (171-1016 mW) depending on scenario
- Has ENABLE pin and PWR_OC (over-current) detection pin connected to EPS
- Max power ~1W with two receivers running at 20 Hz

**Why this matters for EPS firmware:** The EPS must:
- Supply 5V to the GNSS
- Monitor the PWR_OC (over-current) signal from pin H2-6
- Be able to disable GNSS power via the ENABLE signal
- Handle up to ~300mA peak on the GNSS 5V line

**Relevance:** RELEVANT (power provisioning, OC detection)

**Confidence:** Verified from PDF pages 64-65, Figure 3.2.11 and Table 3.2.9

**How to find it in the PDF:** Figure 3.2.11 "GNSS pinout" and Table 3.2.9 "Power measurement"

---

### [Section 3.3 -- AOCS Power Interactions] (PDF pages 64-66)

**What was found:** The ADCS (CubeSpace CubeComputer + CubeControl + CubeMag + CubeWheel):
- ADCS Core: -18 to 78C operational, 1.52W average, 1.88W peak
- Reaction Wheels: 4x at 300mW each = up to 1.2W
- Magnetorquers: 3x at 188mW (nominal), 1200mW during detumbling
- ADCS subsystem gets power from V_Bat line (supports 5-17V input)
- ADCS is ON in ALL modes

**Why this matters for EPS firmware:** The ADCS is always consuming power and connects to the V_Bat bus directly (6-8.2V). During detumbling, magnetorquer power spikes to 1.2W. The EPS firmware must account for this constant load and be aware of detumbling power spikes.

**Relevance:** RELEVANT (load characterization)

**Confidence:** Verified from PDF pages 64-66, power budget tables pages 117-118

**How to find it in the PDF:** Section 3.3.1 AOCS Quick Facts and power budget tables

---

### [Section 3.7.3.3 -- OBSW Telemetry Packet Structure] (PDF page 153)

**What was found:** The OBSW encapsulates subsystem telemetry with a specific packet format:
- 16-bit start word (always 0x2CCF)
- 12-bit data length
- 4 unused bits
- Body (up to 4096 bytes)
- 256-bit checksum

EPS telemetry packet size: 210 bytes per the subsystem table.

**Why this matters for EPS firmware:** The 210-byte EPS telemetry packet must be assembled by the EPS firmware and sent to the OBC when requested via CHIPS. This defines the maximum CHIPS payload size for EPS telemetry responses. The OBC will wrap this in the packet encapsulation format for downlink.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 153, Table 3.7.2

**How to find it in the PDF:** Table 3.7.2 and Figure 3.7.7

---

## IDENTIFIED GAPS AND MISSING INFORMATION

1. **Threshold Values Not Specified:** Bmin, BminCrit, TempMax, TempMin, TimeMax, TimeMax2, TumbMax, TumbMin are referenced throughout but never given concrete values. These must be defined for firmware implementation.

2. **EPS Internal State Machine Not Documented:** The document describes the OBC's EventActionSM in detail but does NOT describe how the EPS internally responds to mode changes. This must be designed.

3. **CHIPS Protocol Details for EPS:** The document describes CHIPS in general but does NOT specify the exact command IDs, response formats, or register map for the EPS subsystem. This is likely in a separate ICD document.

4. **Battery Charging Algorithm Not Specified:** No details on CC/CV charging parameters, maximum charge rate, charge termination voltage, or temperature-dependent charging limits are provided in this document.

5. **MPPT Algorithm Not Specified:** No details on the MPPT approach (perturb-and-observe, incremental conductance, etc.) are provided.

6. **EPS Watchdog Configuration:** The document mentions internal watchdogs but says "the exact implementation of these (Timeout values etc...) are yet to be settled on."

7. **8.4V Bus Mentioned but Not Detailed:** The in-house EPS was targeting 3.3V, 5V, and 8.4V buses, but the 8.4V bus is only mentioned in the SOLO EPS-4 comparison section. Its load requirements are not separately documented.

8. **EPS-Specific Verification Tests:** Section 4.4.4 for EPS verification was expected on page 263 but the PDF only shows the general system verification plan and model philosophy. No specific EPS test procedures are defined.

9. **Solar Panel HDRM Control Logic:** The EPS telemetry includes HK_EPS_SP_HDRM but the document does not detail how the EPS controls the solar panel hold-down and release mechanisms.

10. **Sleep/Low-Power Mode:** V_Bat_backup pin suggests a sleep mode exists, but no description of EPS low-power modes or sleep behavior is provided.

---

## CROSS-REFERENCES FOUND

- Section 1.7 modes are elaborated in Section 3.3 (AOCS mode definitions) and Section 3.7.3.3 (OBSW mode implementation)
- Figure 8.1 "Mode Switch Decision Tree" -- referenced on page 20, located on the last page of the document -- NOT READ YET, should contain the complete mode transition logic
- Figures 8.2-8.5 contain functional architecture diagrams for ALL operational modes
- Product tree: document 09-CP0_SPT_2025-11-15_v0.0
- Space Debris Mitigation Report: CP0_SDMR_2026-03-08_v1.1
- Section 3.8.4 "Link and Data Budget Summary" for detailed communication budget
- The EPS internal design details may be in separate EPS-specific documents not included in this satellite project file

---

## SUMMARY OF CRITICAL FIRMWARE REQUIREMENTS DISCOVERED

1. **Boot-first architecture** -- EPS is the first system to power up, must work autonomously
2. **LEOP-specific conservative power mode** -- before solar panels deploy, battery is only power source
3. **SoC-based mode gating** -- must verify battery level before allowing mode transitions
4. **Safe Mode load shedding** -- must shed non-essential loads in all 4 Safe Mode sub-states
5. **EPS self-reboot capability** -- SAFE_REBOOT can reboot the EPS (TimeMax2 escalation)
6. **Runtime parameter updates** -- safety thresholds updatable via OBC (no bootloader)
7. **Antenna deployment control** -- burn-wire GPIOs with retry logic
8. **Thermal management** -- battery heater activation based on temperature
9. **210-byte telemetry packet** -- must assemble and respond to OBC queries
10. **OBC cold redundancy awareness** -- must work with either OBC Computer A or B
11. **Pre-launch charging mode** -- USB-C ground charging support
12. **Mission disposal mode** -- disconnect solar panels, drain battery
13. **UV/OV protection awareness** -- must keep buses above OBC e-fuse lockout thresholds
14. **Internal watchdog** -- must be configured and serviced (parameters TBD)
15. **Power budget enforcement** -- 10W average, 2A peak per bus for 2s

---

## LATE-DISCOVERED CRITICAL FINDINGS

---

### [Section 8 / Figure 8.1 -- Mode Switch Decision Tree] (PDF pages 300-301)

**What was found:** Figure 8.1 is the COMPLETE mode switch decision tree showing all transitions between satellite operational modes. Table 8.1 provides the legend. The key decision nodes that involve battery charge (EPS-provided data) are:

**Battery-charge-gated transitions:**
- **COM.1**: "Battery charge above Communication Charge threshold" -- required before entering UHF Communication Mode from Charging Mode
- **SB.1**: "Battery charge above S-band Downlink Charge threshold" -- required before entering X-Band Downlink Mode
- **MS.1**: "Battery charge above Measurement Charge threshold" -- required before entering Measurement Mode from Charging Mode

**Safe Mode triggers from any mode:**
- **SM-CH**: SAFE flag raised during CHARGING mode
- **SM-UHF**: SAFE flag raised during UHF-COM mode
- **SM-SB**: SAFE flag raised during SBAND-COM mode
- **SM-MS**: SAFE flag raised during MEASUREMENT mode
- **SM-GENERAL**: Detection of a SAFE flag triggers general SAFE mode
- **SM-EXIT**: No SAFE mode condition is active anymore

**Mode transition flow:**
- Charging Mode is the HUB -- all nominal transitions go through it
- From Charging: COM.1 check -> UHF Communication Mode (COM.2 -> COM.3 -> COM.4 -> back to Charging)
- From Charging: SB.1 check -> X-Band Downlink Mode (SB.2 -> SB.3 -> SB.4 -> SB.5 -> back to Charging)
- From Charging: MS.1 check -> Measurement Mode (MS.2 -> MS.3/MS.5 -> MS.4 -> back to Charging)
- Any mode can trigger Safe Mode via SM-* flags
- Safe Mode exits via SM-EXIT back to Charging Mode

The diagram shows 5 operational modes: Measurement Mode, Charging Mode, UHF Communication Mode, X-Band Downlink Mode, and Safe Mode. Each mode shows which subsystems are active (GNSS, OBC, EPS are always ON in nominal modes; ADCS varies).

**Why this matters for EPS firmware:** This is the DEFINITIVE mode transition logic. The EPS firmware MUST:
1. Continuously report battery charge level (SoC) to the OBC
2. The OBC uses three DIFFERENT battery thresholds for mode transitions:
   - "Communication Charge threshold" (for UHF COM mode)
   - "S-band Downlink Charge threshold" (for X-Band mode)
   - "Measurement Charge threshold" (for science mode)
3. The EPS must support these as configurable parameters (matches the adaptability table requirement)
4. The EPS is shown as ALWAYS ON (green) in every mode including Safe Mode
5. Safe Mode can be triggered from ANY operational mode
6. All nominal mode transitions go through Charging Mode -- meaning the battery must recharge between operations

**Relevance:** CRITICAL -- RELEVANT

**Confidence:** Verified from PDF pages 300-301, Figure 8.1 and Table 8.1

**How to find it in the PDF:** Section 8 "Figures", Figure 8.1 "Mode Switch Decision Tree" and Table 8.1

---

### [Section 4.7.3 -- Testing Capabilities via Umbilical] (PDF page 281)

**What was found:** Table 4.5 confirms:
- Battery recharge through umbilical: YES (enabled by umbilical connection on EPS board)
- Receive telecommands and send telemetry through umbilical: YES (planned functionality)
- Switch on/off through umbilical: initially No, NOW will be included in design
- Software patching/parameter adjustment through umbilical: YES (currently limited to OBC F Prime image; evaluating for other subsystems)

**Why this matters for EPS firmware:** The "switch on/off through umbilical" being added to the design means the EPS firmware may need to support a ground-test power cycling command. Also, "evaluating for other subsystems" for software patching suggests the team may eventually want EPS firmware update capability through the umbilical, even if not through the in-flight UART.

**Relevance:** RELEVANT

**Confidence:** Verified from PDF page 281, Table 4.5

**How to find it in the PDF:** Table 4.5 "Testing Capabilities table"

---

### [Section 4.9.1 -- FlatSat Testing Platform] (PDF pages 282-284)

**What was found:** The FlatSat is a 60cm x 10cm backplane PCB with PC104 connectors for all subsystems. Five units ordered. Currently available subsystems for testing: OBC, ADCS Engineering Model, UHF transceiver. EPS Engineering Model is being tested in isolation and will be added to the FlatSat when ready.

"In this first round of testing, the focus will be on verifying requirements of the EPS, OBC, and Flight Software, with particular attention to subsystem interactions such as correct power delivery from the EPS and data exchange between the OBC and other subsystems."

**Why this matters for EPS firmware:** The EPS firmware will first be validated on the FlatSat. The primary tests are:
1. Correct power delivery (voltage regulation, load switching)
2. Data exchange with OBC (CHIPS protocol over UART)
3. Subsystem interaction (mode changes, telemetry acquisition)

**Relevance:** RELEVANT (defines first testing environment)

**Confidence:** Verified from PDF pages 282-284

**How to find it in the PDF:** Section 4.9.1 "Flatsat"

---

### [Section 7 -- Project Schedule: Launch Target] (PDF page 299)

**What was found:** The launch is targeted for mid-2027. The mission is currently in Phase C (design finalization and testing). FDR (Final Design Review) is scheduled for spring 2026 under ESA supervision.

**Why this matters for EPS firmware:** This provides the timeline constraint. The EPS firmware must be mature enough for FlatSat testing in 2026 and fully flight-qualified before mid-2027 launch.

**Relevance:** RELEVANT (schedule context)

**Confidence:** Verified from PDF page 299

**How to find it in the PDF:** Section 7 "Schedule" and Figure 7.1
