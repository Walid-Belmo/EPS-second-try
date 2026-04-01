# CLAUDE.md — Instructions for Claude Code Instances

## Project

Bare-metal C firmware for SAMD21G17D on Curiosity Nano DM320119.
No HAL, no RTOS, no bootloader. Direct register access. DFP v3.6.144.

- Coding standards: `notes/conventions.md`
- Plan and status: `notes/plan.md`
- Project docs: `notes/readme.md`
- Build reference: `docs/how_to_build_and_flash.md`

---

## Principle 1: Never Trust Documentation — Always Verify Against the Datasheet

Project docs, online tutorials, Stack Overflow answers, and even Microchip's
own application notes contain errors. We have been burned by this.

**Before writing ANY register configuration code:**
1. Open the actual datasheet or user guide PDF in `datasheets/`
2. Find the specific register, pin, or peripheral you are configuring
3. Verify the bit positions, field values, and pin mappings yourself
4. Cross-reference the DFP header files in `lib/samd21-dfp/component/` to
   confirm the constant names and values match what the datasheet says

**For pin assignments specifically:** the chip datasheet tells you what the
silicon CAN do. The board user guide tells you what the PCB ACTUALLY connects.
These are different things. Always check BOTH.

Key reference documents in `datasheets/`:
- `samd21_datasheet.pdf` — chip registers, peripherals, electrical specs
- `dm320119_user_guide.pdf` — board schematic, nEDBG connections, pin routing

If the datasheets are missing, download them from Microchip before proceeding.

---

## Principle 2: If Something Doesn't Work, Question Your Assumptions First

When a peripheral produces no output or wrong output, do NOT start tweaking
register values or rewriting code. Instead:

1. List every assumption your code makes
2. Verify each assumption against the datasheet (Principle 1)
3. Start with the most basic assumption (is this the right pin? the right
   peripheral? the right clock source?)
4. Only after ALL assumptions are verified should you start debugging the
   code logic itself

The fastest path to fixing a bug is finding the wrong assumption, not
writing more code.

---

## Principle 3: One Change at a Time

Never add two new things simultaneously. If something breaks, you must
know which change caused it. This means:
- Get the clock working before adding UART
- Get blocking UART working before adding DMA
- Get one peripheral working before configuring a second

---

## How to Build, Flash, and Read Serial Output

You can do this autonomously without asking the user to open PuTTY.
COM6 (serial) and SWD (debug/flash) are independent interfaces.

### Build and flash

```bash
make clean && make    # must produce zero warnings
make flash            # must print "Verified OK"
```

### Read UART output from COM6

Write a temporary PowerShell script, run it in background, flash (which
resets the board), then read the background task output:

```powershell
# read_serial.ps1
$port = [System.IO.Ports.SerialPort]::new('COM6', 115200)
$port.ReadTimeout = 1000
$port.DtrEnable = $true
$port.Open()
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$buf = ''
while ($sw.ElapsedMilliseconds -lt 6000) {
    if ($port.BytesToRead -gt 0) { $buf += $port.ReadExisting() }
    Start-Sleep -Milliseconds 20
}
$port.Close()
if ($buf.Length -eq 0) { Write-Output 'NO DATA RECEIVED' }
else { Write-Output $buf }
```

```bash
# Run serial reader in background, then flash
powershell -ExecutionPolicy Bypass -File read_serial.ps1  # (run_in_background=true)
sleep 1 && make flash
# Read the background task output file to see UART messages
```

Delete the temp script when done.

---

## Project Structure

```
src/main.c                            ← application code (changes often)
src/drivers/                          ← hardware drivers (stable, add new ones here)
startup/                              ← vendor startup (never edit)
lib/                                  ← vendor headers (never edit)
datasheets/                           ← reference PDFs (always check these)
docs/                                 ← technical documentation
notes/                                ← conventions, plan, readme
code_samples/                         ← saved working milestones
```

## DFP Register API Style

```c
PORT_REGS->GROUP[1].PORT_DIRSET       // NOT PORT->Group[1].DIRSET.reg (ASF3 style)
SERCOM5_REGS->USART_INT.SERCOM_CTRLA  // NOT SERCOM5->USART.CTRLA.reg
```

Translate any ASF3-style code found online to DFP v3.6.144 style before using it.
