# OBC UART Driver — Technical Reference

This document explains the design, hardware configuration, and internal workings
of the OBC UART driver (`uart_obc_sercom0_pa04_pa05.c`). It is intended for
future developers (human or AI) who need to modify the driver, add features, or
debug communication issues.

---

## Quick Summary

| Property | Value |
|---|---|
| Source file | `src/drivers/uart_obc_sercom0_pa04_pa05.c` |
| Header file | `src/drivers/uart_obc_sercom0_pa04_pa05.h` |
| SERCOM | SERCOM0 |
| TX pin | PA04 — SERCOM0 PAD[0], mux D (value 0x3) |
| RX pin | PA05 — SERCOM0 PAD[1], mux D (value 0x3) |
| Baud rate | 115200 (BAUD register = 63019, assuming 48 MHz GCLK0) |
| TX method | Non-blocking, interrupt-driven (DRE interrupt + TX ring buffer) |
| RX method | Non-blocking, interrupt-driven (RXC interrupt + RX ring buffer) |
| Buffer sizes | 256 bytes each for TX and RX |
| Interrupt handler | SERCOM0_Handler (handles both TX and RX) |
| GCLK channel | SERCOM0_GCLK_ID_CORE = 20, sourced from GCLK0 (48 MHz) |
| NVIC IRQ | SERCOM0_IRQn = 9 |
| CPU overhead | ~0.6 us per byte (interrupt handler). Max sustained: 0.7% CPU |

---

## Pin Selection Rationale

SERCOM0 on PA04/PA05 was chosen to avoid conflicts with all other peripherals
needed in later phases. This table shows every peripheral assignment, verified
against the SAMD21 datasheet and DM320119 board user guide:

| Phase | Peripheral | SERCOM | Pins | Mux |
|---|---|---|---|---|
| 1 (done) | Debug UART TX | SERCOM5 | PA22 | D |
| **3 (this)** | **OBC UART TX+RX** | **SERCOM0** | **PA04, PA05** | **D** |
| 5 (future) | TCC0 complementary PWM | N/A (TCC0) | PA18 (WO[2]), PA20 (WO[6]) | F |
| 6 (future) | I2C for INA226 | SERCOM3 | PA16, PA17 | D |
| 6 (future) | SPI for temp sensor | SERCOM4 | PA12, PA13, PA14, PA15 | D |
| N/A | SWD debug | N/A | PA30, PA31 | N/A |
| N/A | User LED | N/A | PB10 | GPIO |
| N/A | User button | N/A | PB11 | GPIO |
| N/A | nEDBG CDC UART | SERCOM5 | PA22 (TX), PB22 (RX) | D |

**Verified no-conflict sources:**
- DM320119 User Guide Table 4-4 (p.13): PA04/PA05 have NO debugger connections
- DM320119 User Guide Section 4.1.1 (p.11): all pins except PB10/PB11 on headers
- SAMD21 Datasheet Table 7-1 (p.29): PA04 = SERCOM0/PAD[0] mux D, PA05 = SERCOM0/PAD[1] mux D
- PA04 is also TCC0/WO[0] (mux E), but TCC0 Phase 5 will use DTI channel 2 on
  PA18/PA20 instead, so no conflict

---

## Public API

### `void uart_obc_initialize_sercom0_at_115200_baud(void)`

Must be called after the 48 MHz clock is configured. Sets up SERCOM0, pins,
baud rate, and enables the RXC interrupt. Call once at boot.

### `void uart_obc_send_bytes(const uint8_t *bytes_to_send, uint32_t number_of_bytes_to_send)`

Copies bytes into the TX ring buffer and enables the DRE interrupt. Returns
immediately. The interrupt handler sends bytes in the background. If the buffer
is full, excess bytes are silently dropped (the main loop is never blocked).

### `uint32_t uart_obc_number_of_bytes_available_in_receive_buffer(void)`

Returns the count of bytes waiting in the RX ring buffer. Zero means no data.

### `uint8_t uart_obc_read_one_byte_from_receive_buffer(void)`

Returns one byte from the RX ring buffer and advances the read pointer. Returns
0 if the buffer is empty (but callers should check `bytes_available` first).

---

## Internal Architecture

### Ring buffer design

Both TX and RX use the same ring buffer pattern:

```
    Buffer: [ . . . D A T A . . . . . . . ]
                    ^           ^
                    read_index  write_index

    bytes_available = (write_index - read_index) & INDEX_MASK
```

- Buffer size is 256 bytes (power of 2)
- Index mask is 0xFF (256 - 1)
- Indices wrap around using bitwise AND: `index = (index + 1) & 0xFF`
- When `write_index == read_index`, the buffer is empty
- When `(write_index + 1) & MASK == read_index`, the buffer is full (255 bytes max)
- `volatile` on all indices shared between the interrupt handler and main loop

### Data flow: sending a byte

```
1. main loop calls uart_obc_send_bytes("PING\n", 5)
2. Function copies 5 bytes into TX ring buffer, advances transmit_write_index
3. Function writes SERCOM_INTENSET = DRE_Msk (enables DRE interrupt)
4. Function returns immediately to the main loop

5. SERCOM0 hardware has DATA register empty → DRE flag is set → interrupt fires
6. SERCOM0_Handler checks: DRE flag set AND transmit buffer not empty
7. Handler reads byte from TX buffer at transmit_read_index
8. Handler writes byte to SERCOM0 DATA register (starts sending on wire)
9. Handler advances transmit_read_index

10. SERCOM0 hardware shifts byte out on PA04 pin (takes 86.8 us at 115200 baud)
11. When done, DRE flag set again → interrupt fires → handler sends next byte
12. Repeat steps 6-11 until TX buffer is empty

13. When TX buffer is empty, handler writes SERCOM_INTENCLR = DRE_Msk
    (disables DRE interrupt to stop it from firing repeatedly with nothing to send)
```

### Data flow: receiving a byte

```
1. External device (ESP32) sends a byte on the wire
2. SERCOM0 hardware captures it into the RxDATA buffer via the receive shift register
3. RXC (Receive Complete) flag is set → interrupt fires

4. SERCOM0_Handler checks: RXC flag set
5. Handler reads byte from SERCOM0 DATA register (this auto-clears the RXC flag)
6. Handler stores byte in RX ring buffer at receive_write_index
7. Handler advances receive_write_index
8. Handler returns (~0.6 us total)

9. Later, main loop calls uart_obc_number_of_bytes_available_in_receive_buffer()
10. If > 0, main loop calls uart_obc_read_one_byte_from_receive_buffer()
11. Function returns byte from RX buffer at receive_read_index, advances the index
```

### SERCOM0_Handler: combined TX+RX interrupt handler

The SAMD21 has ONE interrupt vector per SERCOM. Both RXC (receive) and DRE
(transmit) events trigger the same handler. The handler checks INTFLAG to
determine which event(s) occurred and handles each:

```c
void SERCOM0_Handler(void)
{
    flags = read INTFLAG

    if (RXC flag set):
        read DATA → store in RX buffer

    if (DRE flag set):
        if (TX buffer has data):
            write next byte to DATA
        else:
            disable DRE interrupt (nothing to send)
}
```

If both RXC and DRE fire simultaneously, both are handled in a single invocation
(~1.2 us total). The ARM Cortex-M0+ automatically pends any new interrupt that
occurs while the handler is running, so no events are lost.

---

## Hardware Details

### SERCOM0 registers used

| Register | Offset | Purpose |
|---|---|---|
| SERCOM_CTRLA | 0x00 | Mode, TXPO, RXPO, DORD, ENABLE |
| SERCOM_CTRLB | 0x04 | TXEN, RXEN |
| SERCOM_BAUD | 0x0C | Baud rate value (63019 for 115200 at 48 MHz) |
| SERCOM_INTENSET | 0x16 | Enable specific interrupts (RXC, DRE) |
| SERCOM_INTENCLR | 0x14 | Disable specific interrupts (DRE when TX idle) |
| SERCOM_INTFLAG | 0x18 | Read which interrupt fired (RXC, DRE) |
| SERCOM_SYNCBUSY | 0x1C | Wait for cross-domain register synchronization |
| SERCOM_DATA | 0x28 | Write = TxDATA (send byte), Read = RxDATA (receive byte) |

### DFP v3.6.144 register access pattern

```c
SERCOM0_REGS->USART_INT.SERCOM_CTRLA        // not SERCOM0->USART.CTRLA.reg
SERCOM_USART_INT_CTRLA_DORD_LSB             // not SERCOM_USART_CTRLA_DORD
SERCOM_USART_INT_CTRLB_RXEN_Msk             // not SERCOM_USART_CTRLB_RXEN
SERCOM_USART_INT_INTENSET_RXC_Msk           // not SERCOM_USART_INTENSET_RXC
```

These are the DFP v3.6.144 names. Online examples and Stack Overflow use the ASF3
style (shorter names). Always translate to the DFP style before using.

### Pin configuration details

**PA04 (TX) — PINCFG and PMUX:**
- PINCFG[4]: PMUXEN = 1 (enable peripheral mux, pin is no longer GPIO)
- PMUX[2] lower nibble (even pin): 0x3 = mux D = SERCOM-ALT = SERCOM0 PAD[0]
- INEN not needed for output pins

**PA05 (RX) — PINCFG and PMUX:**
- PINCFG[5]: PMUXEN = 1, INEN = 1 (input buffer enable — CRITICAL)
- PMUX[2] upper nibble (odd pin): 0x3 << 4 = mux D = SERCOM-ALT = SERCOM0 PAD[1]
- Without INEN, the pin's input buffer is disabled and the SERCOM cannot read the
  voltage level on the wire. This is the most common RX pin configuration bug.

**CTRLA pin routing:**
- TXPO = 0: TX on PAD[0] = PA04
- RXPO = 1: RX on PAD[1] = PA05

### Clock configuration

SERCOM0 needs two clocks:
1. **Bus clock** (APB C): `PM_REGS->PM_APBCMASK |= PM_APBCMASK_SERCOM0_Msk`
   — allows the CPU to read/write SERCOM0 registers
2. **Functional clock** (GCLK): GCLK0 (48 MHz) connected to SERCOM0_GCLK_ID_CORE (20)
   — the clock the SERCOM uses to generate baud timing and shift bits

Without the bus clock, register writes are silently ignored. Without the functional
clock, the SERCOM cannot operate even though registers appear writable.

### Baud rate calculation

For asynchronous mode with 16x oversampling (SAMPR=0):

```
BAUD = 65536 × (1 - 16 × (f_baud / f_ref))
     = 65536 × (1 - 16 × (115200 / 48000000))
     = 65536 × (1 - 0.0384)
     = 65536 × 0.9616
     = 63019
```

Actual baud rate: 115206 Hz. Error: +0.005%. UART tolerance: up to ~2%.

---

## Timing Analysis

### Response time for CHIPS protocol (Phase 4)

The CHIPS protocol requires the EPS to respond within 1 second.

| Step | Duration |
|---|---|
| Receive worst-case command (1030 bytes) | 89.4 ms |
| Process command | < 1 ms |
| Send worst-case response (1030 bytes) | 89.4 ms |
| **Total** | **~180 ms** |
| **Safety margin vs 1-second timeout** | **5.6x** |

### CPU overhead at maximum sustained throughput

| Metric | Value |
|---|---|
| Bytes per second (one direction) | 11,520 |
| Interrupt handler time per byte | ~0.6 us |
| CPU time per second (both directions) | 13.8 ms |
| CPU overhead percentage | 1.38% |
| Free CPU time between interrupts | ~42 us minimum (2,000 instructions) |

---

## Changing the SERCOM or Pins

If you need to move the OBC UART to a different SERCOM or different pins (for
example, to match the final satellite PCB layout):

1. Choose new SERCOM and pins from SAMD21 Datasheet Table 7-1
2. Verify new pins are not used by other peripherals (see conflict table above)
3. In the `.c` file, update:
   - `PM_APBCMASK` to the new SERCOM (e.g., `PM_APBCMASK_SERCOM1_Msk`)
   - `GCLK_CLKCTRL_ID()` to the new GCLK ID (e.g., `SERCOM1_GCLK_ID_CORE`)
   - All `SERCOM0_REGS` to the new SERCOM (e.g., `SERCOM1_REGS`)
   - Pin configuration functions (pin numbers, PMUX index, even/odd nibble)
   - TXPO/RXPO values in CTRLA (depends on which PADs the new pins map to)
   - `NVIC_EnableIRQ(SERCOM0_IRQn)` to the new IRQ
   - ISR function name (e.g., `SERCOM1_Handler`)
4. Rename the files to reflect the new hardware
5. Update `Makefile` with the new filename
6. The BAUD register value stays the same (63019) as long as the GCLK source
   is still 48 MHz

---

## Known Limitations

1. **Buffer size is 256 bytes.** The maximum CHIPS frame after byte stuffing can be
   ~2060 bytes. The current 256-byte ring buffer cannot hold a full frame. For
   Phase 4 (CHIPS protocol), the frame parser must consume bytes as they arrive
   (streaming parse), not wait for the entire frame to be buffered.

2. **No overflow notification.** If the RX buffer overflows (256 bytes received
   without the main loop reading them), the `receive_overflow_has_occurred` flag
   is set but no error is reported to the caller. Phase 4 should check this flag.

3. **No assertion handler.** The driver uses simple guard checks instead of
   SATELLITE_ASSERT because `assertion_handler.h` doesn't exist yet. This should
   be created before Phase 4.

4. **TX drop is silent.** If `uart_obc_send_bytes()` is called with more data than
   the TX buffer can hold, excess bytes are dropped without notification.

---

## Debugging Tips

**No data received (OBC RX bytes always 0):**
- Check wiring: TX must connect to the OTHER device's RX, not TX-to-TX
- Check that PA05 PINCFG has INEN bit set (input buffer enable)
- Check that RXEN is set in CTRLB
- Check that RXC interrupt is enabled in INTENSET
- Check that NVIC_EnableIRQ(SERCOM0_IRQn) was called

**Garbled/corrupted data received:**
- Baud rate mismatch: verify GCLK0 is 48 MHz and BAUD register = 63019
- Check that both devices use the same format: 8N1 (8 data bits, no parity, 1 stop)
- Check that GCLK_CLKCTRL was written with the correct ID (20 for SERCOM0)
- Check that PM_APBCMASK SERCOM0 bit is set (bus clock enabled)

**Data sent but never arrives at ESP32:**
- Check wiring: SAMD21 PA04 should connect to ESP32 RX pin
- Check that PA04 PINCFG has PMUXEN set
- Check that PMUX[2] lower nibble = 0x3 (mux D)
- Check that TXEN is set in CTRLB
