# Code Sample 05 — CHIPS Protocol (Phase 4)

Working snapshot of the CHIPS (CHESS Internal Protocol over Serial) implementation
on the SAMD21G17D. All 9 ESP32 integration tests pass. Zero compiler warnings.

## What This Code Does

The SAMD21 acts as the EPS slave in the CHIPS protocol:
- Receives HDLC-framed commands from the OBC (or ESP32 test harness) over UART
- Parses frames using a streaming state machine (one byte at a time)
- Validates CRC-16/KERMIT checksums
- Removes PPP-style byte stuffing (0x7E/0x7D escaping)
- Dispatches commands to handler functions (GET_TELEMETRY, SET_PARAMETER, etc.)
- Builds response frames with CRC and byte stuffing
- Sends responses back over UART
- Tracks idempotency (duplicate sequence numbers re-send cached response)
- Detects 120-second OBC communication timeout

Telemetry data is placeholder zeros — real sensor values come in Phase 6.

## Files

### SAMD21 firmware (src/)
| File | Purpose |
|---|---|
| `src/main.c` | Main loop: polls UART, feeds parser, dispatches commands, heartbeat, timeout |
| `src/assertion_handler.h/.c` | SATELLITE_ASSERT macro (debug=log+freeze, flight=reset) |
| `src/chips_protocol_dispatch_commands_and_build_responses.h/.c` | 6 command handlers, idempotency, telemetry struct |
| `src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.h/.c` | CRC-16/KERMIT, byte stuffing, frame parser, frame builder |
| `src/drivers/millisecond_tick_timer_using_arm_systick.h/.c` | 1ms SysTick timer for timeout and heartbeat |

### ESP32 test harness
| File | Purpose |
|---|---|
| `esp32_test_harness/02_chips_protocol_test/02_chips_protocol_test.ino` | 9 automated tests acting as OBC |

## How to Build and Flash the SAMD21

```bash
make clean && make    # must produce zero warnings
make flash            # must print "Verified OK"
```

Open PuTTY on COM6 at 115200 baud to see:
```
=== BOOT OK — CHIPS Phase 4 ===
--- Running CHIPS self-tests ---
CRC self-test: 0x2189 PASS
Frame round-trip self-test PASS
--- All CHIPS self-tests PASSED ---
CHIPS protocol ready — waiting for OBC commands
```

## How to Run the ESP32 Tests

1. Open `esp32_test_harness/02_chips_protocol_test/02_chips_protocol_test.ino` in Arduino IDE
2. Select your ESP32 board and upload
3. Wire:
   ```
   ESP32 TX2 (GPIO17) --> SAMD21 PA05 (RX)
   ESP32 RX2 (GPIO16) <-- SAMD21 PA04 (TX)
   ESP32 GND ------------ SAMD21 GND
   ```
4. Open ESP32 Serial Monitor at 115200 baud
5. Tests run automatically, one every 3 seconds
6. Expected output: `ALL TESTS COMPLETE: 9 passed, 0 failed`
7. Test 9 (120s timeout) takes ~2.5 minutes — be patient

**IMPORTANT:** The ESP32 sketch must use explicit pin arguments for Serial2:
```c
Serial2.begin(115200, SERIAL_8N1, 16, 17);  // RX=GPIO16, TX=GPIO17
```
Without explicit pins, some ESP32 boards use wrong default pins and no data arrives.

## Test Cases

| # | Test | What it verifies |
|---|---|---|
| 1 | GET_TELEMETRY | Basic command/response, CRC, 220-byte payload |
| 2 | SET_PARAMETER | Command with 5-byte payload |
| 3 | Duplicate seq=0 | Idempotency — cached response re-sent, not re-executed |
| 4 | Corrupted CRC | Bad frames silently dropped, no response |
| 5 | Unknown cmd 0x77 | Error status returned for unrecognized commands |
| 6 | 0x7E in payload | Byte stuffing correctly encodes/decodes |
| 7 | Wrong payload size | Payload validation catches mismatched lengths |
| 8 | Extra 0x7E before frame | Inter-frame fill handled per RFC 1662 |
| 9 | 130s silence | 120s OBC timeout detected, EPS still responds after |

## Memory Usage

| Region | Used | Total | Utilization |
|---|---|---|---|
| Flash | 10,764 bytes | 128 KB | 8.2% |
| RAM | 6,856 bytes | 16 KB | 41.8% |

## Dependencies

This code requires the existing Phase 1-3 drivers (not included in this snapshot):
- `src/drivers/clock_configure_48mhz_dfll_open_loop.c/.h` — 48 MHz clock
- `src/drivers/debug_functions.c/.h` — DMA debug logging on SERCOM5
- `src/drivers/uart_obc_sercom0_pa04_pa05.c/.h` — OBC UART on SERCOM0

These are in the main project tree and must be present for the Makefile to work.
