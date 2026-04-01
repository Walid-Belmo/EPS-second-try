# EPS Sensors & DC-DC Converter Research Log

**Status**: COMPLETE
**Date**: 2026-04-01
**Source PDF**: chess sattelite main doc-1.pdf (Ref. CP0_SATPF_2026-03-08 Issue 1 Revision 1)
**SAMD21 Datasheet**: samd21_datasheet.pdf (DS40001882H)

---

## 1. SENSORS

The EPS MCU (SAMD21RT) communicates with three distinct types of measurement ICs, plus receives analog signals from additional components. The document describes a "triple sensing" redundancy approach where three independent sensors measure the same electrical quantity on each power path (PDF p.106).

### 1.1 INA226 (referred to as "INA226" and "ina246" and "INA226AIDGSR" in the doc)

| Property | Value | Source |
|---|---|---|
| **Part Number** | INA226AIDGSR | PDF p.108, Table 3.4.2 |
| **Interface** | I2C | PDF p.95 (MCU roles list: "from ina246 voltage and current measurements ICs via I2C") |
| **What it measures** | Current and voltage (power monitoring) | PDF p.98, p.108 |
| **Location** | Listed on the ESAPL (European Space Agency Parts List) | PDF p.98 |
| **Role** | One of three independent current measurement devices per power path | PDF p.98 |
| **I2C Address** | **NOT SPECIFIED in the PDF** | - |
| **Measurement Range** | **NOT SPECIFIED in the PDF** | - |
| **Resolution** | **NOT SPECIFIED in the PDF** | - |
| **MCU Pin** | **NOT SPECIFIED** -- connects via I2C bus (which SERCOM instance is not stated) | - |

**Notes and ambiguities:**
- The document refers to "ina246" on PDF p.95 but "INA226" on p.98 and "INA226AIDGSR" in Table 3.4.2 on p.108. The INA246 and INA226 are different parts (INA246 is a high-side current sense amplifier with analog output; INA226 is a digital I2C power monitor). Given that the table on p.108 says "INA226AIDGSR" and the MCU roles list says "I2C", the correct part is almost certainly the **INA226**. The "ina246" reference on p.95 is likely a typo.
- **FLAG**: The INA226 has a configurable I2C address via A0/A1 pins (addresses from 0x40 to 0x4F). The actual address depends on the PCB design and is NOT specified in this document. Multiple INA226 units are used (at least one per power path), so they must have different addresses.
- Confidence: Part number verified from PDF p.108 Table 3.4.2. Interface verified from PDF p.95. Specific I2C address and pin mapping are NOT in this document.

### 1.2 LT6108 (High-Side Current Sense Amplifier)

| Property | Value | Source |
|---|---|---|
| **Part Number** | LT6108AIMS8 | PDF p.108, Table 3.4.2 |
| **Interface** | Analog output to MCU ADC | PDF p.98 |
| **What it measures** | High-side current sensing | PDF p.98 |
| **Role** | Overcurrent protection (comparator function) + analog current reading for MCU | PDF p.98, p.108 |
| **MCU Connection** | Analog output routed to MCU ADC pin | PDF p.98 |
| **ADC Pin** | **NOT SPECIFIED** | - |

**Notes:**
- The LT6108 is used both for analog current monitoring (output to MCU ADC) and as a comparator for overcurrent protection (trips at a threshold set by R_sense and the internal 400 mV reference). See PDF p.98.
- ESARAD close match: LT6108IMS8 (PDF p.108).
- Confidence: Part number verified from PDF p.108. Interface verified from PDF p.98. Exact ADC pin not specified.

### 1.3 TPS2590 eFuse (IMON Current Monitor Output)

| Property | Value | Source |
|---|---|---|
| **Part Number** | TPS25940ARVCR | PDF p.108, Table 3.4.2 |
| **Interface** | Analog (IMON pin) to MCU ADC | PDF p.98 |
| **What it measures** | Current monitoring via IMON output | PDF p.98 |
| **Role** | eFuse protection + analog current monitoring | PDF p.98, p.108 |
| **MCU Connection** | IMON pin routed to MCU | PDF p.98 |
| **ADC Pin** | **NOT SPECIFIED** | - |

**Notes:**
- The TPS25940ARVCR provides protection (overcurrent, reverse current, short circuit) AND an analog IMON output proportional to the load current.
- The document refers to it as "TPS2590 eFuse" on p.98 but the full part number in Table 3.4.2 (p.108) is TPS25940ARVCR.
- Confidence: Part number verified from PDF p.108. Interface verified from PDF p.98.

### 1.4 Temperature Sensor (Battery Pack)

| Property | Value | Source |
|---|---|---|
| **Part Number** | **NOT SPECIFIED** | - |
| **Interface** | SPI | PDF p.95 ("Read Battery Pack telemetry (Temperature via SPI)") |
| **What it measures** | Battery pack temperature | PDF p.94, p.95 |
| **Role** | Battery thermal monitoring for heater control and charging safety | PDF p.94, p.97-98 |
| **MCU Connection** | SPI bus | PDF p.95 |

**Critical gap -- FLAG:**
- The temperature sensor part number is **never stated** in the EPS section. The document says "Temperature Sensor" in Figure 3.4.1 (p.95) and mentions "temperature sensors" on the battery pack (p.94), but no specific IC is named.
- The interface is SPI per p.95 MCU roles list. This is unusual for a temperature sensor (most use I2C or analog). Common SPI temperature sensors include MAX31855 (thermocouple), MAX31865 (RTD), or TMP126 (digital). The actual part is unknown from this document.
- The heater resistance is 22 ohm, typical power 6 W (PDF p.95, Table 3.4.1).
- There will be two heaters, one between each pair of cells (PDF p.116).
- Confidence: Interface (SPI) verified from PDF p.95. Part number is MISSING.

### 1.5 Voltage Measurement (Resistive Dividers to MCU ADC)

| Property | Value | Source |
|---|---|---|
| **Method** | Resistive voltage dividers | PDF p.98, Figure 3.4.3 (p.99) |
| **Interface** | Analog to MCU ADC | PDF p.96 ("current and voltage measurements via Analog I/O Pin") |
| **What it measures** | Solar panel voltage, battery voltage, system bus voltage | PDF p.97, p.99 |
| **MCU Connection** | ADC pins (via Analog I/O) | PDF p.96 |
| **Voltage ranges** | Battery: 5V min to 8.4V max; Solar array: 8V min to 25V max | PDF p.99 |

**Notes:**
- Figure 3.4.3 (p.99) shows the schematic of voltage dividers. These are not ICs but passive resistor networks that scale high voltages down to the SAMD21 ADC input range (0 to VREF, typically 0 to 3.3V).
- The SAMD21 ADC is 12-bit, 350 ksps, with up to 20 channels, differential and single-ended input, 1/2x to 16x programmable gain (SAMD21 datasheet p.2).
- Confidence: Verified from PDF p.96, p.98-99, Figure 3.4.3.

### 1.6 LM139 Comparator (NOT a sensor per se, but provides digital flags to MCU)

| Property | Value | Source |
|---|---|---|
| **Part Number** | LM139DR | PDF p.108, Table 3.4.2 |
| **Interface** | Digital flags to MCU GPIO | PDF p.99-100 |
| **What it monitors** | Overvoltage and undervoltage thresholds | PDF p.99 |
| **Role** | Hardware protection -- drives eFuse enable signals AND digital flag lines to MCU | PDF p.99-100 |

**Notes:**
- The LM139 is a quad comparator. Each channel monitors a voltage (battery or solar array) against a trip threshold set by resistive dividers referenced to 3 VDC (PDF p.99).
- Each comparator output is routed both to the hardware cutoff logic AND to a dedicated digital flag line readable by the MCU (PDF p.100).
- Confidence: Verified from PDF p.99-100, p.108 Table 3.4.2.

### Sensor Summary Table

| Sensor | Interface | Measures | MCU Connection | Part Number |
|---|---|---|---|---|
| INA226AIDGSR | I2C | Current + Voltage | I2C bus (SERCOM?) | Verified (p.108) |
| LT6108AIMS8 | Analog | High-side current | ADC pin | Verified (p.108) |
| TPS25940ARVCR | Analog (IMON) | Current | ADC pin | Verified (p.108) |
| Temperature sensor | SPI | Battery temperature | SPI bus (SERCOM?) | **UNKNOWN** |
| Resistive dividers | Analog | Voltage (SA, Bat, Bus) | ADC pins | Passive (p.99) |
| LM139DR | Digital flags | OV/UV fault status | GPIO pins | Verified (p.108) |

---

## 2. DC-DC BUCK CONVERTER

### 2.1 Converter IC

| Property | Value | Source |
|---|---|---|
| **Part Number** | EPC2152 | PDF p.96, p.108 Table 3.4.2 |
| **Description** | GaN half-bridge IC (80V, 12mOhm symmetric half-bridge with UVLO & ESDs) | PDF p.96, Figure 3.4.2 (p.97) |
| **Why selected** | Radiation robustness (tested at CERN), SEE immunity, ESA recommendation, low cost | PDF p.96 |
| **Radiation status** | Tested by CERN | PDF p.108 Table 3.4.2 |

### 2.2 Switching Frequency

| Property | Value | Source |
|---|---|---|
| **Switching frequency** | **300 kHz** (fixed) | PDF p.96 |

### 2.3 Input/Output Specs

| Property | Value | Source |
|---|---|---|
| **Input voltage range** | 8.2V - 18.34V (from solar array via 2x2 panel config) | PDF p.94 Table 3.4.1 |
| **Max solar panel input (design)** | 23V at 0.5A (preliminary design assumption) | PDF p.96 |
| **Output voltage** | 8V (to battery bus) | PDF p.96 |
| **Output voltage ripple** | 25-40 mV | PDF p.96 |
| **Target inductor current ripple** | 30% | PDF p.96 |
| **Calculated inductance** | 47 uH | PDF p.96 |
| **Calculated output capacitance** | 4 uF (10 uF selected with derating margin) | PDF p.96 |
| **Inductor (from schematic)** | 33 uH (Figure 3.4.2 shows "33uH") | PDF p.97 Figure 3.4.2 |

**FLAG -- Inconsistency:**
- The text on p.96 says "calculated inductance of 47 uH" but Figure 3.4.2 on p.97 shows a 33 uH inductor in the schematic. These values are contradictory. The schematic value (33 uH) may be the actual selected part, while 47 uH may be a preliminary calculation. This needs clarification.
- Output capacitance: Text says 4 uF calculated, 10 uF selected. Schematic (Figure 3.4.2) shows C1=10uF, C3=10uF, C5=4.7uF on the output. This is consistent with the 10 uF practical selection plus additional filtering.

### 2.4 Complementary PWM Requirement

**What exactly does "complementary PWM" mean?** (PDF p.97)

The document states: "The MCU generates two complementary and synchronized PWM signals with appropriate dead-time to drive the high-side and low-side switches of the GaN half-bridge."

This means:
1. **Two PWM output signals** are generated from the MCU
2. They are **complementary**: when one is HIGH, the other is LOW (they are logical inverses)
3. They have **dead time inserted**: during transitions, BOTH signals are LOW briefly to prevent shoot-through (both high-side and low-side FETs conducting simultaneously, which would create a destructive short circuit)
4. The schematic (Figure 3.4.2, p.97) labels these as **"pwm-H"** (to HSin, pin 3) and **"pwm-L"** (to LSin, pin 4) on the EPC2152

**Dead time value: NOT SPECIFIED in the PDF.**
- The document says "appropriate dead-time" but does not give a numeric value.
- For GaN FETs like the EPC2152 (very fast switching), typical dead times are in the range of 5-50 ns. The EPC2152 datasheet would specify the minimum required dead time based on turn-on/turn-off times.
- Confidence: The complementary PWM requirement is verified from PDF p.97. The dead time value is NOT specified in this document.

### 2.5 SAMD21 Peripheral for Complementary PWM

From the SAMD21 datasheet (DS40001882H):

**The TCC (Timer/Counter for Control applications) peripheral is the correct choice.**

Key findings from the SAMD21 datasheet:

| Feature | Details | SAMD21 Datasheet Reference |
|---|---|---|
| **Peripheral** | TCC (Timer/Counter for Control) | Chapter 31, p.616 |
| **Dead-Time Insertion (DTI)** | Built-in hardware DTI unit | p.620, p.643-644 |
| **DTI block diagram** | Figure 31-34, p.644 | |
| **DTI timing diagram** | Figure 31-35, p.644 | |
| **DTI registers** | WEXCTRL register (offset 0x14): DTIEN[0:3] enable bits, DTLS[7:0] (low-side dead time), DTHS[7:0] (high-side dead time) | p.650 Register Summary |
| **Dead time resolution** | 8-bit counter per side (0-255 peripheral clock cycles) | p.644 |
| **DTI operation** | Splits each compare channel output into non-inverted LS (low-side) and inverted HS (high-side) outputs, with configurable OFF time (dead time) ensuring LS and HS never switch simultaneously | p.643 |
| **Output pins** | LS on WO[x], HS on WO[x + WO_NUM/2] (e.g., for TCC0: WO[0] and WO[4]) | p.642 |

**Which TCC instance supports DTI?** (SAMD21 datasheet p.34, Table 7-7):

| TCC# | Channels | WO outputs | Counter | DTI Support | Output Matrix | SWAP | Pattern Gen |
|---|---|---|---|---|---|---|---|
| TCC0 | 4 | 8 | 24-bit | **YES** | Yes | Yes | Yes |
| TCC1 | 2 | 4 | 24-bit | **NO** | - | - | Yes |
| TCC2 | 2 | 2 | 16-bit | **NO** | - | - | - |
| TCC3 | 4 | 8 | 24-bit | **YES** | Yes | Yes | Yes |

**Only TCC0 and TCC3 support Dead-Time Insertion.** TCC1 and TCC2 do NOT.

**Recommended: Use TCC0** (available on all SAMD21 variants) for generating the complementary PWM for the buck converter. TCC3 is only available on SAMD21x17D variants (SAMD21 datasheet p.34 note 8).

**Dead time calculation example:**
- At 48 MHz GCLK_TCC (no prescaler): each count = 1/48MHz = ~20.83 ns
- DTLS/DTHS = 0x03 (3 counts) gives ~62.5 ns dead time
- DTLS/DTHS = 0x0A (10 counts) gives ~208 ns dead time
- Maximum: 0xFF (255 counts) = ~5.3 us dead time
- For GaN half-bridge at 300 kHz, typical dead time of ~50-100 ns requires DTLS/DTHS values of approximately 2-5 at 48 MHz.

**PWM frequency calculation for 300 kHz:**
- Single-slope PWM: f_PWM = f_GCLK_TCC / (N * (TOP+1))
- At 48 MHz, no prescaler (N=1): TOP = (48,000,000 / 300,000) - 1 = 159
- This gives a PWM resolution of log2(160) = ~7.3 bits
- At 96 MHz (using FDPLL96M): TOP = (96,000,000 / 300,000) - 1 = 319, giving ~8.3 bits resolution

### 2.6 MPPT Algorithm

| Property | Value | Source |
|---|---|---|
| **Algorithm** | Incremental Conductance | PDF p.97 |
| **Control variable** | PWM duty cycle | PDF p.97, p.101 |
| **Measurements used** | Panel current, voltage, and their small variations (dI, dV) | PDF p.97 |

---

## 3. SOLAR ARRAYS

| Property | Value | Source |
|---|---|---|
| **Number of panels** | 4 deployable panels | PDF p.94, p.114 |
| **Cells per panel** | 7 cells in series | PDF p.114 |
| **Total cells** | 28 (7 x 4) | PDF p.114 |
| **Cell type** | DCUBED rigid solar panels, multi-junction GaAs (based on Azur Space reference) | PDF p.113 |
| **Panel arrangement** | "Flower" configuration on Z+ face | PDF p.94 |
| **Electrical configuration** | 2x2: two solar panels in parallel, two in series | PDF p.114 |
| **Blocking diodes** | Yes, integrated to prevent reverse currents | PDF p.114 |
| **Connection to EPS** | Gecko cables | PDF p.114 |
| **MPPT** | Single MPPT for the combined array (not per panel) | PDF p.96, p.114 ("MPPT controllers regulate the output voltage") |
| **Input voltage to PCDU** | 8.2V - 18.34V | PDF p.94 Table 3.4.1 |
| **Input current** | 0A - 1.8A | PDF p.94 Table 3.4.1 |
| **Max expected power** | 18.34V at 1.8A | PDF p.120 |

**Solar cell efficiency (from Azur Space reference, subject to change with DCUBED):**

| Condition | BOL Efficiency | EOL Efficiency | Source |
|---|---|---|---|
| At 25 deg C | 29.3% | 25.4% | PDF p.113 |
| At 80 deg C | 26% | 22% | PDF p.113 |

| Condition | Power Output | Source |
|---|---|---|
| BOL | 33.84 W | PDF p.113 |
| EOL | 25.41 W | PDF p.113 |

**FLAG:** The exact solar cell model from DCUBED is still under discussion (PDF p.113: "We are in discussion with DCUBED for exact values").

---

## 4. BATTERY PACK

| Property | Value | Source |
|---|---|---|
| **Battery type** | Lithium-Ion (Li-Ion), 18650 cylindrical format | PDF p.94, p.115 |
| **Number of cells** | 4 | PDF p.94 |
| **Configuration** | 2S2P (2 series, 2 parallel) | PDF p.94, p.115 |
| **Nominal voltage** | 6V - 8.4V | PDF p.94 Table 3.4.1 |
| **Capacity** | ~43 Wh / 6 Ah | PDF p.94 Table 3.4.1 |
| **Output current range** | 0A - 6.4A | PDF p.94 Table 3.4.1 |
| **Protection features** | Overvoltage, Undervoltage, Short Circuit, Thermal Protection | PDF p.95 Table 3.4.1 |
| **Charge/discharge current limits** | **NOT EXPLICITLY SPECIFIED** as numeric values | - |
| **Battery enclosure** | Aluminum 6061 T6, metallic, with EMI shielding | PDF p.115-116 |
| **Heaters** | 2 heaters (one between each cell pair), 22 ohm, ~6W typical | PDF p.95, p.116 |
| **Battery connector** | Wire-to-Board Terminal Block, 2383942-5, 5 pins, 12A per pin | PDF p.112 |

**Thermal monitoring:**

| Property | Value | Source |
|---|---|---|
| **Sensor type** | Temperature sensor | PDF p.95 Figure 3.4.1 |
| **Interface** | SPI | PDF p.95 |
| **Part number** | **NOT SPECIFIED** | - |
| **Purpose** | Monitor battery temperature, control heater activation, protect against cold temperatures during eclipse | PDF p.94 |

**FLAG:**
- The specific battery temperature sensor part number is a critical gap. The firmware engineer needs this to write the SPI driver.
- Charge/discharge current limits: The document mentions "maximum discharge rate" monitoring (PDF p.103) and "battery current exceeds maximum" (PDF p.101) but does not give specific ampere limits. The battery output current range is 0-6.4A (Table 3.4.1), which implies the max discharge rate is around 6.4A.
- The document mentions eFuses on "both ends of the battery connection" (PDF p.94).
- Confidence: Battery specs verified from PDF p.94 Table 3.4.1. Temperature sensor part number is MISSING.

---

## 5. POWER BUDGET AND RAILS

### 5.1 Power Rails

| Rail | Voltage Range | Regulators | Subsystems Powered | Source |
|---|---|---|---|---|
| **3.3V Power Bus** | 3.13V - 3.46V | L6982C33DR (PDU) | EPS MCU, OBC | PDF p.94 Table 3.4.1, p.108 |
| **5.0V Power Bus** | 4.75V - 5.25V | L6982C50DR (PDU) | OBC, Novoviz Payload, GNSS | PDF p.94 Table 3.4.1, p.108 |
| **V_Bat Bus** | 6V - 8.4V | Unregulated (direct from battery) | ADCS, SatNogs | PDF p.94 Table 3.4.1 |
| **3.3V_EPS (internal)** | 3.3V | TPS7B8733QDDARQ1 (LDO) | EPS MCU internal power | PDF p.105-106, p.108 |
| **5V_EPS (internal)** | 5V | TPS7B8750QDDARQ1 (LDO) | EPS MCU internal power | PDF p.105-106 |

**Redundancy:** Each voltage output has two independent power lines (primary + backup) for critical systems. There is also a separate 3.3V line for non-critical subsystems (PDF p.104). UHF is COTS and shares a power line with OBC (PDF p.104).

### 5.2 PDU eFuse Protection

| eFuse Option | Part Number | Features | Source |
|---|---|---|---|
| MAX17523ATE+ | MAX17523ATE+ | Current limiter, OV/UV/Reverse voltage protection | PDF p.104, p.108 |
| TPS25940ARVCR | TPS25940ARVCR | eFuse, current monitoring via IMON | PDF p.104, p.108 |

### 5.3 PC104 Pinout (Power Pins)

From PDF p.112, Figure 3.4.16:

| Header | Pin | Name | Description | Voltage |
|---|---|---|---|---|
| H2 | 29,30,31,32 | GND | Ground for all modules | 0V |
| H2 | 45,46 | V_Bat | Battery voltage bus output | 6 to 8.2V |
| H2 | 25,26 | 5V_Main | Main 5V supply output | 4.75 to 5.25V |
| H2 | 42 | V_Bat_backup | Sleep mode backup voltage supply | 6 to 8.2V |
| H2 | 27 | 3.3V_Main | Main 3.3V supply output (OBC) | 3.27 to 3.33V |
| H2 | 28 | 3.3V_Backup | Backup 3.3V supply output (OBC) | 3.27 to 3.33V |

### 5.4 Power Consumption by Mode

From PDF p.117-118, Tables 3.4.3 through 3.4.6:

**Measurement Mode (176 min):**
| Subsystem | Mode | Avg Power | Peak Power | Voltage |
|---|---|---|---|---|
| ADCS | ON | 1.52 W | 1.88 W | 8V |
| Novoviz | ON | 2.11 W | 4.00 W | 12V |
| EPS | ON | 0.20 W | 0.30 W | - |
| GNSS | ON | 0.56 W | 0.64 W | 5V |
| OBC | ON | 1.42 W | 1.68 W | 5V |
| Transceiver | ON | 2.20 W | 5.08 W | 6V |
| **Total** | | **8.01 W** | **11.58 W** | |

**Safe Mode (5 hours):**
| Total | 5.24 W avg | 9.16 W peak |

**Communication Mode (10 min, UHF only):**
| Total | 7.70 W avg | 12.50 W peak |

**System-level requirements:**
- Minimum average power supply: 10 W (PDF p.118)
- Each 3.3V and 5V bus must handle minimum peak of 2A for 2s duration (PDF p.118)
- Peak power consumption (downlink mode): 30.64 W (PDF p.95 Table 3.4.1)
- Average continuous power supply: 25 W (PDF p.95 Table 3.4.1)

### 5.5 Simulation Parameters

From PDF p.118, Table 3.4.7:

| Mode | Avg Power [W] | Peak Power [W] | Total Energy [Wh] |
|---|---|---|---|
| Measurement (176 min) | 14.34 | 18.48 | 37.15 |
| Safe mode (5 hours) | 5.64 | 7.28 | 31.09 |
| Communication (5 min) | 23.9 | 28.38 | 2.33 |
| Idle/Charging | 3.70 | 4.5 | - |

---

## 6. MCU DETAILS

| Property | Value | Source |
|---|---|---|
| **MCU** | SAMD21RT (Radiation Tolerant variant) | PDF p.96 |
| **Core** | ARM Cortex-M0+ | PDF p.96 |
| **Package** | TQFP64 | PDF p.96 |
| **TID tolerance** | > 50 krad (Si) | PDF p.96 |
| **SEL immunity** | LET > 78 MeV.cm2/mg | PDF p.96 |
| **Temperature range** | -40 deg C to +125 deg C | PDF p.96 |
| **Operating voltage** | 1.62V - 3.63V (SAMD21 datasheet p.2) | SAMD21 DS p.2 |
| **Communication with OBC** | I2C via PC104 connector | PDF p.96, p.112 |

**MCU Roles (PDF p.95-96):**
1. Read telemetry from PDU (INA226 via I2C for current/voltage)
2. Read battery pack telemetry (temperature via SPI)
3. Control heater on Battery Pack (Digital I/O pin)
4. Control PCU (via PWM signal) based on MPPT algorithm
5. Current and voltage measurements via Analog I/O pins
6. Communication with OBC via I2C

---

## 7. OPEN QUESTIONS AND FLAGS

### Critical Information Gaps

1. **Battery temperature sensor part number** -- NOT specified anywhere in the EPS section. This is needed for SPI driver development.

2. **INA226 I2C addresses** -- Not specified. Need the A0/A1 pin configuration from the schematic to determine addresses.

3. **Inductor value discrepancy** -- Text says 47 uH (p.96), schematic shows 33 uH (p.97 Figure 3.4.2). Which is correct?

4. **Dead time value** -- "Appropriate dead-time" is mentioned (p.97) but no numeric value given. Need to check EPC2152 datasheet for minimum dead time requirements.

5. **MCU pin assignments** -- No pin mapping table for the SAMD21RT is provided in this document. We do not know which TCC instance/channel drives PWM-H and PWM-L, which SERCOM handles I2C for INA226 vs OBC, which SERCOM handles SPI for the temperature sensor, or which ADC channels are used for voltage/current measurements.

6. **INA226 vs INA246 typo** -- The text on p.95 says "ina246" but Table 3.4.2 on p.108 says "INA226AIDGSR". These are different parts. Almost certainly INA226 is correct.

7. **Specific battery cell model** -- Not specified. Only says "18650 Li-ion" with ESA CubeSat engineering guidelines model.

8. **Solar panel exact model** -- Under discussion with DCUBED (p.113). Specs given are based on Azur Space reference values.

9. **Charge/discharge current limits** -- No explicit numeric values for battery charge current limit or discharge current limit, only that they are monitored against "maximums" (p.101).

### Design Observations

- The EPS uses a **single MPPT** for all panels combined (2x2 configuration), not per-panel MPPT. This is simpler but less optimal if panels receive unequal illumination.
- The **triple sensing redundancy** (INA226 + LT6108 + TPS2590 IMON) on each power path is a strong design choice for a student CubeSat, with voting algorithm for reliability (p.106).
- The **hardware cutoff circuit** is independent of firmware, which is good radiation mitigation (p.98, p.107).
- The **EPC2152 GaN half-bridge** is unusual for a student CubeSat but has strong radiation heritage from CERN testing (p.96).
- There is a **backup plan** using SOLO EPS-4 from 2NDSpace if the custom EPS cannot be delivered in time (p.120).

---

## 8. COMPONENT TABLE (from Table 3.4.2, PDF p.108)

### PDU Components
| IC | Description | Usage | Radiation Justification |
|---|---|---|---|
| L6982C33DR | Voltage Regulator | 3.3V PDU regulator | ESARAD (SEE, TID) |
| L6982C50DR | Voltage Regulator | 5V PDU regulator | ESARAD (SEE, TID) |
| MAX17523ATE+ | Current Limiter/eFuse | Current limiter, OV/UV protection per PDU line | ESARAD (SEE) |
| TPS25940ARVCR | eFuse | Protection | ESARAD (SEE) |
| INA226AIDGSR | Current & Power Monitor | Measurements | ESARAD (SEE) |

### PCU Components
| IC | Description | Usage | Radiation Justification |
|---|---|---|---|
| EPC2152 | GaN half-bridge driver | Buck converter | Tested by CERN |
| LM139DR | Quad Comparator | Overvoltage detect | Family-level similarity: LM139A EPPL |
| LT6108AIMS8 | High-side current sense amp | Overcurrent protection | ESARAD close match: LT6108IMS8 (SEE) |
| TPS7B8733QDDARQ1 | 3.3V LDO | LDO for cold start | Same family, similar use to EPPL |
| 1N4148WT | Fast switching diode | Signal steering/protection | No public radiation record |
| RB160MM-50TR | Schottky barrier diode | OR-ing | No public radiation record |
