# CHIPS Protocol & OBC-EPS Communication Interface -- Deep Research Log

**Date:** 2026-04-01
**Source:** `chess sattelite main doc-1.pdf` (Ref. CP0_SATPF_2026-03-08, Issue 1, Revision 1)
**Researcher:** Claude deep-research agent
**Status:** COMPLETE

---

## Table of Contents

1. [CHIPS Protocol Overview](#1-chips-protocol-overview)
2. [Packet Format (Byte-Level)](#2-packet-format-byte-level)
3. [Byte Stuffing / Encoding](#3-byte-stuffing--encoding)
4. [CRC-16/KERMIT Checksum](#4-crc-16kermit-checksum)
5. [Communication Model & Timing](#5-communication-model--timing)
6. [Transaction Sequence Numbers](#6-transaction-sequence-numbers)
7. [Decoder State Machine](#7-decoder-state-machine)
8. [Physical Layer: I2C vs UART](#8-physical-layer-i2c-vs-uart)
9. [EPS Telemetry Items](#9-eps-telemetry-items)
10. [Telecommands (OBC to Subsystems)](#10-telecommands-obc-to-subsystems)
11. [Telemetry Rates & Data Budget](#11-telemetry-rates--data-budget)
12. [OBC Software Architecture (F Prime)](#12-obc-software-architecture-f-prime)
13. [Satellite Operating Modes (EPS Impact)](#13-satellite-operating-modes-eps-impact)
14. [ESP32 Test Harness Implications](#14-esp32-test-harness-implications)
15. [Gaps, Missing Information & Open Questions](#15-gaps-missing-information--open-questions)
16. [Is CHIPS Custom or Based on a Standard?](#16-is-chips-custom-or-based-on-a-standard)

---

## 1. CHIPS Protocol Overview

**Confidence: VERIFIED from PDF page 124, section 3.5.2.2**

CHIPS stands for **CHESS Internal Protocol over Serial**. It is the internal
communication protocol used between the OBC and subsystems that "do not follow
an existing standard interface." Specifically, CHIPS is used by:

- **The EPS**
- **The telecom transceiver**
- **The Novoviz camera payload**

to exchange commands and data with the OBC.

> "The internal communication protocol, referred to as CHIPS (CHESS Internal
> Protocol over Serial), provides a communication method for subsystems that
> do not follow an existing standard interface."
> -- PDF page 124, section 3.5.2.2

**Key point:** CHIPS is NOT used for ADCS (which uses its own UART protocol) or
GNSS (which has its own message structure). It is specifically for subsystems
without a pre-existing command interface.

---

## 2. Packet Format (Byte-Level)

**Confidence: VERIFIED from PDF pages 126-127, Tables 3.5.2 and 3.5.3, Figures 3.5.3 and 3.5.4**

A CHIPS frame is divided into 3 sections:

1. **Header** (24 bits = 3 bytes)
2. **Payload** (variable length, 0 to 1024 bytes)
3. **Footer** (24 bits = 3 bytes)

### Header (3 bytes, 24 bits)

| Bits   | Size   | Description                      |
|--------|--------|----------------------------------|
| 0-7    | 8 bits | Sync byte (always **0x7E**)      |
| 8-15   | 8 bits | Sequence number (0-255)          |
| 16     | 1 bit  | Request (0) / Response (1) flag  |
| 17-23  | 7 bits | Command ID (0-127)               |

**Figure 3.5.3 (page 126)** shows the header bit diagram:
```
[  Sync (0x7E)  ][ Seq ][ Rsp ][ Command ]
   bits 0-7     bits 8-15  b16   bits 17-23
```

### Payload (variable, 0-1024 bytes)

> "The header is immediately followed by the payload, which is command-specific
> and can have a length of 0 to 1024 bytes."
> -- PDF page 127

**CRITICAL NOTE:** The payload contents are **subsystem-specific** and are NOT
defined in this document. The CHIPS spec only defines the framing; the actual
command data within the payload is left to each subsystem's specification.

### Footer (3 bytes, 24 bits)

| Bits   | Size    | Description               |
|--------|---------|---------------------------|
| 0-15   | 16 bits | CRC-16/KERMIT checksum    |
| 16-23  | 8 bits  | Sync byte (always **0x7E**) |

**Table 3.5.3 (page 127)** and **Figure 3.5.4 (page 127)** confirm this layout.

### Complete Frame Layout (Wire Format, Before Byte Stuffing)

```
+--------+--------+--------+--------···--------+--------+--------+--------+
| 0x7E   | SeqNum | Rsp|Cmd|   Payload bytes    | CRC-lo | CRC-hi | 0x7E   |
| (sync) | (8bit) | (8bit) |   (0-1024 bytes)   |  (16 bits CRC)  | (sync) |
+--------+--------+--------+--------···--------+--------+--------+--------+
  byte 0   byte 1  byte 2   bytes 3..N+2        byte N+3  byte N+4  byte N+5
```

**IMPORTANT:** The byte at offset 2 is packed as: bit 7 = Response flag, bits 6-0 = Command ID. This is because the header diagram shows bits 16-23 where bit 16 is the Rsp flag and bits 17-23 are the command. Since the frame is transmitted MSB first within each byte, bit 16 maps to the MSB of byte 2.

### Minimum & Maximum Frame Sizes

- **Minimum frame** (no payload): 3 (header) + 0 (payload) + 3 (footer) = **6 bytes** (before byte stuffing)
- **Maximum frame** (1024-byte payload): 3 + 1024 + 3 = **1030 bytes** (before byte stuffing)
- After byte stuffing, the on-wire size could be larger (worst case ~2x if every byte needs escaping, though this is extremely unlikely in practice).

---

## 3. Byte Stuffing / Encoding

**Confidence: VERIFIED from PDF pages 127-128, Tables 3.5.4 and 3.5.5**

The protocol uses **0x7E** as a special sync/delimiter byte. To prevent 0x7E from
appearing inside the frame data, a byte-stuffing scheme is used. This is
essentially identical to **HDLC/PPP-style framing** (RFC 1662).

### Encoding Rules (Transmitter Side)

| Original Byte    | Encoded As     |
|------------------|----------------|
| 0x7E (**sync**)  | 0x7D 0x5E      |
| 0x7D (**esc**)   | 0x7D 0x5D      |

**Table 3.5.4, page 127**

### Decoding Rules (Receiver Side)

| Received Bytes | Decoded As       |
|----------------|------------------|
| 0x7D 0x5E      | 0x7E (**sync**)  |
| 0x7D 0x5D      | 0x7D (**esc**)   |

**Table 3.5.5, page 128**

### Encoding Procedure (4 Steps)

From PDF page 127-128:

1. Initialize a frame with the sequence number, response flag, command type, and payload.
2. Compute the CRC-16/KERMIT checksum of the frame and append it.
3. Substitute any sync (0x7E) and esc (0x7D) bytes in the frame body (everything between the two delimiter sync bytes) by applying the encoding rules above.
4. Add a sync byte (0x7E) at both ends of the frame.

**CRITICAL IMPLEMENTATION DETAIL:** The byte stuffing is applied to bytes 1
through N+4 (i.e., SeqNum, Rsp|Cmd, Payload, and CRC). The leading and
trailing 0x7E sync bytes are NOT stuffed -- they are the raw delimiters.

### Relationship to HDLC/PPP

This is functionally identical to the byte-stuffing mechanism defined in RFC 1662
"PPP in HDLC-like Framing":
- 0x7E is the flag sequence (frame delimiter)
- 0x7D is the control escape octet
- Escaped bytes are XOR'd with 0x20 (0x7E XOR 0x20 = 0x5E; 0x7D XOR 0x20 = 0x5D)

---

## 4. CRC-16/KERMIT Checksum

**Confidence: VERIFIED from PDF page 127 (name), algorithm parameters from web search**

The document specifies "CRC-16/KERMIT" as the checksum algorithm (Table 3.5.3,
page 127; also mentioned in encoding step 2, page 127-128).

### CRC-16/KERMIT Parameters

| Parameter        | Value     |
|------------------|-----------|
| Width            | 16 bits   |
| Polynomial       | 0x1021    |
| Initial Value    | 0x0000    |
| Input Reflected  | **true**  |
| Output Reflected | **true**  |
| Final XOR        | 0x0000    |
| Check Value      | 0x2189    |

The "Check Value" means: CRC-16/KERMIT of the ASCII string "123456789" = 0x2189.
This is useful for verifying your implementation.

**NOTE:** CRC-16/KERMIT is also known as CRC-CCITT (LSB-first variant). It is the
same algorithm used in the Kermit file transfer protocol and Bluetooth Low Energy.
Do NOT confuse it with CRC-16/CCITT-FALSE (a.k.a. CRC-16/XMODEM or
CRC-16/AUTOSAR) which has different init/reflection settings.

### What the CRC Covers

The CRC is computed over the frame content BEFORE byte stuffing. Based on the
encoding procedure (step 2: "Compute the CRC-16/KERMIT checksum of the frame
and append it"), the CRC covers:

- Byte 1: Sequence number
- Byte 2: Response flag | Command ID
- Bytes 3..N+2: Payload

The leading sync byte (0x7E) is NOT included in the CRC (it is added in step 4,
after CRC computation). The CRC itself is then appended, and THEN byte stuffing
is applied to everything between the delimiters.

**GAP:** The document does not explicitly state the byte order (endianness) of the
CRC in the footer. Standard CRC-16/KERMIT convention is LSB-first (little-endian),
which aligns with the footer diagram showing bits 0-15 for CRC. This needs
verification with the OBC team.

---

## 5. Communication Model & Timing

**Confidence: VERIFIED from PDF page 125, section "Communication model"**

### Master/Slave (Poll/Response) Architecture

- **The OBC is always the master.** Transactions are ALWAYS initiated by the OBC.
- **The subsystem (EPS) is always the slave.** It can NEVER initiate a transaction.
- A transaction = one command from OBC + one response from subsystem.
- Commands and responses are differentiated by the **Response flag** in the header (bit 16): 0 = Request, 1 = Response.

> "A transaction is always initiated by the OBC and consists of a command from
> the OBC followed by a response from the subsystem."
> -- PDF page 125

> "Since the subsystem can not initiate transactions, commands that can not
> immediately be completed must be polled for completion by the OBC through a
> separate command."
> -- PDF page 125

### Timing Requirements

| Parameter                | Value        | Source        |
|--------------------------|--------------|---------------|
| Response timeout         | **1 second** | PDF page 125  |
| Retransmission interval  | **1 second** | PDF page 125  |
| Autonomy timeout         | **120 seconds** | PDF page 125 |

**Response timeout:** The subsystem MUST send a response to any received command
within 1 second.

**Retransmission:** The OBC retransmits every second while no response has been
received. (Implied: the OBC considers a command failed if no response within 1s
and retransmits the same command with the same sequence number.)

**Autonomy timeout:** After 120 seconds without ANY message from the OBC, the
subsystem may assume autonomy.

> "After 120 seconds without any message from OBC, the subsystem may assume
> autonomy."
> -- PDF page 125

**EPS IMPLICATION:** This 120-second timeout is critical for EPS firmware. If the EPS
does not hear from the OBC for 120 seconds, it should enter an autonomous
safe mode. This is likely the trigger for the EPS to operate independently
(e.g., continue battery protection, maintain power bus regulation, etc.).

### Idempotency

> "Commands with the same sequence number are idempotent, which means that if
> the subsystem receives duplicates due to retransmission, the command must
> still be executed exactly once. However, a response must be sent back every
> time."
> -- PDF page 125

This means the EPS firmware must:
1. Track the last received sequence number.
2. If a duplicate command arrives (same seq number), do NOT re-execute the command.
3. But DO re-send the response.

---

## 6. Transaction Sequence Numbers

**Confidence: VERIFIED from PDF page 125, section "Transactions"**

- Each new command uses a fresh sequence number: `seq = (seq + 1) mod 256`
- The sequence number is an **8-bit wrap-around** counter (0-255).
- There may be **at most one in-flight transaction** at a time (no reordering required).
- **Responses must mirror** the sequence number AND command type of their corresponding command.

### Sequence Diagram (from Figure 3.5.2, page 126)

The document provides a sequence diagram showing two transactions including
retransmission of lost messages:

```
    FS (OBC)                        Subsystem (EPS)
       |                                  |
       |--- req, seq=0, cmd=status ------>|
       |<-- resp, seq=0, cmd=status ------|
       |                                  |
       |--- req, seq=1, cmd=deploy ----X  |  (lost)
       |         [1 second]               |
       |--- req, seq=1, cmd=deploy ------>|
       |                    [Execute deploy command]
       |<-- resp, seq=1, cmd=deploy ------|
       |         [1 second]               |
       |<-- X (response lost)             |
       |--- req, seq=1, cmd=deploy ------>|
       |          [Do not re-execute, simply reply]
       |<-- resp, seq=1, cmd=deploy ------|
       |                                  |
```

---

## 7. Decoder State Machine

**Confidence: VERIFIED from PDF page 128, Figure 3.5.5**

The document provides a **reference state diagram** for a decoder that handles
resynchronization, unescaping, and CRC checks. The states (from Figure 3.5.5):

```
                  +------+
                  | Idle |<---------+
                  +------+          |
                     |              |
                  0x7E (sync)       |
                     v              |
                  +-------+         |
            +---->| Start |         |
            |     +-------+         |
            |        |              |
            |     (any byte)        |
            |        v              |
     +------+   +---------+    timeout or
     |Escaped|<--|Collect  |--- len > MAX ---> [Error] --> drop frame --> [Idle]
     +------+   +---------+                                                ^
     |  0x5E    |     |                                                    |
     |  =0x7E   |   0x7E                                                   |
     |  0x5D    |     v                                                    |
     |  =0x7D   | +----------+                                             |
     +--------->| | CheckCRC |                                             |
                  +----------+                                             |
                   /        \                                              |
                 good       bad                                            |
                  |           \                                            |
                  v            +-->  drop frame  --------------------------+
               +------+
               | Emit | --> deliver frame
               +------+
```

**States:**
1. **Idle** -- waiting for a sync byte (0x7E)
2. **Sync/Start** -- received 0x7E, waiting for frame content
3. **Collect** -- accumulating frame bytes, unescaping as needed
4. **Escaped** -- received 0x7D, next byte determines the unescaped value
5. **CheckCRC** -- received closing 0x7E, now verify CRC
6. **Emit** -- CRC good, deliver frame to application
7. **Error** -- timeout or length exceeded MAX, drop frame

**Error conditions leading to frame drop:**
- Timeout during frame collection
- Frame length exceeds maximum (>1024 bytes payload, so >1030 raw)
- CRC check fails

---

## 8. Physical Layer: I2C vs UART

### What the Document Says

**Confidence: VERIFIED from PDF pages 121, 124, 130, 132**

The document shows MIXED information about the EPS physical interface:

**Figure 3.5.1 (page 124)** -- The functional OBDH architecture block diagram shows
the EPS connected to the OBC via **I2C** (solid green line) with a **dotted** I2C line
as redundant. The diagram labels clearly show "I2C" for the EPS connection.

**Page 121, Section 3.4.9.4** -- The SOLO EPS-4 (the Gomspace COTS EPS, which is
the BACKUP EPS option) "supports I2C and RS485, in addition to a UART debug
port. The in-house model uses I2C as its primary internal bus."

**Page 130, Figure 3.5.7 (PC104 Pinout)** -- The pinout table shows:
- Pin H1-41: `I2C-SDA` -- "I2C serial data to EPS (Gomspace)" -- 3.3V
- Pin H1-43: `I2C-SCL` -- "I2C serial clock to EPS (Gomspace)" -- 3.3V

This confirms the document describes the EPS interface as **I2C** going to the
**Gomspace** (COTS backup) EPS.

**Page 132, Section 3.5.2.3** -- "I2C is also chosen for the low data-rate lines or
backup lines to some COTS systems like the Gnomespace EPS and the SatNogs
transceiver."

### Our Actual Implementation: UART

**Per the user's instruction:** The actual in-house EPS uses **UART**, not I2C. The
CHIPS protocol runs on top of UART instead. Since CHIPS is a serial framing
protocol (sync byte delimited, byte-stuffed), it is transport-layer agnostic and
works identically over UART.

### UART Parameters

**GAP -- NOT SPECIFIED IN DOCUMENT.** The document does not specify UART baud
rate, data bits, parity, or stop bits for the EPS-OBC link. The PC104 pinout
(Figure 3.5.7, page 130) does not show a dedicated UART line for the in-house EPS.

**What we know from the PC104 pinout for other UART connections:**
- UART_A_TO_BUS (H2, pin 13): Primary UART output line, 0 to 3.3V
- UART_A_FROM_BUS (H2, pin 15): Primary UART input line, 0 to 3.3V
- UART_B_TO_BUS (H1, pin 24): Secondary, 0 to 3.3V
- UART_2_RX (H1, pin 18): System UART RX, 0 to 5V
- UART_2_TX (H2, pin 22): System UART TX, 0 to 5V

**OPEN QUESTION:** Which specific UART pins on the PC104 will the in-house EPS
use? The voltage level (3.3V vs 5V) must also be confirmed. Baud rate is TBD --
common CubeSat UART rates are 9600, 115200, or 921600 baud. This must be
agreed upon with the OBC team.

### I2C Address (for Gomspace backup EPS)

The document does not explicitly state the I2C address for the Gomspace EPS.
The Gomspace NanoPower P31u/P60 family typically uses I2C address **0x02** by
default, but this should be verified against the specific SOLO EPS-4 datasheet.

---

## 9. EPS Telemetry Items

**Confidence: VERIFIED from PDF pages 35-36, Section 2.2.1, Table 2.2.1**

The following telemetry items are defined with the `HK_EPS_` prefix:

| Reference           | Size (bytes) | Description                                                       |
|---------------------|-------------|-------------------------------------------------------------------|
| HK_EPS_VOLT         | 16          | Voltage for each power line                                        |
| HK_EPS_AMP          | 16          | Current for each power line                                        |
| HK_EPS_BAT_INFO     | 40          | Battery info (State of charge, depth of discharge, charge and discharge cycles) |
| HK_EPS_SP_INFO      | 8           | Voltage and current generated by each Solar Panel                  |
| HK_EPS_SP_HDRM      | 1           | Status of the HDRM deployment for each panel                       |
| HK_EPS_MCU_TEMP     | 4           | MCU temperature                                                    |
| HK_EPS_BP_TEMP      | 16          | Battery pack temperature                                           |
| HK_EPS_SP_TEMP      | 8           | Solar panels temperature                                           |
| HK_EPS_COMMAND_LOG   | 100         | Logs the commands from EPS that have been executed                 |
| HK_EPS_ALERT_LOG     | 10          | Logs alerts generated by internal components (e.g., current sensor)|

**Total EPS telemetry size: 219 bytes** (sum of all HK_EPS_* fields: 16+16+40+8+1+4+16+8+100+10 = 219)

Note: Table 3.7.2 on page 153 says EPS telemetry is **210 bytes** per 5-minute
cycle. The slight discrepancy (219 vs 210) may indicate that not all fields are
sent every cycle, or the table uses rounded/approximate values.

### Telemetry Item Analysis for Firmware Implementation

**HK_EPS_VOLT (16 bytes):** With 16 bytes for "each power line," this likely means
8 power lines x 2 bytes each (16-bit ADC readings), OR it could be a different
breakdown. The EPS has multiple voltage buses (VBUS, 5V, 3.3V, battery, solar
panels). This needs clarification from the EPS hardware design.

**HK_EPS_AMP (16 bytes):** Same structure as VOLT -- 8 current channels x 2 bytes,
measured by the INA226AIDGSR current/power monitors.

**HK_EPS_BAT_INFO (40 bytes):** This is the largest single item. The description
mentions SoC, DoD, charge cycles, discharge cycles. 40 bytes is substantial --
likely includes multiple battery parameters and possibly historical data.

**HK_EPS_SP_INFO (8 bytes):** With 4 solar panels, this is likely 2 bytes per panel
(1 byte V + 1 byte I, or 2x 4-panel readings).

**HK_EPS_SP_HDRM (1 byte):** Single byte for Hold Down and Release Mechanism
status. Likely a bitmask with one bit per panel.

**HK_EPS_COMMAND_LOG (100 bytes):** Large log of executed commands. This is
NOT a real-time measurement but a historical log.

**HK_EPS_ALERT_LOG (10 bytes):** Alerts from current sensors / protection events.

---

## 10. Telecommands (OBC to Subsystems)

**Confidence: VERIFIED from PDF page 37, Section 2.2.2**

The document provides a **generic** telecommand table (not EPS-specific):

| Command                          | Description                                                |
|----------------------------------|------------------------------------------------------------|
| REBOOT [SUBSYSTEM]               | Reboots a referenced subsystem                              |
| SET [SUBSYSTEM] [PROPERTY]       | Updates a register parsed as argument                       |
| READ [SUBSYSTEM] [PROPERTY]      | Asks for the downlink of a register value                   |
| PROGRAM [SUBSYSTEM]              | Re-programs the software of a subsystem following a binary file passed as argument |
| SET MODE                         | Manually switches the S/C to parsed mode                    |

**CRITICAL GAP:** These are high-level ground-to-OBC telecommands, NOT the
actual CHIPS command IDs sent from OBC to EPS. The CHIPS command IDs (the
7-bit values in the header) are **NOT defined in this document**.

> "Commands and their associated payload are subsystem specific, and are not
> defined as part of this specification."
> -- PDF page 125

This means the actual EPS-specific CHIPS commands (e.g., "read voltage,"
"set power mode," "deploy HDRM," etc.) must be defined by us as part of
the EPS firmware specification. The CHIPS protocol only defines the framing.

### What We Can Infer About EPS Commands

Based on the telemetry items and the generic telecommand structure, the EPS
likely needs to support at minimum:

1. **GET_TELEMETRY** (or multiple: GET_VOLTAGES, GET_CURRENTS, GET_BAT_INFO, etc.)
2. **SET_POWER_MODE** -- switch EPS operating mode
3. **DEPLOY_HDRM** -- fire hold-down release mechanisms
4. **REBOOT** -- reset the EPS MCU
5. **GET_STATUS** -- general health/status query
6. **SET_THRESHOLD** -- update safety thresholds (per Table 2.1.1, page 34, EPS parameters like safety thresholds can be adapted via OBC communication)
7. **GET_ALERTS** -- retrieve alert log

The 7-bit command field allows for up to **128 unique commands** (0-127), which is
more than sufficient.

---

## 11. Telemetry Rates & Data Budget

**Confidence: VERIFIED from PDF pages 153-154, Section 3.7.4.3**

### Sampling Rate

> "the current best estimation that telemetry data is generated at a rate of
> about 2.5 kB every 5 minutes"
> -- PDF page 154

| Parameter         | Value                    | Source        |
|-------------------|--------------------------|---------------|
| EPS telemetry size | 210 bytes per sample    | Table 3.7.2, p153 |
| Sample interval   | Every 5 minutes          | Table 3.7.2, p153 |
| Total all subsystems | 2315 bytes per 5 min  | Table 3.7.2, p153 |
| Max packet size   | 512 bytes                | p154          |

### Telemetry Per Subsystem (Table 3.7.2, page 153)

| Subsystem    | Size (bytes) |
|--------------|-------------|
| ADCS         | 58          |
| **EPS**      | **210**     |
| OBSW         | 5           |
| OBC          | 1256        |
| Telecom      | 650         |
| Novoviz SPAD | 8           |
| GNSS         | 128         |
| **Total**    | **2315**    |

### Data Accumulation

- 2.5 kB every 5 min = ~30 kB/hour
- 512-byte max telemetry packet -> ~10 packets per 5-minute cycle
- ~120 packets per hour
- 8-hour communication gap -> 960 packets, ~480 KB
- 24-hour outage -> 2,880 packets, ~1.4 MB
- 1-week outage -> 20,160 packets, ~10 MB (telemetry queue capacity)

### OBSW-Level Packet Format (NOT CHIPS)

**Confidence: VERIFIED from PDF page 153, Figure 3.7.7**

At the OBSW level (F Prime), packets have a different structure for storage/downlink:

| Field        | Size      |
|--------------|-----------|
| Start Word   | 16 bits (always 0x2CCF) |
| Data Length   | 12 bits   |
| Unused       | 4 bits    |
| Body         | Up to 4096 bytes |
| Checksum     | 256 bits  |

**IMPORTANT:** This is the F Prime packet encapsulation for downlink, NOT the
CHIPS framing used on the OBC-EPS UART link. Do not confuse the two.

---

## 12. OBC Software Architecture (F Prime)

**Confidence: VERIFIED from PDF pages 136-142**

### Software Stack

The OBC runs a 3-layer architecture:

1. **Linux (Yocto)** -- Custom embedded Linux, managed by Systemd
2. **Phoenix** -- Flight software monitor daemon (1.4 KB), manages restarts and failsafe
3. **Pathfinder0FS** -- The actual flight software, written in C++ using F Prime (F')

### F Prime (F') Framework

F Prime is an open-source software framework from NASA JPL. Key concepts:
- Software is decomposed into **components** with typed **ports**
- Components communicate via port invocations (synchronous or async)
- The component graph is called a **topology**
- Follows the **application manager driver pattern**: Application layer -> Manager layer -> Driver layer

### Subsystem Manager Pattern (page 141-142)

The OBC uses dedicated F' components for each subsystem:

```
[EventActionSM] <---> [Subsystem Manager] <---> [Hardware Interface Driver]
                      (e.g., EPSManager)        (e.g., UART/I2C driver)
```

> "Each manager level component has its own Finite State Machine to manage
> the subsystem's operational mode."
> -- PDF page 141

> "Each peripheral device will have a dedicated manager component. At the
> lowest level is the driver layer, consisting of components dedicated to a
> particular hardware interface (e.g. UART or I2C)."
> -- PDF page 141

### Communication Flow

> "UART-based subsystem communication is implemented via CHIPS, which provides
> a master-initiated, transaction-based communication scheme with sequence
> control, CRC-protected framing and deterministic retransmission."
> -- PDF page 142

**The OBC-side communication with the EPS would flow:**

1. **EPSManager** (F' component) decides it needs telemetry or needs to send a command
2. EPSManager calls the **CHIPS Driver** component to build a CHIPS frame
3. CHIPS Driver encodes the frame (header + payload + CRC + byte stuffing + sync)
4. The **UART Driver** (Linux serial port driver) transmits the bytes
5. EPS receives, decodes, processes, and sends a CHIPS response
6. UART Driver receives the response bytes
7. CHIPS Driver decodes the frame (sync detection, unescaping, CRC check)
8. EPSManager receives the decoded response and updates telemetry/state

### Key Architecture Diagrams

**Figure 3.7.3 (page 140)** shows the high-level FS architecture:
```
[Deframer] --> [Command Dispatcher] --> [Subsystem Manager] --> [HW Interface Driver]
                                        [Subsystem Manager] --> [HW Interface Driver]
                                        [...]
[Communication Adapter]
[Framer] --> [Communication Queue] --> [Event Logger]
                                       [Telemetry Channels]
```

**NOTE:** The "Deframer" and "Framer" in this diagram are for the UHF uplink/downlink
communication, NOT for the CHIPS protocol. The CHIPS framing/deframing would be
handled within the subsystem manager or its dedicated driver component.

---

## 13. Satellite Operating Modes (EPS Impact)

**Confidence: VERIFIED from PDF pages 146-148**

The satellite has the following global operating modes managed by EventActionSM:

| Mode            | Description                              | EPS Implications                    |
|-----------------|------------------------------------------|-------------------------------------|
| **LEOP**        | Launch & Early Operations                | Deploy solar arrays, initial power  |
| **CHARGE**      | Charging secondary batteries             | Maximum solar harvesting            |
| **MEASURE**     | Payload data collection                  | Higher power consumption            |
| **COM**         | Communication with ground                | Peak power for UHF transmitter      |
| **SAFE**        | Stabilize until ground intervention      | Minimum power, protect battery      |
| SAFE_BASE       | Default SAFE sub-state                   |                                     |
| SAFE_COM        | SAFE + prioritize ground comms           |                                     |
| SAFE_CHARGE     | SAFE + prioritize charging               |                                     |
| SAFE_DETUMBLE   | SAFE + prioritize detumbling             |                                     |
| SAFE_REBOOT     | SAFE + reboot subsystems periodically    |                                     |

> "EventAction has been designed to never take control of, or individually
> command, a sub-system, instead it only broadcasts the global operating mode
> of the satellite and lets the individual sub-system managers align their
> internal state machines with this mode."
> -- PDF page 147

**EPS IMPLICATION:** The EPS must be able to receive the global mode from the OBC
(via a CHIPS command) and adjust its behavior accordingly. For example:
- In CHARGE mode: maximize MPPT efficiency, limit non-essential loads
- In COM mode: ensure sufficient power headroom for transmitter
- In SAFE: minimum operations, protect battery, maintain bus regulation
- In LEOP: handle HDRM deployment commands

The EPS firmware should also have its own internal state machine that maps
satellite modes to EPS-specific behaviors.

### Adaptability Table (page 34)

> "The EPS firmware isn't updatable since there is no bootloader implemented in
> the MCU that would allow for such and adding one is too complex and might be
> unreliable. The EPS is still in communication with the OBC, allowing us to
> adapt some predefined parameters like safety thresholds for example."
> -- Table 2.1.1, page 34

**CRITICAL:** EPS firmware cannot be updated in flight. The CHIPS interface must
support commands to adjust runtime parameters (thresholds, modes) but the
firmware itself is fixed at launch.

---

## 14. ESP32 Test Harness Implications

Based on all findings above, an ESP32 simulating the OBC for EPS testing would
need to:

### UART Configuration (TBD -- Needs Agreement with OBC Team)

- Baud rate: TBD (suggest starting with 115200 as a common default)
- Data bits: 8
- Parity: None (assumed, typical for CubeSat UART)
- Stop bits: 1
- Voltage: 3.3V logic levels (matching SAMD21 / OBC levels)

### CHIPS Frame Construction (ESP32 as OBC Simulator)

The ESP32 must implement:

1. **Frame encoder:**
   - Build header: 0x7E | seq_num | (0 << 7) | cmd_id
   - Append payload (command-specific data)
   - Compute CRC-16/KERMIT over [seq_num, cmd_byte, payload]
   - Append CRC (2 bytes, likely little-endian)
   - Byte-stuff the middle section (replace 0x7E->0x7D,0x5E; 0x7D->0x7D,0x5D)
   - Prepend and append 0x7E sync bytes

2. **Frame decoder:**
   - State machine: Idle -> Sync -> Collect (with Escaped sub-state) -> CheckCRC -> Emit
   - Un-escape received bytes
   - Verify CRC-16/KERMIT
   - Extract: seq_num, response_flag (should be 1), cmd_id, payload

3. **Transaction manager:**
   - Maintain sequence counter (0-255, wrapping)
   - Send command, wait up to 1 second for response
   - Retransmit on timeout
   - Verify response mirrors seq_num and cmd_id

### Minimal Test Commands the ESP32 Should Send

| Command (suggested) | Purpose                          | Expected Response                 |
|---------------------|----------------------------------|-----------------------------------|
| GET_STATUS (0x00?)  | Basic health check               | Status byte(s) from EPS          |
| GET_VOLTAGES (0x01?)| Read all voltage channels        | 16 bytes of ADC readings         |
| GET_CURRENTS (0x02?)| Read all current channels        | 16 bytes of ADC readings         |
| GET_BAT_INFO (0x03?)| Read battery status              | 40 bytes of battery data         |
| GET_SP_INFO (0x04?) | Read solar panel V/I             | 8 bytes of solar data            |
| GET_TEMPS (0x05?)   | Read all temperatures            | 28 bytes (MCU + battery + solar) |
| SET_MODE (0x10?)    | Change EPS operating mode        | ACK/status byte                  |
| DEPLOY_HDRM (0x20?) | Fire HDRM mechanism              | ACK/status byte                  |
| GET_ALERTS (0x30?)  | Read alert log                   | 10 bytes of alert data           |

**NOTE:** The command IDs above are suggestions only. The actual command IDs must
be defined as part of the EPS firmware design. The 7-bit field allows 0-127.

### ESP32 Validation Checklist

- [ ] Can construct valid CHIPS frames with correct CRC
- [ ] Can parse CHIPS response frames and verify CRC
- [ ] Byte stuffing works correctly (test with payloads containing 0x7E and 0x7D)
- [ ] Sequence number increments correctly
- [ ] Retransmission fires after 1-second timeout
- [ ] Idempotency: duplicate commands produce same response, single execution
- [ ] 120-second autonomy timeout: verify EPS enters autonomous mode
- [ ] Frame length limits: test with 0-byte and 1024-byte payloads
- [ ] Error handling: send corrupted frames, verify EPS drops them gracefully

---

## 15. Gaps, Missing Information & Open Questions

### CRITICAL GAPS (Must be resolved before implementation)

| # | Gap Description | Impact | Where to Find |
|---|----------------|--------|---------------|
| 1 | **EPS-specific CHIPS command IDs are NOT defined** | Cannot implement command parser | Must be defined by EPS/OBC team jointly |
| 2 | **UART baud rate not specified** | Cannot configure serial port | Agree with OBC team |
| 3 | **UART pin assignment on PC104 for in-house EPS** | Cannot wire hardware | OBC team / hardware design |
| 4 | **CRC byte order (endianness) in footer** | May cause CRC mismatches | Verify with OBC reference implementation |
| 5 | **Payload format for each command** | Cannot serialize/deserialize telemetry | Must be co-defined with OBC team |
| 6 | **No EPSManager section in the OBSW documentation** | Don't know exactly how OBC will poll us | OBC OBSW team; the doc describes ADCSManager, OBCSensorsManager, GNSSManager but NOT EPSManager |

### MINOR GAPS

| # | Gap Description | Impact | Notes |
|---|----------------|--------|-------|
| 7 | Maximum payload is stated as 1024 bytes, but typical telemetry is ~210 bytes | Low | Design for worst case |
| 8 | No explicit mention of flow control (hardware or software) | Low | CHIPS has its own framing, flow control likely not needed |
| 9 | Telemetry priority levels not defined for EPS items | Low | All EPS telemetry likely same priority |
| 10 | The 120s autonomy timeout behavior is not specified | Medium | What exactly should EPS do? Continue nominal? Enter safe? |
| 11 | Watchdog timeout values for OBC are "yet to be settled" (p135) | Medium | May affect communication timing assumptions |
| 12 | No error response format defined | Medium | What does EPS send if it receives an unknown command? |

### OPEN QUESTIONS FOR OBC TEAM

1. What UART baud rate will the OBC use for the EPS link?
2. Which PC104 pins are allocated for the in-house EPS UART?
3. Is the CRC in the footer little-endian or big-endian?
4. Will there be an EPSManager F' component? What commands will it send?
5. How frequently will the OBC poll the EPS for telemetry? (Every 5 minutes per Table 3.7.2, or more frequently?)
6. What should the EPS do during the 120-second autonomy timeout? Continue last mode? Enter safe mode?
7. Is there a NACK/error response format in CHIPS? (The ADCS uses ACK/NACK per p143, but CHIPS doesn't define this)
8. Will the OBC send a global mode change command to the EPS, or is the mode change implicit?

---

## 16. Is CHIPS Custom or Based on a Standard?

**Confidence: HIGH (analysis-based)**

CHIPS is a **custom protocol** developed by the CHESS/EST team, but it is heavily
based on well-established standards:

| Feature              | Standard Origin                        |
|----------------------|----------------------------------------|
| Sync byte 0x7E       | HDLC (ISO 13239) / PPP (RFC 1662)    |
| Escape byte 0x7D     | HDLC / PPP (RFC 1662)                |
| Byte stuffing XOR 0x20| PPP (RFC 1662)                       |
| CRC-16/KERMIT        | ITU-T V.41, Kermit protocol           |
| Master/slave polling  | Standard embedded pattern             |
| Sequence numbers      | Common in reliable serial protocols   |
| Idempotent commands   | Standard distributed systems pattern  |

The framing mechanism is essentially **PPP in HDLC-like framing** (RFC 1662) with
a simplified, fixed-size header and footer. The byte-stuffing rules are IDENTICAL
to PPP/HDLC. The only non-standard aspects are:

1. The specific header format (sync + seq + rsp|cmd packed into 3 bytes)
2. The footer format (CRC16 + sync packed into 3 bytes)
3. No address or control fields (unlike full HDLC)
4. No protocol field (unlike PPP)

**Web search confirmed:** No external documentation exists for "CHIPS" as a
communication protocol. The name appears to be entirely internal to the CHESS
project. Searching for "CHIPS protocol CubeSat" only returns results about the
CubeSat High Impulse Propulsion System (a thruster), which is unrelated.

---

## Summary for EPS Firmware Engineer

**What you need to implement on the EPS (SAMD21) side:**

1. **UART receiver** -- interrupt-driven byte reception
2. **CHIPS frame decoder** -- state machine per Figure 3.5.5
3. **CRC-16/KERMIT** -- compute and verify (poly=0x1021, init=0x0000, reflected)
4. **Byte un-stuffing** -- in the decoder state machine
5. **Command dispatcher** -- parse command ID from header, route to handler
6. **Response builder** -- mirror seq_num and cmd_id, set response flag, add payload
7. **CHIPS frame encoder** -- CRC, byte stuffing, sync delimiters
8. **UART transmitter** -- send response frame
9. **Sequence number tracker** -- for idempotency (cache last seq + response)
10. **120-second watchdog** -- trigger autonomy if no OBC communication
11. **1-second response deadline** -- ensure all responses are sent within 1s

**What you do NOT need to implement:**
- Transaction initiation (the OBC always initiates)
- Retransmission logic (the OBC handles retransmission)
- Multi-transaction queuing (at most one in-flight transaction)
- Payload format definition (this is yours to define, jointly with OBC team)
