# DMA UART Logging System

This document covers the design, implementation, and operational characteristics
of the non-blocking debug logging system. It is the second thing built after
the smoke test, and it is used for all subsequent development phases.

---

## The Problem With Blocking UART

A straightforward blocking UART transmit stalls the CPU until every bit has
been sent. At 115200 baud, each byte takes 86.8 microseconds. A 20-character
log message takes 1.7 milliseconds of dead CPU time.

If the MPPT control loop runs every 10 milliseconds, one log line per iteration
steals 17% of CPU time. More critically, it changes the timing behaviour of the
code you are trying to observe — the classic Heisenbug problem.

Source: This problem is well documented. A relevant analysis:
        https://www.beningo.com/rtedbg-open-source-data-logging-and-tracing-for-embedded-systems/
        "A single printf call can take thousands of cycles. In a time-critical
        control loop, that overhead changes the behavior you are trying to observe."

---

## The Solution: DMA + Circular Buffer

The CPU writes log bytes into a RAM buffer (takes ~50 nanoseconds per message).
DMA hardware drains the buffer to SERCOM5 in the background (takes ~1.7ms,
but the CPU is not involved). The CPU cost of one log call is approximately
1 microsecond regardless of message length.

This approach is standard practice in embedded systems. Microchip's own official
MPLAB Harmony v3 training documentation for the SAMD21 uses exactly this pattern
(DMA channel → SERCOM USART TX register, one byte per trigger).

Source: Microchip official SAMD21 DMA tutorial:
        https://developerhelp.microchip.com/xwiki/bin/view/software-tools/harmony/archive/samd21-getting-started-training-module/step2/
Source: SAMD21 DMA mechanics in detail:
        https://aykevl.nl/2019/09/samd21-dma

---

## Board-Specific Details — DM320119

The virtual COM port on the Curiosity Nano DM320119 is wired to:
- **SERCOM5**, **PB22** (TX), **PB23** (RX)
- PB22 uses mux function **D** (SERCOM-ALT) to reach SERCOM5 PAD[2]

This is confirmed in the official DM320119 hardware schematic and from community
verification. An earlier version of Microchip's documentation incorrectly listed
PB02/PB03 — this has been identified as an error.

Source: element14 community roadtest review of DM320119 (confirmed PB22/PB22):
        https://community.element14.com/products/roadtest/rv/roadtest_reviews/510/sam_d21_curiosity_na
Source: Microchip MPLAB Harmony migration guide confirming SERCOM5 for virtual COM:
        https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/32bit-mcu/sam/samd21-mcu-overview

---

## System Design

```
Your code calls LOG("message")
  │
  │  CPU writes bytes into circular RAM buffer
  │  Takes: ~50 nanoseconds per message
  │  CPU returns immediately
  ▼
Circular buffer in RAM (512 bytes)
  ↑ CPU writes here (write_position advances)
  ↓ DMA reads from here (read_position advances)
  ▼
DMA hardware (DMAC channel 0)
  Trigger: SERCOM5 TX data register empty
  Action: one byte per trigger, no CPU involvement
  ▼
SERCOM5 DATA register (address 0x42001818)
  Shifts bits out electrically on PB22
  86.8 microseconds per byte at 115200 baud
  ▼
Copper trace on PCB → nEDBG RX pin
  ▼
nEDBG chip
  Buffers bytes, forwards as USB CDC packets
  ▼
USB cable → Windows → Virtual COM port (COMx)
  ▼
PuTTY terminal
  Displays text in real time
```

---

## Circular Buffer State Machine

The buffer has two position counters:
- `log_write_position`: where the CPU writes the next byte
- `log_read_position`: where DMA reads the next byte to send

Both counters increase monotonically. The actual slot in the 512-byte array
is `position & 511` (bitmask equivalent to modulo 512, cheaper on Cortex-M0+).

```
Buffer full when:  (write_position - read_position) & 511 == 511
Buffer empty when: write_position == read_position
Bytes pending:     (write_position - read_position) & 511
```

DMA state machine:

```
State IDLE (dma_is_active == 0):
  CPU writes to buffer.
  On first write: CPU fills descriptor, enables DMA → go to RUNNING.

State RUNNING (dma_is_active == 1):
  CPU writes to buffer freely.
  CPU does NOT touch DMA.
  DMA sends bytes one at a time, triggered by SERCOM5.
  On Transfer Complete interrupt:
    → advance read_position by burst_size
    → if bytes_pending > 0: fill descriptor for new bytes, restart DMA (stay RUNNING)
    → if bytes_pending == 0: dma_is_active = 0 → go to IDLE
```

The key insight: when DMA finishes a burst and checks for more bytes, it sees
all bytes that accumulated during the burst — whether they came from 1 LOG() call
or from 50 LOG() calls. It sends them all in the next burst. The number of
DMA restarts is proportional to the number of "idle gaps" in the transmit stream,
not the number of LOG() calls.

---

## CPU Cost Breakdown

| Action | CPU instructions | Time at 48 MHz |
|---|---|---|
| Write 20-byte message (DMA running) | ~17 | 0.35 µs |
| Write 20-byte message (DMA idle, kick DMA) | ~28 | 0.58 µs |
| Transfer Complete interrupt handler | ~15 | 0.31 µs |

For a 100 Hz MPPT loop logging 30 bytes per iteration:
- LOG() call overhead: 100 × 0.35 µs = 35 µs/second = **0.003% of CPU**
- Interrupt overhead: ~200 firings × 0.31 µs = 62 µs/second = **0.006% of CPU**
- **Total logging overhead: ~0.01% of CPU time**

---

## Throughput and Buffer Sizing

At 115200 baud: 11,520 bytes/second maximum throughput.

For a 100 Hz loop logging 30 bytes per iteration: 3,000 bytes/second (26% utilization).
For a 1000 Hz loop logging 30 bytes per iteration: 30,000 bytes/second — exceeds capacity.

If you log faster than 11,520 bytes/second, the circular buffer fills up. The
implementation silently drops bytes (the log message is truncated) rather than
corrupting memory or blocking the CPU. You will notice this as truncated log lines.

The buffer size of 512 bytes is sufficient for a 100 Hz control loop. If your
loop rate is higher, either reduce log frequency (log every Nth iteration) or
increase the buffer to absorb bursts. The buffer size must always be a power of 2.

---

## Implementation

**debug_log_dma.h**

```c
#ifndef DEBUG_LOG_DMA_H
#define DEBUG_LOG_DMA_H

#include <stdint.h>

#ifdef DEBUG_LOGGING_ENABLED
    void debug_log_initialize_dma_uart_on_sercom5_pb22(void);
    void debug_log_write_string_into_transmit_buffer(const char *string);
    void debug_log_write_uint32_value(const char *label, uint32_t value);
    void debug_log_write_int32_value(const char *label, int32_t value);

    #define LOG(msg)            debug_log_write_string_into_transmit_buffer(msg "\r\n")
    #define LOG_U(label, val)   debug_log_write_uint32_value(label, val)
    #define LOG_I(label, val)   debug_log_write_int32_value(label, val)
#else
    // Flight build: all macros compile to zero bytes, zero instructions
    #define LOG(msg)            ((void)0)
    #define LOG_U(label, val)   ((void)0)
    #define LOG_I(label, val)   ((void)0)
#endif

#endif // DEBUG_LOG_DMA_H
```

**Key implementation notes for debug_log_dma.c:**

The SRCADDR in a SAMD21 DMA descriptor points to the END of the source range,
not the start. This is a SAMD21-specific convention that differs from other
microcontroller families. If you get this wrong, DMA sends garbage.

```c
// Correct: SRCADDR points past the last byte
dma_base_descriptors[0].SRCADDR.reg =
    (uint32_t)&log_circular_buffer[read_pos] + burst_size;
```

Source: Confirmed in the SAMD21 DMA tutorial:
        https://aykevl.nl/2019/09/samd21-dma
        "The source address stored into the configuration must correspond to
        the end of the transfer, not the beginning."

The DMA trigger for SERCOM5 TX is `SERCOM5_DMAC_ID_TX`, defined in the DFP headers.
This constant is the numeric trigger ID that tells the DMAC which peripheral to
listen to. Using the wrong trigger ID means DMA never starts.

The Transfer Complete interrupt handler MUST clear the interrupt flag immediately
on entry, before doing anything else:
```c
DMAC->CHINTFLAG.reg = DMAC_CHINTFLAG_TCMPL;
```
If you do not clear it, the interrupt fires again immediately upon return, creating
an infinite interrupt loop that prevents the CPU from doing anything else.

---

## Reading Logs on Windows

Open PuTTY:
- Connection type: Serial
- Serial line: COMx (find in Device Manager → Ports)
- Speed: 115200
- Data bits: 8, Stop bits: 1, Parity: None, Flow control: None

To save logs to a file: Session → Logging → All session output → choose file path.

Alternative: Windows Terminal with a serial connection, or any terminal emulator
that supports serial ports (Tera Term, RealTerm).

The COM port number is assigned by Windows and may change if you plug into a
different USB port. Check Device Manager if logs are not appearing.

---

## Verification

After implementing the logging system, the pass criterion for Phase 2 is:

1. Flash the board with main() calling `LOG("BOOT OK\r\n")`
2. Open PuTTY on the correct COM port at 115200 baud
3. See "BOOT OK" appear within 1 second of power-up
4. The message appears once per reset, not continuously
5. The LED (if still blinking from Phase 1) continues to blink at the same rate
   — confirming that logging does not interfere with timing

---

## Things to Be Careful About

**PB22 mux function is D (SERCOM-ALT), not C (SERCOM).** On the SAMD21, each pin
can route to two different SERCOMs: one via mux C, one via mux D. PB22 reaches
SERCOM5 via mux D. Using mux C routes it to a different SERCOM and nothing works.

**Port group index.** PB22 is in `PORT->Group[1]` (Port B), not `PORT->Group[0]` (Port A).
PB22 is pin 22 within group B, so the PMUXE/PMUXO index is `22/2 = 11`.

**TXPO must be 1 for PAD[2].** The SERCOM UART TXPO field selects which pad group
TX uses. TXPO=1 means TX is on PAD[2] (which is PB22 via the mux). TXPO=0 would
select PAD[0] instead — wrong pin.

**DMA channel 0 is used exclusively for logging.** If other modules need DMA
(ADC, SPI, etc.), they must use channels 1 and above.

**The `volatile` qualifier on shared variables.** Variables written by the DMAC_Handler
ISR and read by the main loop must be `volatile`. Without it, the compiler caches
values in registers and the main loop never sees ISR updates.
