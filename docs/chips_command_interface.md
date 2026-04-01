# CHIPS Command Interface — EPS Commands, Telemetry, and Protocol Rules

This document defines the command interface between the OBC and the EPS over
the CHIPS protocol. It is the contract that both sides must implement.

**Share this document with the OBC team.** It tells them exactly which commands
to send, what payload format to use, and what responses to expect.

Source: CHESS mission document PDF pages 35-36 (telemetry), 124-128 (CHIPS).
Command IDs are defined by the EPS firmware team (not in the PDF — the CHIPS
spec only defines framing, not command meanings).

---

## Communication Model — Who Talks First?

The OBC and EPS follow a strict **master/slave** pattern:

- **The OBC is always the master.** It initiates every exchange by sending a
  command. The EPS never speaks unless spoken to.
- **The EPS is always the slave.** It waits for a command, processes it, and
  sends back one response. It can never initiate communication on its own.
- **One transaction = one command + one response.** The OBC sends a command,
  the EPS sends back exactly one response. Then the OBC can send the next
  command.
- **The EPS must respond within 1 second.** If the OBC gets no response within
  1 second, it assumes the message was lost and retransmits.

Source: PDF page 125.

---

## How Responses Work — Mirroring

When the EPS sends a response, it does NOT use a different command ID. Instead,
it sends back the **same command ID and sequence number** as the request, with
the response flag set to 1.

This is how the OBC knows which command the response belongs to.

**Example:**

The OBC sends: "seq=5, cmd=0x01 (GET_TELEMETRY), response_flag=0"
The EPS responds: "seq=5, cmd=0x01, response_flag=1, payload=[status + data]"

The byte-level difference is only in byte 2:
```
Request:  byte 2 = 0x01  (bit 7 = 0, bits 6-0 = 0x01)
Response: byte 2 = 0x81  (bit 7 = 1, bits 6-0 = 0x01)
```

The only bit that changes is the response flag (bit 7 of byte 2).

---

## Command Table

All commands flow from OBC to EPS. The EPS always responds.

| CMD ID | Name | What the OBC sends as payload | What the EPS sends back as payload |
|---|---|---|---|
| 0x01 | GET_TELEMETRY | nothing (0 bytes) | status(1) + telemetry(219) = **220 bytes** |
| 0x02 | SET_PARAMETER | param_id(1) + value(4) = **5 bytes** | status(1) = **1 byte** |
| 0x03 | GET_STATE | nothing (0 bytes) | status(1) + state_id(1) + sub_state(1) = **3 bytes** |
| 0x04 | SET_MODE | mode_id(1) = **1 byte** | status(1) = **1 byte** |
| 0x05 | SHED_LOAD | load_mask(1) = **1 byte** | status(1) = **1 byte** |
| 0x06 | DEPLOY_ANTENNA | nothing (0 bytes) | status(1) = **1 byte** |

**Command IDs 0x07 through 0x7F are reserved for future use.** The 7-bit
command field allows up to 128 commands, far more than we need.

---

## Response Status Codes

Every response payload starts with a **status byte** — the first byte tells
the OBC whether the command succeeded or failed, and why.

| Status code | Name | When it is sent |
|---|---|---|
| 0x00 | SUCCESS | Command executed successfully. Response data follows. |
| 0x01 | UNKNOWN_COMMAND | The command ID is not recognized by the EPS. |
| 0x02 | INVALID_PAYLOAD_LENGTH | The payload size doesn't match what this command expects. For example, SET_PARAMETER expects exactly 5 bytes but received 2. |
| 0x03 | PARAMETER_OUT_OF_RANGE | The parameter value was rejected (e.g., a voltage threshold set to an impossible value). |
| 0x04 | COMMAND_NOT_AVAILABLE | The command is valid but not allowed in the current EPS operating mode. |

For successful commands, the status byte is followed by the response data
(if any). For errors, the status byte is the only payload byte.

---

## Telemetry Structure (219 bytes)

When the OBC sends GET_TELEMETRY (cmd 0x01), the EPS responds with a 220-byte
payload: 1 status byte (0x00 = SUCCESS) followed by 219 bytes of sensor data.

The 219 bytes are laid out as follows (from PDF pages 35-36, Table 2.2.1):

| Byte offset | Size (bytes) | Field name | What it contains |
|---|---|---|---|
| 0 | 16 | HK_EPS_VOLT | Voltage reading for each power rail. 8 rails x 2 bytes each (16-bit ADC values). |
| 16 | 16 | HK_EPS_AMP | Current reading for each power rail. 8 rails x 2 bytes each. Measured by INA226 current monitors. |
| 32 | 40 | HK_EPS_BAT_INFO | Battery state: state of charge (SoC), depth of discharge (DoD), charge cycle count, discharge cycle count. Multiple battery parameters packed into 40 bytes. |
| 72 | 8 | HK_EPS_SP_INFO | Solar panel voltage and current. 4 panels x 2 bytes each (1 byte voltage + 1 byte current per panel). |
| 80 | 1 | HK_EPS_SP_HDRM | Hold-Down Release Mechanism status. A bitmask: each bit represents one solar panel's deployment status (0=stowed, 1=deployed). |
| 81 | 4 | HK_EPS_MCU_TEMP | SAMD21 internal temperature sensor reading. 4 bytes for a 32-bit temperature value. |
| 85 | 16 | HK_EPS_BP_TEMP | Battery pack temperature. Multiple temperature sensors on the battery, 2 bytes each. |
| 101 | 8 | HK_EPS_SP_TEMP | Solar panel temperature. 4 panels x 2 bytes each. |
| 109 | 100 | HK_EPS_COMMAND_LOG | Log of the last N commands executed by the EPS. Stored as a circular buffer in RAM. |
| 209 | 10 | HK_EPS_ALERT_LOG | Active alert/fault flags. Each bit represents a specific fault condition (overcurrent, overvoltage, overtemperature, etc.). |

**Total: 219 bytes.**

**Phase 4 status:** All fields are filled with zeros (placeholder). Real sensor
data will be populated by Phase 6 sensor drivers (ADC, I2C INA226, SPI
temperature sensor). The protocol layer works correctly regardless of whether
the data is real or zeros — the framing, CRC, and byte stuffing operate on raw
bytes.

---

## Idempotency — Why It Matters and How It Works

Source: PDF page 125.

### The problem

Space communication is unreliable. The OBC sends DEPLOY_ANTENNA (seq=5) to the
EPS. The EPS executes it and sends a response. But the response gets corrupted
by noise and never arrives. The OBC waits 1 second, gets no response, and
retransmits: "seq=5, DEPLOY_ANTENNA."

If the EPS executes the command again, it fires the deployment mechanism twice.
This wastes power and could damage hardware.

### The rule

> "Commands with the same sequence number are idempotent, which means that if
> the subsystem receives duplicates due to retransmission, the command must
> still be executed exactly once. However, a response must be sent back every
> time." — PDF page 125

### How the EPS implements this

1. The EPS remembers the **last sequence number** it processed.
2. The EPS **caches the last response** in wire format (ready to re-send).
3. When a command arrives:
   - If the sequence number is **different** from the last: execute the command,
     build a response, cache it, send it.
   - If the sequence number is **the same** as the last: do NOT re-execute.
     Just re-send the cached response.

### Sequence diagram

```
OBC                                    EPS
 │                                      │
 │── seq=5, DEPLOY_ANTENNA ────────────►│ Execute! Cache response.
 │◄── seq=5, DEPLOY_ANTENNA, OK ───────│
 │                                      │
 │   [response gets lost on the wire]   │
 │                                      │
 │── seq=5, DEPLOY_ANTENNA ────────────►│ Same seq=5! Don't execute again.
 │◄── seq=5, DEPLOY_ANTENNA, OK ───────│ Re-send cached response.
 │                                      │
 │── seq=6, GET_TELEMETRY ─────────────►│ New seq=6! Execute normally.
 │◄── seq=6, GET_TELEMETRY, [data] ────│
```

---

## 120-Second Autonomy Timeout

Source: PDF page 125.

> "After 120 seconds without any message from OBC, the subsystem may assume
> autonomy."

### What this means

If the OBC crashes, loses power, or its UART fails, the EPS will stop receiving
commands. After 120 seconds of silence, the EPS assumes the OBC is gone and
enters autonomous mode.

### What the EPS does

- **Phase 4 (current):** Detects the timeout and logs "OBC TIMEOUT — 120s
  without communication. Entering autonomous mode." No action is taken beyond
  logging.
- **Phase 8 (future):** The EPS will implement actual autonomous behavior:
  continue regulating the power bus, protect the battery from over-discharge,
  keep running MPPT for solar panels, and shed non-critical loads.

### When the timer resets

The timeout counter resets whenever **any valid CHIPS frame** is received —
even if the command itself fails (unknown command, bad payload length, etc.).
The point is: the OBC is still communicating, so the EPS is not alone.

The timer is implemented using the SysTick millisecond counter:
```c
uint32_t elapsed = now_ms - last_valid_obc_message_ms;
if (elapsed >= 120000u) { /* timeout! */ }
```

---

## Complete Wire Example — GET_TELEMETRY Transaction

### Step 1: OBC builds and sends the request

Raw content (before stuffing): `[0x00, 0x01]`
- 0x00 = sequence number 0
- 0x01 = response_flag=0, command=0x01 (GET_TELEMETRY)

CRC of [0x00, 0x01] = 0x1189

Raw content with CRC appended (LSB first): `[0x00, 0x01, 0x89, 0x11]`

No byte needs stuffing (none are 0x7E or 0x7D).

Final frame on the wire:
```
0x7E  0x00  0x01  0x89  0x11  0x7E
 │     │     │     │     │     └── end sync
 │     │     │     └─────┘        CRC = 0x1189 (low byte first)
 │     │     └── resp=0, cmd=0x01
 │     └── seq=0
 └── start sync
```

Total: 6 bytes.

### Step 2: EPS receives, parses, and executes

The SAMD21 parser receives these 6 bytes, unstuffs them (no stuffing needed
here), extracts: seq=0, cmd=0x01, resp=0, payload=empty. CRC matches.

Command handler sees cmd=0x01 = GET_TELEMETRY. It builds a response with
status=SUCCESS (0x00) followed by 219 bytes of telemetry data (all zeros
in Phase 4).

### Step 3: EPS builds and sends the response

Raw content: `[0x00, 0x81, 0x00, 0x00, 0x00, ... (219 zeros) ..., CRC-lo, CRC-hi]`
- 0x00 = sequence number 0 (mirrors request)
- 0x81 = response_flag=1, command=0x01
- 0x00 = status SUCCESS
- 219 zeros = telemetry data

CRC is computed over the 222 bytes (seq + cmd + status + 219 telemetry).
CRC appended as 2 bytes (LSB first). Byte stuffing applied. Sync bytes added.

Total on wire: ~226 bytes (222 content + 2 CRC + 2 sync, minimal stuffing
because zeros and 0x81 don't need escaping).

---

## Source Files

| File | What it contains |
|---|---|
| `src/chips_protocol_dispatch_commands_and_build_responses.h` | Public API: `chips_command_dispatch_initialize()`, `chips_dispatch_received_command_and_send_response()`, telemetry struct type, command ID enum, status code enum |
| `src/chips_protocol_dispatch_commands_and_build_responses.c` | Implementation: 6 command handler functions (one per command, each is `static`), idempotency tracking, cached response buffer, chunked UART sending for large frames |
