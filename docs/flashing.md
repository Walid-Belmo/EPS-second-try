# Flashing — OpenOCD and the Build-Flash Cycle

This document explains how OpenOCD works, how it connects to the chip through
the nEDBG, what the flash command actually does, and how to diagnose failures.

---

## How OpenOCD Fits in the Picture

OpenOCD is a server that runs on your laptop. It does not debug your code
directly — it translates between two worlds:

```
Your laptop                        The board
──────────────────────────────     ──────────────────────────────
OpenOCD process
  │
  │  USB (CMSIS-DAP protocol)
  ▼
nEDBG chip (pre-programmed
  by Microchip, you cannot
  change it)
  │
  │  SWD: 2 wires
  │  SWDIO (PA31) + SWDCLK (PA30)
  ▼
SAMD21G17D DAP hardware
  │
  │  internal bus
  ▼
CPU core, flash controller, RAM
```

When OpenOCD flashes your binary, the CPU is halted by the DAP hardware.
The DAP then writes directly to the flash controller, bypassing the CPU
entirely. Your firmware code plays no role in the flash process.

Source: OpenOCD official documentation, https://openocd.org/doc/pdf/openocd.pdf
Source: Interrupt/Memfault SAMD21 programming guide,
        https://interrupt.memfault.com/blog/getting-started-with-ibdap-and-atsamd21g18

---

## openocd.cfg for the DM320119

```
# openocd.cfg
adapter driver cmsis-dap
transport select swd
source [find target/at91samdXX.cfg]
adapter speed 4000
```

The `at91samdXX.cfg` file ships with OpenOCD and covers all SAMD21 variants.
OpenOCD auto-detects the specific device variant at runtime by reading its
Device ID register.

Source: Confirmed working with SAMD21G18A (same family) at
        https://omzlo.com/articles/programming-the-samd21-using-atmel-ice-with-openocd-(updated)
        and https://interrupt.memfault.com/blog/getting-started-with-ibdap-and-atsamd21g18

---

## The Flash Command

```bash
openocd \
    -f openocd.cfg \
    -c "program build/satellite_firmware.bin verify reset exit 0x00000000"
```

What each part does:

`program build/satellite_firmware.bin` — erases the necessary flash pages and
writes the binary.

`verify` — reads back every written byte and compares to the original binary.
If any byte mismatches, OpenOCD exits with an error. If it says "verified OK"
you can trust the flash contents exactly match your binary.

`reset` — releases the CPU halt. The chip starts running from the beginning of
flash immediately.

`exit` — closes the OpenOCD server.

`0x00000000` — the address where the binary is placed in flash. Since we have no
bootloader, code starts at address 0. This must match the origin in the linker script.

In the Makefile this becomes `make flash`, which first compiles (ensuring the
binary is up to date) then runs this command.

---

## What Successful Output Looks Like

```
Open On-Chip Debugger 0.12.0
Info : CMSIS-DAP: SWD Supported
Info : CMSIS-DAP: Interface Initialised (SWD)
Info : SWCLK/TCK = 1 SWDIO/TMS = 1
Info : CMSIS-DAP: Interface ready
Info : clock speed 4000 kHz
Info : SWD DPIDR 0x0bc11477
Info : at91samd21.cpu: hardware has 4 breakpoints, 2 watchpoints
** Programming Started **
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
shutdown command invoked
```

`SWD DPIDR 0x0bc11477` is the debug port ID register value for all Cortex-M0+
chips. Seeing this confirms SWD communication is working.

`Verified OK` is the only thing that matters. If you see it, the binary is in flash.

---

## Common Errors and Fixes

**Error: LIBUSB_ERROR_ACCESS**

OpenOCD cannot access the USB device. Two causes:

On Windows: The nEDBG USB device needs the WinUSB driver installed via Zadig.
See `docs/toolchain_setup_windows.md`.

**Error: unable to find CMSIS-DAP device**

OpenOCD cannot find the debugger at all. Check:
1. Is the USB cable plugged in?
2. Is it a data cable, not a charge-only cable?
3. Is the green LED on the board lit? (it should be after power-up)
4. Does Device Manager show the device?

**Error: SAMD: NVM lock error / failed to erase sector 0**

This means flash page 0 is write-protected. This happens if a bootloader was
previously flashed with protection enabled. Fix:

```
openocd -f openocd.cfg -c "init; reset halt; at91samd bootloader 0; reset; exit"
```

This disables the bootloader protection region, allowing full flash access.
Source: https://interrupt.memfault.com/blog/getting-started-with-ibdap-and-atsamd21g18

**Error: DAP_CONNECT failed**

Usually a driver issue on Windows. Try Zadig, switch CMSIS-DAP interface to WinUSB.

**Error: target halted due to debug-request, pc: 0xfffffffe**

The CPU is stuck at an invalid address — it crashed before OpenOCD could halt it.
OpenOCD can still flash in this state. Flash and reset, see if the new code works.

---

## Using OpenOCD for GDB (Optional — for Crash Diagnosis Only)

For situations where UART logs are insufficient (HardFault, stack overflow, memory
corruption), OpenOCD can serve as a GDB backend.

Terminal 1 — start OpenOCD server (leave it running):
```bash
openocd -f openocd.cfg
```

Terminal 2 — connect GDB:
```bash
arm-none-eabi-gdb build/satellite_firmware.elf
(gdb) target remote localhost:3333
(gdb) monitor reset halt
(gdb) load
(gdb) break HardFault_Handler
(gdb) continue
```

Important limitation: the Cortex-M0+ has only 4 hardware breakpoints and 2
hardware watchpoints. Halting the CPU with a breakpoint stops all timers and
interrupts. For timing-sensitive code, use UART logging instead of breakpoints.
GDB is for crash diagnosis, not for normal development flow.

For crash diagnosis specifically, `backtrace` is the most useful command. When
HardFault fires, halt and type `backtrace` — it shows the exact call chain that
led to the fault.

Source: Adafruit SAMD21 GDB guide,
        https://learn.adafruit.com/debugging-the-samd21-with-gdb
