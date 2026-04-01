# CHIPS Protocol — Frame Format, Byte Stuffing, and CRC

This document explains the CHIPS (CHESS Internal Protocol over Serial) framing
layer from first principles. No prior knowledge of serial protocols, CRC, or
HDLC is assumed. After reading this, you will understand every byte on the wire.

Source: CHESS mission document PDF pages 124-128, Section 3.5.2.

---

## Why We Need a Protocol

Phase 3 gave us a raw UART connection: the ESP32 sends text like "PING\n" and
the SAMD21 echoes it back. The newline character (\n, value 0x0A) marks the end
of each message.

But for real EPS communication, we need to send **binary data** — raw sensor
readings, status bytes, configuration values. These can be any value from 0x00
to 0xFF. If we use 0x0A as a separator and a sensor reading happens to be 0x0A,
the receiver thinks the message ended in the middle.

A protocol solves three problems:
1. **Framing** — where does each message start and end?
2. **Integrity** — did the message arrive without any bits flipped?
3. **Identification** — what kind of message is this, and which conversation
   does it belong to?

---

## The Frame: What Goes On the Wire

A CHIPS frame is a sequence of bytes sent over UART. Every frame has this
structure:

```
Byte 0:    0x7E             ← "message starts here" (sync byte)
Byte 1:    sequence_number  ← which conversation is this? (0-255)
Byte 2:    [R][command_id]  ← what kind of message + is it a question or answer?
Byte 3:    payload[0]       ← first byte of actual data (optional)
...        payload[...]     ← more data bytes (0 to 256 bytes in our build)
Byte N-2:  CRC low byte     ← error check (explained below)
Byte N-1:  CRC high byte    ← error check continued
Byte N:    0x7E             ← "message ends here" (sync byte)
```

The frame is split into three parts:

### Part 1: Header (3 bytes = 24 bits)

| Bits across the header | Size | What it means |
|---|---|---|
| 0-7 | 8 bits | Sync byte — always 0x7E — marks the start of a frame |
| 8-15 | 8 bits | Sequence number (0-255) — identifies this transaction |
| 16 | 1 bit | Response flag: 0 = "I'm asking", 1 = "I'm answering" |
| 17-23 | 7 bits | Command ID: what kind of command (0-127) |

### How bit 16 and bits 17-23 fit in one byte

The table numbers bits across the entire 24-bit header (0 through 23). But
data is sent one byte at a time. Here's how they map:

```
Bits 0-7   → Byte 0 on the wire (sync byte 0x7E)
Bits 8-15  → Byte 1 on the wire (sequence number)
Bits 16-23 → Byte 2 on the wire (response flag + command ID, packed together)
```

Byte 2 contains TWO fields packed into 8 bits:

```
Byte 2, viewed as 8 individual bits:

  bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
  ┌───┬───────────────────────────────────────────┐
  │ R │           Command ID (7 bits)             │
  └───┴───────────────────────────────────────────┘
    │                    │
    │                    └── 7 bits → values 0 to 127
    └── 1 bit: 0 = request, 1 = response
```

**Concrete example — OBC sends GET_TELEMETRY (command 0x01) as a request:**
```
Byte 2 = 0  0000001  = 0x01
         │  └┴┴┴┴┴┘── command = 1 (GET_TELEMETRY)
         └── response flag = 0 ("I'm asking")
```

**EPS responds to GET_TELEMETRY:**
```
Byte 2 = 1  0000001  = 0x81
         │  └┴┴┴┴┴┘── command = 1 (still GET_TELEMETRY)
         └── response flag = 1 ("I'm answering")
```

In C, to extract these fields from a received byte:
```c
uint8_t response_flag = (byte_2 >> 7) & 1;    /* shift bit 7 down, mask it */
uint8_t command_id    = byte_2 & 0x7F;         /* keep lower 7 bits only */
```

To build this byte when transmitting:
```c
uint8_t byte_2 = (response_flag << 7) | command_id;
```

### Part 2: Payload (0 to 256 bytes)

The actual data being sent. Its meaning depends on the command ID. For example:
- GET_TELEMETRY request has 0 bytes of payload (just the question, no data)
- GET_TELEMETRY response has 220 bytes (1 status byte + 219 bytes of sensor data)
- SET_PARAMETER request has 5 bytes (1 byte parameter ID + 4 bytes value)

The CHIPS specification allows up to 1024 bytes, but our implementation limits
this to 256 bytes because no Phase 4 command needs more than 219 bytes, and
the full 1024 would cost ~768 bytes of our limited 16KB RAM.

### Part 3: Footer (3 bytes = 24 bits)

| Bits across the footer | Size | What it means |
|---|---|---|
| 0-15 | 16 bits | CRC-16/KERMIT checksum (low byte first) |
| 16-23 | 8 bits | Sync byte — always 0x7E — marks the end of a frame |

### Size limits

- Minimum frame (no payload): 3 header + 0 payload + 3 footer = **6 bytes**
- Maximum in our build (256-byte payload): 3 + 256 + 3 = **262 bytes** (before
  byte stuffing, which can increase the wire size)

---

## Byte Stuffing — Why It Exists and How It Works

### The problem

We chose 0x7E as the "start/end of message" marker. But what if the payload
data, or the CRC bytes, happen to contain the value 0x7E? The receiver would
see that 0x7E and think: "The message just ended!" — but it didn't. That byte
was just data.

### The analogy

Imagine writing a letter where the rule is "the letter ends at the first
period." But your letter says "Dr. Smith is here." The reader stops at "Dr."
thinking the letter ended.

The fix: agree on an escape rule. Write `\.` for a literal period. And if the
letter contains an actual backslash, write `\\`.

### The CHIPS escape rules

CHIPS uses the same idea with two special bytes:
- **0x7E** = the sync/delimiter byte ("message boundary")
- **0x7D** = the escape byte ("the next byte isn't what it looks like")

**When transmitting (encoding)** — for every byte between the two sync bytes:
- If the byte is 0x7E → send 0x7D followed by 0x5E instead
- If the byte is 0x7D → send 0x7D followed by 0x5D instead
- Any other byte → send it unchanged

**When receiving (decoding):**
- If you see 0x7D → don't store it. Look at the NEXT byte. XOR it with 0x20.
  That's the real data value.
  - 0x5E XOR 0x20 = 0x7E (the original was a sync byte in the data)
  - 0x5D XOR 0x20 = 0x7D (the original was an escape byte in the data)
- If you see 0x7E → this is a real frame boundary (start or end)
- Anything else → store it directly

### Why XOR with 0x20?

It's a simple reversible bit flip. Applying XOR 0x20 twice gives back the
original:
```
0x7E XOR 0x20 = 0x5E    (encode)
0x5E XOR 0x20 = 0x7E    (decode — back to original)
```

### What gets stuffed?

Byte stuffing applies to **everything between the two sync bytes**:
- Sequence number (byte 1)
- Command/response byte (byte 2)
- All payload bytes
- Both CRC bytes

The opening and closing 0x7E sync bytes are **never stuffed** — they are the
real delimiters.

### Complete worked example

Suppose we want to send: sequence=0, command=0x01, payload=[0x7E, 0x7D, 0x42].
The CRC of [0x00, 0x01, 0x7E, 0x7D, 0x42] might be, say, 0xABCD.

**Step 1 — Raw content before stuffing:**
```
[0x00] [0x01] [0x7E] [0x7D] [0x42] [0xCD] [0xAB]
  seq    cmd   pay0   pay1   pay2   CRC-lo CRC-hi
```

**Step 2 — After byte stuffing:**
```
[0x00] [0x01] [0x7D,0x5E] [0x7D,0x5D] [0x42] [0xCD] [0xAB]
  seq    cmd     0x7E!       0x7D!      pay2   CRC-lo CRC-hi
               escaped     escaped    (no stuffing needed for these)
```

**Step 3 — Add sync bytes:**
```
0x7E  0x00  0x01  0x7D 0x5E  0x7D 0x5D  0x42  0xCD  0xAB  0x7E
start  seq   cmd  ←stuffed→  ←stuffed→  pay2  CRC        end
```

The 7 raw content bytes became 9 on the wire (two bytes needed escaping).

### Back-to-back sync bytes

What if the receiver sees `0x7E 0x7E 0x7E` on the wire? Per RFC 1662, multiple
consecutive 0x7E bytes are called "inter-frame fill." They are normal and
should be silently ignored. The parser treats each 0x7E as a potential frame
boundary and resets if no data has accumulated.

---

## CRC-16/KERMIT — Error Detection from First Principles

### What is a CRC?

CRC stands for **Cyclic Redundancy Check**. It's a "fingerprint" of the data.

Before sending, the transmitter computes a 16-bit number from the data bytes
(the checksum). It appends this number to the frame. The receiver computes the
same checksum from the received data and compares it to the received checksum.
If they match, the data is almost certainly intact. If they don't match,
something was corrupted during transmission.

### Why not just add up all the bytes?

A simple sum (add all bytes together) can't detect if two bytes got swapped,
or if two errors cancel each other out (+1 in one byte, -1 in another). CRC
is mathematically much stronger.

### How CRC works conceptually

CRC treats your entire message as one giant binary number. It divides this
number by a fixed constant called the "polynomial." The **remainder** of that
division is the CRC checksum.

It's like using the remainder of regular division to verify data: if you know
the dividend, divisor, and remainder, you can verify the math. If any digit of
the dividend changed, the remainder would be different.

### CRC-16/KERMIT specifically

"16" = the checksum is 16 bits (2 bytes). "Kermit" = the name of this specific
variant (named after the Kermit file transfer protocol from the 1980s).

| Parameter | Value | What it means |
|---|---|---|
| Width | 16 bits | The checksum is 2 bytes |
| Polynomial | 0x1021 | The "divisor" — chosen for good error detection properties |
| Reflected polynomial | 0x8408 | Same polynomial, bit-reversed (used in our implementation) |
| Initial value | 0x0000 | The CRC register starts at zero |
| Input reflected | YES | We process bits LSB-first (matches UART bit order) |
| Output reflected | YES | The result is also bit-reversed |
| Final XOR | 0x0000 | No final transformation |
| Check value | 0x2189 | CRC of the ASCII string "123456789" — universal test vector |

### What the CRC covers

The CRC is computed over the **raw, unstuffed** content:
- Sequence number (1 byte)
- Command/response byte (1 byte)
- Payload (0-256 bytes)

The CRC does NOT cover the sync bytes (they are added after CRC computation).
The CRC is computed BEFORE byte stuffing is applied.

### How the CPU computes it (table-based)

We pre-compute a lookup table of 256 entries (one per possible byte value).
Each entry answers: "if I feed just this one byte through the CRC polynomial,
what's the contribution?" This table is 512 bytes (256 entries x 2 bytes each)
and is stored in flash memory (constant, never changes).

To compute the CRC of a message:

```c
uint16_t crc = 0x0000;                           /* start at zero */

for each byte in the message:
    uint8_t index = (crc XOR byte) AND 0xFF;      /* combine byte with current CRC */
    crc = (crc >> 8) XOR lookup_table[index];     /* look up the contribution */

/* crc now holds the 16-bit checksum */
```

For a 220-byte telemetry response, this loop runs 220 times. Each iteration is
~10 CPU instructions. At 48 MHz that's ~46 microseconds — negligible.

### How the CRC is placed in the frame

The 16-bit CRC is split into two bytes and appended **low byte first**:

```c
frame[position]     = (uint8_t)(crc & 0xFF);         /* low byte */
frame[position + 1] = (uint8_t)((crc >> 8) & 0xFF);  /* high byte */
```

For example, if CRC = 0x1189:
- Low byte = 0x89 (sent first)
- High byte = 0x11 (sent second)

### How the receiver verifies it

1. Extract the last 2 bytes of the unstuffed frame as the received CRC
2. Compute CRC over everything EXCEPT those last 2 bytes
3. Compare: if computed == received, the data is intact

### How good is CRC-16?

CRC-16 detects:
- All single-bit errors (1 bit flipped)
- All double-bit errors (2 bits flipped)
- All odd numbers of bit errors
- All burst errors up to 16 bits long (consecutive bits corrupted)
- 99.997% of all longer burst errors

For a UART link inside a CubeSat, this is more than sufficient.

### Boot self-test

At every power-on, the firmware computes CRC of the ASCII string "123456789"
(bytes 0x31 through 0x39) and asserts the result equals 0x2189. This is the
universally published test vector. If the lookup table has even one wrong entry,
this test catches it before any communication begins.

---

## The Full Data Flow — From Wire to Command Handler and Back

### Receiving a command (step by step)

```
Step 1: Bytes arrive on the wire (electrical signals on PA05)
        SERCOM0 hardware captures each byte into its DATA register.
        The RXC interrupt fires. SERCOM0_Handler reads the byte and
        stores it in the 256-byte RX ring buffer. (~0.6 microseconds)
        This is the EXISTING Phase 3 UART driver. We don't touch it.

Step 2: Main loop calls feed_all_available_uart_bytes_to_chips_parser()
        This reads bytes ONE AT A TIME from the RX ring buffer using
        uart_obc_read_one_byte_from_receive_buffer().
        For each byte, it calls the CHIPS parser.

Step 3: CHIPS parser processes each byte through the state machine
        It removes byte stuffing (0x7D sequences) and accumulates
        clean data into the accumulation buffer (~260 bytes).
        Returns INCOMPLETE until the closing 0x7E arrives.

Step 4: When the closing 0x7E arrives with ≥4 accumulated bytes,
        the parser extracts the CRC from the last 2 bytes,
        computes CRC over the remaining bytes, and compares.
        If match → returns FRAME_READY.
        If mismatch → returns CRC_ERROR.

Step 5: On FRAME_READY, the parsed frame (sequence number, command ID,
        response flag, payload) is passed to the command dispatcher.

Step 6: Command dispatcher identifies the command by its ID,
        calls the appropriate handler function (e.g., GET_TELEMETRY),
        which builds a response payload.

Step 7: Command dispatcher builds a response frame:
        - Same sequence number as the request
        - Same command ID as the request
        - Response flag = 1
        - Payload = status byte + response data
        Computes CRC, applies byte stuffing, adds sync bytes.

Step 8: The finished frame bytes are written to the UART TX ring buffer
        using uart_obc_send_bytes(). The function returns immediately.
        SERCOM0_Handler sends the bytes on the wire in the background.
```

### Buffer architecture

```
WIRE ──► SERCOM0 ──► RX ring buffer (256 bytes, interrupt-driven)
                          │
                     main loop reads one byte at a time
                          │
                          ▼
                     CHIPS parser accumulation buffer (260 bytes)
                     removes stuffing, checks CRC
                          │
                     on FRAME_READY
                          │
                          ▼
                     Command dispatcher
                     executes command, builds response
                          │
                          ▼
                     Cached response buffer (512 bytes)
                     stores wire-format response for idempotency
                          │
                          ▼
                     TX ring buffer (256 bytes, interrupt-driven)
                          │
                     SERCOM0 ──► WIRE
```

---

## Parser State Machine

The streaming parser processes one byte at a time. It has three states:

```
                  byte != 0x7E (noise — ignored)
                      │
             ┌────────▼───────────┐
             │ WAITING_FOR_SYNC   │◄─────── error/reset
             └────────┬───────────┘
                      │
                   0x7E (found start of frame!)
                      │
             ┌────────▼───────────┐
          ┌─►│ COLLECTING_DATA    │
          │  └────────┬───────────┘
          │     │      │      │
          │   0x7E   0x7D   other
          │     │      │      │
          │     ▼      ▼      ▼
          │  [check]  ESCAPE  store byte in buffer
          │  frame?    │
          │     │    XOR with 0x20, store result
          │     │      │
          │     │      └───► go back to COLLECTING
          │     │
          │     ├── pos==0? inter-frame fill, stay COLLECTING
          │     ├── pos<4?  too short, reset buffer, stay COLLECTING
          │     └── pos>=4? validate CRC:
          │                   CRC matches → FRAME_READY (success!)
          │                   CRC wrong   → CRC_ERROR (drop frame)
          │                 reset buffer position to 0
          └────┘
```

### Why the parser stays in COLLECTING after a frame ends

When the closing 0x7E arrives and the frame is validated, the parser resets its
buffer but transitions to COLLECTING (not WAITING_FOR_SYNC). This is because in
standard HDLC, the closing 0x7E of one frame can simultaneously be the opening
0x7E of the next frame. By staying in COLLECTING with position=0, the parser is
ready for the next frame's data immediately.

### Edge case: 0x7D followed by 0x7E

If the parser is in ESCAPE state (just received 0x7D) and the next byte is 0x7E,
the XOR produces 0x7E ^ 0x20 = 0x5E, which is stored as data. The frame
continues. This is NOT treated as a frame boundary — the 0x7D "consumed" the
0x7E. The CRC will catch any corruption.

---

## Source Files

| File | What it contains |
|---|---|
| `src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.h` | Public API: function declarations, struct types, constants, enums |
| `src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.c` | Implementation: CRC lookup table (512 bytes), CRC function, parser state machine, frame builder, boot self-tests |

This module is **PURE LOGIC** — it touches no hardware registers. All functions
take inputs and return outputs. It can be compiled and tested on a laptop without
any microcontroller.

---

## Verification and Testing

### Boot self-tests (run automatically at every power-on)

These run before any CHIPS communication begins. If any fail, the board halts
with a SATELLITE_ASSERT and the debug log shows which test failed.

| Test | Input | Expected | Result |
|---|---|---|---|
| CRC test vector | ASCII "123456789" (bytes 0x31-0x39) | CRC = 0x2189 | PASS — PuTTY shows "CRC self-test: 0x2189 PASS" |
| Frame round-trip | Build frame: seq=0x42, cmd=0x01, payload=[0xAA,0xBB]. Feed the wire bytes one at a time into the parser. | Parser returns FRAME_READY. All fields match: seq=0x42, cmd=0x01, resp=0, payload=[0xAA,0xBB] | PASS — PuTTY shows "Frame round-trip self-test PASS" |

### ESP32 integration tests (9 test cases)

See `esp32_test_harness/02_chips_protocol_test/02_chips_protocol_test.ino`

| Test | What ESP32 sends | What ESP32 checks in the response | What edge case this covers |
|---|---|---|---|
| 1 | GET_TELEMETRY (seq=0, cmd=0x01, no payload) | 220-byte payload, CRC valid, resp_flag=1, cmd=0x01, seq=0 | Basic happy path |
| 2 | SET_PARAMETER (seq=1, cmd=0x02, payload=[0x01,0x00,0x00,0x12,0x34]) | Status byte = 0x00 (SUCCESS) | Command with payload |
| 3 | GET_TELEMETRY again (seq=0 — same as test 1) | Response is byte-for-byte identical to test 1's response | Idempotency: duplicate seq re-sends cached response |
| 4 | GET_TELEMETRY with CRC deliberately corrupted (one bit flipped) | No response at all (3-second timeout) | Bad CRC: SAMD21 silently drops the frame |
| 5 | Unknown command 0x77 (seq=3) | Status byte = 0x01 (UNKNOWN_COMMAND), cmd=0x77 mirrored | Unknown command error handling |
| 6 | SET_PARAMETER with 0x7E in the value field | Status = SUCCESS, payload correctly unstuffed | Byte stuffing in payload data |
| 7 | SET_PARAMETER with only 2 bytes payload (should be 5) | Status = 0x02 (INVALID_PAYLOAD_LENGTH) | Payload length validation |
| 8 | Three extra 0x7E bytes before a valid GET_STATE frame | Normal successful response | Inter-frame fill (RFC 1662 compliance) |
| 9 | 130 seconds of complete silence, then one GET_TELEMETRY | SAMD21 still responds normally after the timeout | 120-second OBC autonomy timeout detection |

### How to re-run all tests

1. Build and flash SAMD21: `make clean && make`, then `make flash`
2. Open PuTTY on COM6 at 115200 baud — boot self-tests run automatically
3. Verify PuTTY shows "CRC self-test: 0x2189 PASS" and "Frame round-trip self-test PASS"
4. Upload `02_chips_protocol_test.ino` to ESP32 via Arduino IDE
5. Wire: ESP32 TX2 (GPIO17) → SAMD21 PA05, ESP32 RX2 (GPIO16) → SAMD21 PA04, GND → GND
6. Open ESP32 Serial Monitor at 115200 baud — tests run automatically
7. All 9 tests should print PASS
8. Test 9 takes ~2.5 minutes (130 second silence + one final command)

### Known issue: ESP32 Serial2 pin configuration

The ESP32 Arduino core does NOT guarantee which GPIO pins `Serial2` uses by
default. Different ESP32 boards and Arduino core versions assign different
default pins. Our sketch was initially written with `Serial2.begin(115200)`
(no pin arguments) and the ESP32 sent data on the wrong GPIO — the SAMD21
received zero bytes.

**The fix:** Always specify pins explicitly:
```c
Serial2.begin(115200, SERIAL_8N1, 16, 17);  // RX=GPIO16, TX=GPIO17
```

This matches the Phase 3 echo test sketch which worked. If you use a different
ESP32 board, check its documentation for the correct Serial2 pins and update
`ESP32_UART_RX_PIN` and `ESP32_UART_TX_PIN` at the top of the sketch.

### All tests verified passing (9/9)

Tested on: SAMD21G17D Curiosity Nano DM320119 + ESP32 DevKit v1
Result: **9 passed, 0 failed**
ESP32 Serial Monitor output: `ALL TESTS COMPLETE: 9 passed, 0 failed`
