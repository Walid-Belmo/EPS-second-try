# How to Recover From a Stalled Debug Port (AP Stall)

This document explains what an AP stall is, why it happens, how we got into
one on 2026-04-01, and exactly how to recover. If OpenOCD prints
**"stalled AP operation, issuing ABORT"** and refuses to connect, this is
the document to read.

---

## What Happened to Us

We flashed code that tried to reconfigure the CPU clock from 1 MHz to 48 MHz
(DFLL48M). The clock configuration code had a bug and crashed the chip
immediately on boot. This left the debug access port frozen, and OpenOCD
could no longer communicate with the chip — not even to erase it and flash
working code.

The board appeared completely dead. The LED did not blink. OpenOCD printed
the same error on every attempt. Power cycling did not help. The problem
persisted for ~30 minutes before we found the solution.

---

## The Error Message

```
Info : SWD DPIDR 0x0bc11477
Warn : Connecting DP: stalled AP operation, issuing ABORT
Error: Failed to read memory and, additionally, failed to find out where
Error: [at91samd.cpu] Examination failed
```

If you see this, you have an AP stall.

---

## What These Terms Mean

The debug system on the SAMD21 has layers, like doors you must pass through
to reach the chip's memory:

```
Your laptop
  │
  │  USB
  ▼
nEDBG chip (the debugger built into the Curiosity Nano board)
  │
  │  SWD protocol (2 wires: SWCLK + SWDIO)
  ▼
DP (Debug Port) — the "outer door"
  OpenOCD can always talk to this. It responds with DPIDR 0x0bc11477.
  This part works even when the chip is crashed.
  │
  ▼
AP (Access Port) — the "inner door"
  This connects to the chip's internal memory bus.
  When the chip crashes mid-operation, the AP gets stuck waiting for
  a response from the bus that will never come. This is the "stall."
  │
  ▼
Chip internals (flash, RAM, peripherals)
  Cannot be reached while the AP is stalled.
```

**DP works, AP is stuck.** That is the problem.

---

## Why Power Cycling Does Not Fix It

After a power cycle, the chip boots from flash. If the code in flash is
the same code that caused the crash, the chip crashes again within
microseconds of powering on — before OpenOCD can even establish a connection.
The AP stalls again immediately.

This creates a chicken-and-egg problem:
- We need to erase flash to remove the bad code
- We need the AP to work to erase flash
- The AP stalls because the bad code runs on boot

---

## Why It Happened

The clock configuration code wrote to hardware registers that control the
CPU's clock source. If any step in the sequence is wrong — wrong register
value, wrong order, missing wait — the CPU's clock can stop, glitch, or
run at an invalid frequency. When the clock fails, the CPU halts mid-
instruction, leaving the debug bus in an undefined state.

Specifically, our code tried to switch GCLK0 (the CPU clock generator)
from OSC8M to DFLL48M. Something in the DFLL48M configuration was incorrect,
and the CPU crashed during or immediately after the switch.

---

## How We Fixed It

### Step 1: Use MPLAB X IDE to erase the chip

OpenOCD could not recover the board on its own because its AP stall recovery
is not robust enough for this scenario. Microchip's own tool (MPLAB X IDE)
has a proprietary connection to the nEDBG that handles AP stalls internally.

**Exact procedure:**

1. Open **MPLAB X IDE** (already installed at `C:\Program Files\Microchip\MPLABX\v6.30\`)
2. Create or open any project targeting **ATSAMD21G17D**
3. Make sure the board is plugged in via USB
4. Go to menu: **Production → Erase Device Memory Main Project**
5. Wait for the output to show:
   ```
   Target device ATSAMD21G17D found.
   Erasing...
   Erase successful
   ```

This erases all flash contents. The bad code is gone. The AP stall clears.

### Step 2: Flash working code with OpenOCD

After the MPLAB erase, OpenOCD works normally again:

```bash
make flash
```

Expected output includes `** Verified OK **` with no AP stall errors.

### Step 3: Verify the board is alive

The LED should blink after flashing the safe blink code.

---

## What Does NOT Work for Recovery

We tried all of these before finding the MPLAB solution. They are documented
here so nobody wastes time on them again:

| Approach | Result |
|---|---|
| Power cycling (unplug USB, replug) | AP stalls again immediately — bad code re-runs on boot |
| OpenOCD `at91samd chip-erase` | Fails — cannot access DSU because AP is stalled |
| OpenOCD `connect_assert_srst` | nEDBG does not support hardware reset line control via OpenOCD |
| OpenOCD `cortex_m reset_config sysresetreq` | AP stalls before the reset command can execute |
| Raw DAP writes to DSU chip-erase register | Writes appear to succeed but erase does not take effect (possibly because AP is stalled and writes don't reach the bus) |
| pyOCD `erase --chip` | Cannot detect the nEDBG probe (driver incompatibility) |
| MPLAB IPE command line (`ipecmd.exe`) | Does not recognize nEDBG as a supported programmer type from the command line |
| Lowering SWD clock speed | Does not help — the stall is on the chip side, not the communication side |

**The only method that worked: MPLAB X IDE → Production → Erase Device Memory Main Project.**

---

## How to Prevent This in the Future

1. **Test clock changes incrementally.** Do not switch to a new clock source
   in one step. First configure the new oscillator and verify it is running
   (poll the ready flag), then switch the clock source.

2. **Keep a known-good binary handy.** The safe blink code (no clock changes,
   no UART) is saved in `code_samples/01_blink_and_button/`. If a new build
   crashes the board, erase with MPLAB X and flash the safe binary.

3. **Always have MPLAB X IDE installed.** It is the only reliable recovery
   tool for the nEDBG on the Curiosity Nano. OpenOCD alone is not sufficient
   for AP stall recovery on this board.

4. **Do not remove MPLAB X.** Even though we do all development with
   arm-none-eabi-gcc and OpenOCD, MPLAB X is our emergency recovery tool.

---

## Quick Reference

**Symptom:** OpenOCD says "stalled AP operation, issuing ABORT"

**Fix:**
1. Open MPLAB X IDE
2. Production → Erase Device Memory Main Project
3. `make flash` with known-good code

**Time to recover:** ~2 minutes once you know the procedure.

---

*This document was written on 2026-04-01 after spending ~30 minutes stuck
on an AP stall caused by bad clock configuration code. The recovery procedure
using MPLAB X IDE was verified to work on the SAMD21G17D Curiosity Nano
DM320119 with nEDBG debugger firmware version 01.22.0059.*
