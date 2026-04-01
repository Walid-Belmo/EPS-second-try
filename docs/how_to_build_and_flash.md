# How to Build and Flash — Complete Reference

**READ THIS FIRST if you are a new Claude Code instance working on this project.**

This document contains everything needed to compile C code and flash it onto
the SAMD21G17D Curiosity Nano DM320119 board. It was written after successfully
building and flashing for the first time on 2026-04-01. Every detail here has
been verified to work.

---

## The Board

- **Chip:** ATSAMD21G17D (ARM Cortex-M0+, 128KB flash, 16KB RAM)
- **Board:** Microchip Curiosity Nano DM320119
- **Debugger chip:** nEDBG (on-board, pre-programmed by Microchip, not reprogrammable)
- **Debug protocol:** SWD via CMSIS-DAP over USB
- **USB serial number:** MCHP3313043000000910
- **User LED:** PB10 (active low — drive LOW to turn ON)
- **User button:** PB11 (active low — reads LOW when pressed)
- **Virtual COM port:** SERCOM5, PB22 (TX), PB23 (RX), mux D
- **No drag-and-drop programming.** Must use OpenOCD.

---

## The Toolchain (Already Installed)

All tools are installed and on PATH. Do NOT reinstall them.

| Tool | Version | Location |
|---|---|---|
| `arm-none-eabi-gcc` | 12.2.1 (Arm GNU Toolchain 12.2.MPACBTI-Rel1) | `/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/12.2 mpacbti-rel1/bin/` |
| `arm-none-eabi-objcopy` | same as above | same directory |
| `arm-none-eabi-size` | same as above | same directory |
| `openocd` | 0.12.0 (xPack) | `/c/Users/iceoc/AppData/Local/Microsoft/WinGet/Packages/xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe/xpack-openocd-0.12.0-7/bin/` |
| `make` | 4.4.1 (GNU Make) | `/c/Users/iceoc/AppData/Local/Microsoft/WinGet/Packages/ezwinports.make_Microsoft.Winget.Source_8wekyb3d8bbwe/bin/` |

Verify with:
```bash
arm-none-eabi-gcc --version
openocd --version
make --version
```

---

## Project File Layout

```
EPS-second-try/
├── src/
│   └── main.c                          ← your application code
├── startup/                            ← vendor files, NEVER edit
│   ├── startup_samd21g17d.c            ← vector table + Reset_Handler
│   ├── system_samd21g17d.c             ← SystemInit (stub, does nothing)
│   └── system_samd21.h                 ← declares SystemInit, SystemCoreClock
├── lib/
│   ├── cmsis/                          ← ARM CMSIS headers, NEVER edit
│   │   ├── core_cm0plus.h
│   │   ├── cmsis_gcc.h
│   │   ├── cmsis_compiler.h
│   │   └── cmsis_version.h
│   └── samd21-dfp/                     ← Microchip device headers, NEVER edit
│       ├── samd21g17d.h                ← top-level device header
│       ├── sam.h
│       ├── system_samd21.h
│       ├── component-version.h
│       ├── component/                  ← one header per peripheral
│       ├── instance/                   ← peripheral base addresses
│       └── pio/
│           └── samd21g17d.h            ← pin definitions
├── build/                              ← compiler output (gitignored)
├── Makefile                            ← build system
├── openocd.cfg                         ← flash/debug connection config
├── syscalls_min.c                      ← C library stubs (see docs/newlib_and_syscalls.md)
├── samd21g17d_flash.ld                 ← linker script from DFP, NEVER edit
├── notes/                              ← conventions, plan, readme
└── docs/                               ← technical reference
```

---

## How to Build

From the project root, inside a terminal with the tools on PATH:

```bash
make clean && make
```

This does:
1. `arm-none-eabi-gcc` compiles each `.c` file listed in SRCS to a `.o` object file
2. `arm-none-eabi-gcc` links all `.o` files into `build/satellite_firmware.elf`
3. `arm-none-eabi-objcopy` converts the `.elf` to a raw binary `build/satellite_firmware.bin`
4. `arm-none-eabi-size` prints how much flash and RAM the firmware uses

**Expected output (zero warnings, zero errors):**
```
arm-none-eabi-gcc [flags] -c src/main.c -o build/main.o
arm-none-eabi-gcc [flags] -c startup/startup_samd21g17d.c -o build/startup_samd21g17d.o
arm-none-eabi-gcc [flags] -c startup/system_samd21g17d.c -o build/system_samd21g17d.o
arm-none-eabi-gcc [flags] -c syscalls_min.c -o build/syscalls_min.o
arm-none-eabi-gcc [linker flags] -o build/satellite_firmware.elf [objects]
arm-none-eabi-objcopy -O binary build/satellite_firmware.elf build/satellite_firmware.bin
   text    data     bss     dec     hex filename
    952       0    4128    5080    13d8 build/satellite_firmware.elf
```

The SIZE report:
- `text` = bytes in flash (code + constants). Must stay under 131072 (128KB).
- `data` = initialized globals copied from flash to RAM at boot.
- `bss` = zero-initialized globals in RAM.
- `data + bss` must stay under 16384 (16KB).

---

## How to Flash

**Plug in the Curiosity Nano via USB.** Then:

```bash
make flash
```

This runs `make` first (so the binary is up to date), then:

```bash
openocd -f openocd.cfg \
    -c "program build/satellite_firmware.bin verify reset exit 0x00000000"
```

**Expected output (successful flash):**
```
xPack Open On-Chip Debugger 0.12.0 [...]
Info : CMSIS-DAP: SWD supported
Info : CMSIS-DAP: FW Version = 02.01.0000
Info : CMSIS-DAP: Serial# = MCHP3313043000000910
Info : CMSIS-DAP: Interface Initialised (SWD)
Info : clock speed 4000 kHz
Info : SWD DPIDR 0x0bc11477
Info : [at91samd.cpu] Cortex-M0+ r0p1 processor detected
Info : [at91samd.cpu] target has 4 breakpoints, 2 watchpoints
[at91samd.cpu] halted due to debug-request [...]
** Programming Started **
Info : SAMD MCU: SAMD21G17D (128KB Flash, 16KB RAM)
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
shutdown command invoked
```

**The only line that matters is `** Verified OK **`.** If you see it, the
binary is correctly written to flash and the chip is now running your code.

---

## openocd.cfg Explained

```
adapter driver cmsis-dap          # nEDBG speaks CMSIS-DAP protocol over USB
transport select swd              # SWD (2-wire debug), not JTAG
source [find target/at91samdXX.cfg]   # OpenOCD's built-in config for SAMD21 family
adapter speed 4000                # SWD clock: 4 MHz (safe default, max is 8 MHz)
```

`at91samdXX.cfg` ships with OpenOCD. It auto-detects the specific SAMD21
variant by reading the chip's Device ID register at runtime.

---

## The Flash Command Explained

```
program build/satellite_firmware.bin verify reset exit 0x00000000
```

| Part | What it does |
|---|---|
| `program [file]` | Halts the CPU, erases necessary flash pages, writes the binary |
| `verify` | Reads back every byte and compares to the original — fails if any mismatch |
| `reset` | Releases the CPU halt, chip starts running from address 0 |
| `exit` | Closes the OpenOCD server process |
| `0x00000000` | Flash address where the binary is placed — must match the linker script origin |

---

## Compiler Flags Explained

These are in the Makefile and every single one matters:

### CPU flags
```
-mcpu=cortex-m0plus    # exact CPU core in the SAMD21G17D
-mthumb                # Thumb instruction set (Cortex-M0+ only supports Thumb)
-mfloat-abi=soft       # no hardware FPU — float operations are software-emulated
```
**Wrong CPU or instruction set = the chip cannot execute the code. Hard fault.**

### Preprocessor defines
```
-D__SAMD21G17D__       # selects the correct chip in the DFP headers
-DUSE_CMSIS_INIT       # makes samd21g17d.h include system_samd21.h
                       # (provides prototypes for SystemInit, SystemCoreClock)
```

### Warning flags
```
-Wall -Wextra -Werror  # all warnings enabled, all treated as errors
-Wshadow               # warn if a local variable shadows an outer one
-Wstrict-prototypes    # warn if function declared without parameter types
-Wmissing-prototypes   # warn if function defined without a prior prototype
```
**Zero warnings is mandatory. See conventions.md.**

### Language standard
```
-std=c99               # C99 standard (fixed-width integers, // comments, etc.)
```

### Optimization and debug
```
-g                     # include debug symbols (for GDB crash diagnosis)
-O0                    # no optimization (debug build — variables not optimized away)
```

### Code size reduction
```
-ffunction-sections    # each function in its own section
-fdata-sections        # each global variable in its own section
-fno-common            # don't merge uninitialized globals
```
Combined with `-Wl,--gc-sections` in the linker, this lets the linker
remove any function that is never called. Significantly reduces flash usage.

### Include paths
```
-I src                           # our code headers
-isystem lib/cmsis               # ARM CMSIS headers (suppress warnings)
-isystem lib/samd21-dfp          # Microchip DFP headers (suppress warnings)
-isystem startup                 # startup/system headers (suppress warnings)
```
**`-isystem` instead of `-I` for vendor directories.** This tells the compiler
to treat those headers as system headers, suppressing any warnings they produce.
The DFP headers use GCC extensions that trigger warnings under `-Werror`.
Our own code in `src/` uses `-I` so all warnings remain active.

### Linker flags
```
-T samd21g17d_flash.ld    # linker script: defines flash at 0x00000000 (128KB),
                          # RAM at 0x20000000 (16KB), stack at top of RAM
-Wl,--gc-sections         # remove unused sections (works with -ffunction-sections)
-Wl,-Map=build/[...].map  # produce a map file showing where everything is in memory
--specs=nano.specs         # use newlib-nano (smaller C library for embedded)
```

---

## The Register API (CRITICAL — Read This)

The DFP v3.6.144 uses a **different register access style** from many online
tutorials and from some of the documentation in this repo (which was written
before the actual DFP was examined).

### What many tutorials and our older docs show (ASF3 style — WRONG for us):
```c
PORT->Group[1].DIRSET.reg = (1u << 10);
PORT->Group[1].OUTSET.reg = (1u << 10);
SERCOM5->USART.CTRLA.reg = ...;
GCLK->CLKCTRL.reg = ...;
PM->APBCMASK.reg |= ...;
```

### What the actual DFP v3.6.144 uses (Harmony/CMSIS-pack style — CORRECT):
```c
PORT_REGS->GROUP[1].PORT_DIRSET = (1u << 10);
PORT_REGS->GROUP[1].PORT_OUTSET = (1u << 10);
SERCOM5_REGS->USART_INT.SERCOM_CTRLA = ...;
GCLK_REGS->GCLK_CLKCTRL = ...;
PM_REGS->PM_APBCMASK |= ...;
```

### The pattern:
| ASF3 style | DFP v3.6.144 style |
|---|---|
| `PERIPHERAL->` | `PERIPHERAL_REGS->` |
| `.field.reg` | `.PERIPHERAL_FIELD` |
| `Group[n]` | `GROUP[n]` |

**Always check the actual header file** in `lib/samd21-dfp/component/` before
writing register access code. The struct definitions are the authoritative
reference. For example, for PORT registers, read `lib/samd21-dfp/component/port.h`.

---

## Startup Sequence (What Happens Before main())

When the chip powers on or resets, this is the exact sequence:

```
1. Hardware reads address 0x00000000 → initial stack pointer value
   Hardware reads address 0x00000004 → address of Reset_Handler
   CPU jumps to Reset_Handler
   CPU is running at 1 MHz (OSC8M / 8, silicon default)

2. Reset_Handler (in startup/startup_samd21g17d.c):
   a. Copies .data section from flash to RAM (initialized globals)
   b. Zeros .bss section in RAM (uninitialized globals)
   c. Sets SCB->VTOR to the vector table address
   d. Calls _on_reset() — weak symbol, does nothing unless you define it
   e. Calls __libc_init_array() — initializes C library (requires newlib)
   f. Calls _on_bootstrap() — weak symbol, does nothing unless you define it
   g. Calls main()

3. main() — YOUR code starts here
   CPU is still at 1 MHz. Nobody has configured 48 MHz yet.
```

**IMPORTANT:** `Reset_Handler` does NOT call `SystemInit()`. This differs
from many other ARM MCU startup codes and from some SAMD21 tutorials online.
The DFP's `SystemInit()` is a stub that sets `SystemCoreClock = 1000000` and
returns — it does not configure the DFLL48M or any clocks.

To run at 48 MHz, you must write clock configuration code yourself and call
it from `main()`. See `docs/samd21_clocks.md` for the register sequence.

---

## Adding a New Source File

1. Create the `.c` file (e.g., `src/uart_driver.c`)
2. Create the `.h` file (e.g., `src/uart_driver.h`)
3. Add the `.c` file to SRCS in the Makefile:
   ```makefile
   SRCS += src/uart_driver.c
   ```
4. Run `make` — if you forget step 3, you get "undefined reference" linker errors

Only `.c` files go in SRCS. Header files are found automatically via the
`-I` and `-isystem` include paths.

---

## Common Errors and Fixes

### "unable to find CMSIS-DAP device"
Board is not plugged in, or the USB cable is charge-only (no data lines).

### "LIBUSB_ERROR_ACCESS"
Windows USB driver issue. Fix with Zadig (https://zadig.akeo.ie):
1. Open Zadig → Options → List All Devices
2. Select "CMSIS-DAP" or "nEDBG" (NOT the CDC serial port)
3. Replace driver with WinUSB
4. Retry `make flash`

### "NVM lock error" or "failed to erase sector 0"
Flash page 0 is write-protected (leftover from a previous bootloader). Fix:
```bash
openocd -f openocd.cfg -c "init; reset halt; at91samd bootloader 0; reset; exit"
```
Then retry `make flash`.

### "DAP_CONNECT failed"
Usually a Windows driver issue. Try the Zadig fix above.

### Linker error: "undefined reference to _sbrk / _write / _read"
`syscalls_min.c` is missing from SRCS in the Makefile. Add it:
```makefile
SRCS += syscalls_min.c
```

### Warning: "no previous prototype for 'SystemInit'"
`-DUSE_CMSIS_INIT` is missing from CFLAGS. It must be present:
```makefile
CFLAGS += -D__SAMD21G17D__ -DUSE_CMSIS_INIT
```

### LED does not blink after flash
- Check the SIZE output: if `text` is 0, the build produced an empty binary
- Check `** Verified OK **` appeared in the flash output
- If the LED blinks but at the wrong rate: the delay loop count needs adjusting
  for the actual CPU clock speed (1 MHz default, not 48 MHz)

---

## Vendor Files — Where They Come From

These files are downloaded from official sources and NEVER edited:

### Microchip SAMD21 DFP (Device Family Pack)
- **Source:** https://packs.download.microchip.com/Microchip.SAMD21_DFP.3.6.144.atpack
- **Format:** `.atpack` file (it is a ZIP archive)
- **Chip-specific subdirectory:** `samd21d/` (NOT `samd21a/` — the "D" suffix
  variants like SAMD21G17D have their own subdirectory)

Files extracted from the DFP:
```
samd21d/include/samd21g17d.h          → lib/samd21-dfp/
samd21d/include/sam.h                 → lib/samd21-dfp/
samd21d/include/system_samd21.h       → lib/samd21-dfp/ AND startup/
samd21d/include/component-version.h   → lib/samd21-dfp/
samd21d/include/component/*.h         → lib/samd21-dfp/component/
samd21d/include/instance/*.h          → lib/samd21-dfp/instance/
samd21d/include/pio/samd21g17d.h      → lib/samd21-dfp/pio/
samd21d/gcc/gcc/startup_samd21g17d.c  → startup/
samd21d/gcc/system_samd21g17d.c       → startup/
samd21d/gcc/gcc/samd21g17d_flash.ld   → project root
```

### ARM CMSIS 5 Core Headers
- **Source:** https://github.com/ARM-software/CMSIS_5 (branch: develop)
- **Path:** `CMSIS/Core/Include/`
- **Files needed:** `core_cm0plus.h`, `cmsis_gcc.h`, `cmsis_compiler.h`, `cmsis_version.h`
- **Destination:** `lib/cmsis/`

### Re-downloading vendor files
If vendor files are suspected to be corrupted, delete them and re-download:
```bash
# Delete existing vendor files in the project
rm -rf lib/samd21-dfp/* lib/cmsis/* startup/*

# Download fresh DFP
curl -L -o /tmp/samd21_dfp.atpack \
    "https://packs.download.microchip.com/Microchip.SAMD21_DFP.3.6.144.atpack"

# Extract (it's a ZIP)
unzip -o /tmp/samd21_dfp.atpack "samd21d/*" -d /tmp/dfp_extract

# Copy to project (see file mapping above)

# Download fresh CMSIS headers
curl -L -o lib/cmsis/core_cm0plus.h \
    "https://raw.githubusercontent.com/ARM-software/CMSIS_5/develop/CMSIS/Core/Include/core_cm0plus.h"
curl -L -o lib/cmsis/cmsis_gcc.h \
    "https://raw.githubusercontent.com/ARM-software/CMSIS_5/develop/CMSIS/Core/Include/cmsis_gcc.h"
curl -L -o lib/cmsis/cmsis_compiler.h \
    "https://raw.githubusercontent.com/ARM-software/CMSIS_5/develop/CMSIS/Core/Include/cmsis_compiler.h"
curl -L -o lib/cmsis/cmsis_version.h \
    "https://raw.githubusercontent.com/ARM-software/CMSIS_5/develop/CMSIS/Core/Include/cmsis_version.h"
```

---

## Quick Reference

```bash
# Build (compile + link + produce .bin)
make

# Build and flash to board
make flash

# Clean all build artifacts
make clean

# Build flight version (size-optimized, no debug logging)
make flight

# Test OpenOCD connection without flashing
openocd -f openocd.cfg -c "init; exit"

# Unlock flash if write-protected
openocd -f openocd.cfg -c "init; reset halt; at91samd bootloader 0; reset; exit"

# Connect GDB for crash diagnosis
# Terminal 1: openocd -f openocd.cfg
# Terminal 2: arm-none-eabi-gdb build/satellite_firmware.elf
#   (gdb) target remote localhost:3333
#   (gdb) monitor reset halt
#   (gdb) load
#   (gdb) continue
```

---

*This document was created on 2026-04-01 after the first successful build and
flash of the SAMD21G17D Curiosity Nano. All commands, paths, flag values, and
register API details have been verified to work on this specific hardware and
toolchain configuration.*
