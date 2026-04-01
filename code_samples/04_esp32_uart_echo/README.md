# Code Sample 04 — ESP32 UART Echo Test (Phase 3)

Bidirectional UART between the SAMD21G17D (Curiosity Nano DM320119) and an
ESP32 dev board. The ESP32 sends "PING" every 2 seconds, the SAMD21 echoes it
back, and both sides log what they send and receive.

This is the foundation for all subsequent test phases. The ESP32 will later
simulate the OBC, inject sensor data, and verify protocol responses.

---

## What this milestone proves

1. A second SERCOM (SERCOM0) can operate simultaneously with the debug SERCOM5
2. Interrupt-driven bidirectional UART works without blocking the CPU
3. Ring buffers for both TX and RX handle data flow without data loss
4. The SAMD21 can receive data from an external device and respond to it

---

## Files in this snapshot

| File | Description |
|---|---|
| `main.c` | Application: initializes clock, debug UART, OBC UART, runs echo loop |
| `uart_obc_sercom0_pa04_pa05.c` | OBC UART driver: interrupt-driven TX+RX on SERCOM0 |
| `uart_obc_sercom0_pa04_pa05.h` | Public API for the OBC UART driver |
| `01_uart_echo_test.ino` | ESP32 Arduino sketch: sends PING, prints responses |

---

## Hardware setup

### SAMD21 Curiosity Nano (DM320119) pin assignments

| Pin | Function | SERCOM | Mux |
|---|---|---|---|
| PA04 | OBC UART TX (data out to ESP32) | SERCOM0 PAD[0] | D (0x3) |
| PA05 | OBC UART RX (data in from ESP32) | SERCOM0 PAD[1] | D (0x3) |
| PA22 | Debug UART TX (to PC via USB/CDC) | SERCOM5 PAD[0] | D (0x3) |

### Wiring between SAMD21 and ESP32

**CRITICAL: TX connects to RX, not TX-to-TX. If you get garbled data, swap
the two data wires.**

```
    SAMD21 Curiosity Nano              ESP32 DevKit v1
    (DM320119)                         (ESP32-WROOM-32)

    PA04 (TX) ─────────────────────── GPIO16 (RX2)
    PA05 (RX) ─────────────────────── GPIO17 (TX2)
    GND ───────────────────────────── GND
```

PA04 and PA05 are on the **left edge header** of the Curiosity Nano board (looking
at the board with the USB connector at the top). They are adjacent pins near the
bottom of the left edge. Look for the silkscreen labels "PA04" and "PA05" printed
on the PCB.

The ESP32 default Serial2 pins are GPIO16 (RX) and GPIO17 (TX). If your ESP32
board is not a standard DevKit v1, change the pin definitions at the top of the
Arduino sketch.

Both boards operate at 3.3V. No level shifting is needed.

### Common wiring mistake

During development, we initially connected ESP32 TX to SAMD21 TX (and RX to RX).
Symptoms:
- ESP32 received garbled/corrupted characters
- SAMD21 received nothing (OBC RX bytes: 0)
- Both TX pins were fighting over the same wire (bus contention)

The fix: swap the two data wires so TX always connects to the other board's RX.

---

## How to reproduce

### 1. Build and flash the SAMD21

```bash
cd /path/to/EPS-second-try
make clean && make      # must produce zero warnings
make flash              # must print "Verified OK"
```

### 2. Flash the ESP32

1. Open `01_uart_echo_test.ino` in Arduino IDE
2. Select your ESP32 board and COM port
3. Upload
4. Open Serial Monitor at 115200 baud

### 3. Observe

**ESP32 Serial Monitor should show:**
```
=== ESP32 UART Echo Test ===
Connected to SAMD21 on Serial2 (115200 baud)
  RX pin (from SAMD21 TX): GPIO16
  TX pin (to SAMD21 RX):   GPIO17
Sending PING every 2 seconds...
---
[TX] PING
[RX] Received: PING
[TX] PING
[RX] Received: PING
```

**SAMD21 COM6 (PuTTY at 115200 baud) should show:**
```
=== BOOT OK ===
CPU clock Hz: 48000000
reset cause: 64
OBC UART initialized on SERCOM0 PA04/PA05
heartbeat: 1
OBC RX:
PING
heartbeat: 2
heartbeat: 3
OBC RX:
PING
```

You can also type messages in the ESP32 Serial Monitor. They will be sent to the
SAMD21, which echoes them back.

---

## How the code works

### Architecture overview

```
  ESP32                          SAMD21
  ─────                          ──────
  Serial2.println("PING")  ───► PA05 (RX wire)
                                   │
                                   ▼
                            SERCOM0 hardware captures byte
                            Sets RXC flag → interrupts CPU
                                   │
                                   ▼
                            SERCOM0_Handler reads DATA register
                            Stores byte in RX ring buffer (256 bytes)
                                   │
                                   ▼
                            main loop calls process_any_bytes_received()
                            Accumulates bytes until '\n'
                            Logs "OBC RX: PING" to debug UART
                            Echoes "PING\n" back via uart_obc_send_bytes()
                                   │
                                   ▼
                            Bytes copied to TX ring buffer
                            DRE interrupt enabled
                            SERCOM0_Handler sends bytes one at a time
                                   │
                                   ▼
                            PA04 (TX wire) ───► ESP32 GPIO16 (RX)
                                                Serial2.readStringUntil('\n')
                                                Serial.println("Received: PING")
```

### Why interrupt-driven instead of blocking or DMA

**Blocking TX** (polling the DRE flag per byte) would waste 18 ms of CPU time for a
210-byte CHIPS telemetry response. At 48 MHz, that is 864,000 wasted cycles where
no MPPT algorithm, watchdog, or sensor read can execute.

**DMA TX** would be ideal (zero CPU per byte) but the SAMD21 has a single shared
DMAC interrupt handler. The existing debug logging driver (debug_functions.c) owns
DMAC_Handler inside `#ifdef DEBUG_LOGGING_ENABLED`. Adding a second DMA channel
would require splitting that ISR into a shared module — too much structural change.

**Interrupt-driven TX** is the compromise: the CPU spends ~0.6 microseconds per byte
in SERCOM0_Handler (one interrupt per byte sent). At maximum sustained throughput
(11,520 bytes/second), the total CPU overhead is 0.7%. The main loop never blocks.

### The DATA register is two registers at one address

The SERCOM USART DATA register (offset 0x28) is NOT a single storage location.
The hardware routes reads and writes to different internal buffers:

- **Writing** to DATA puts the byte into the **transmit write buffer** (TxDATA)
- **Reading** from DATA returns a byte from the **receive buffer** (RxDATA)

These are separate circuits in the silicon. Sending a byte does not affect received
bytes, and vice versa. Full-duplex (simultaneous TX and RX) works because the
transmit shift register and receive shift register are independent hardware.

Source: SAMD21 datasheet Section 26.6.2.4, page 421:
"The USART Transmit Data register (TxDATA) and USART Receive Data register
(RxDATA) share the same I/O address, referred to as the Data register (DATA).
Writing the DATA register will update the TxDATA register. Reading the DATA
register will return the contents of the RxDATA register."

### Transmit is double-buffered (no gaps between bytes)

The transmitter has a write buffer (TxDATA) AND a shift register. When the shift
register finishes sending byte N, it immediately loads byte N+1 from the write
buffer — with zero idle time — as long as the CPU wrote the next byte before the
shift register needed it. Since the CPU responds in ~0.6 us and a byte takes
86.8 us to send, the write buffer always has the next byte ready. Total transmission
time = number_of_bytes x 86.8 us, no added overhead.

### Receiver has a two-level buffer

The receive hardware can hold 2 bytes before overflow. If a third byte arrives
before the CPU reads the first, the BUFOVF (Buffer Overflow) error flag is set.
At 115200 baud, the CPU has 86.8 us between each byte to read the buffer. Our
interrupt handler takes ~0.6 us. Even in the worst case (another interrupt delays
the handler by a few microseconds), overflow cannot occur.
