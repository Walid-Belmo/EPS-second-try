# Build Targets and File Map

This document is the **canonical reference** for the question:

> "Which file in this repo is used by which board's firmware build, and why?"

Read this first if you are about to add or move a file, or if you are
confused about whether a piece of code applies to your board.

---

## The two boards this repo builds firmware for

| Build | Board (physical) | Chip on the board |
|---|---|---|
| `devboard` | Microchip Curiosity Nano DM320119 | ATSAMD21G17D in 48-pin TQFP |
| `mainboard` | CHESS EPS PCU testing board V4.1 | ATSAMD21J17D-MUT in 64-pin QFN |

Both chips are the **same SAMD21 silicon die** in different packages. Same
memory map, same peripherals, same registers, same errata. The difference
is purely (a) how many of the silicon's pads are bonded to external pins
and (b) what those pins are wired to on the board around the chip.
Verified by direct schematic and DFP-file diff in
[`research_logs/agent_A_samd21_g17d_vs_j17d_silicon.md`](../research_logs/agent_A_samd21_g17d_vs_j17d_silicon.md)
and
[`research_logs/agent_B_dfp_files_g17d_vs_j17d_diff.md`](../research_logs/agent_B_dfp_files_g17d_vs_j17d_diff.md).

For the proof-backed pin map of the mainboard, see
[`mainboard_pinout_pcu_v4_1.md`](mainboard_pinout_pcu_v4_1.md).

---

## How to invoke a build

`BOARD` must be set explicitly on every invocation. There is no default
(a default caused real confusion in early bring-up and was removed):

```bash
make BOARD=devboard           # build for Curiosity Nano dev board
make BOARD=devboard flash     # build then flash the dev board via on-board nEDBG
make BOARD=mainboard          # build for EPS PCU testing board V4.1
make BOARD=mainboard flash    # build then flash the mainboard via Atmel-ICE
make clean                    # wipes build/ — does NOT need BOARD
```

If you forget `BOARD=`, `make` will abort with a message telling you what to do.

**Always run `make clean` when switching `BOARD`.** Object files go into a
single `build/` directory; an object file compiled for one chip would link
silently into a binary for the other chip and produce a broken result.

---

## The three categories every file falls into

Every file in this repo belongs to **exactly one** of these:

1. **`devboard only`** — compiled into `BOARD=devboard` builds; not used by
   the mainboard. Almost always because the file references hardware that
   only exists in the dev-board context (a specific pin used by the
   Curiosity Nano's nEDBG, etc.).
2. **`mainboard only`** — compiled into `BOARD=mainboard` builds; not used
   by the dev board. Same logic in reverse — references hardware that
   only exists on the EPS PCB.
3. **`shared`** — compiled into both builds; identical source. The two
   chips share enough silicon that the same C code with the same compiler
   flags produces a working object file for both targets.

---

## Three reinforcing layers tell you which category a file is in

If they ever disagree, **the Makefile wins** (it's what actually compiles).

### Layer 1 — The Makefile (executable source of truth)

`Makefile` has an `ifeq ($(BOARD),devboard) ... else ifeq ($(BOARD),mainboard)`
block. The `APP_SRCS` and `STARTUP_SRCS` and `LINKER_SCRIPT` variables in
each branch list exactly which files are compiled for that board. A file
listed under both branches is shared. A file listed under neither is dead
code (or vendor support code that gets pulled in implicitly via `#include`).

### Layer 2 — File names

The project's naming convention (see [`notes/conventions.md`](../notes/conventions.md))
requires file names to be sentences. By convention:

- A file ending in `_on_mainboard.c/.h` is **mainboard only**.
- A file whose name contains a specific dev-board pin (e.g. `_pa04_pa05`
  for the dev board's edge pads, `_pa22` for the dev board's debug UART
  pin) is **devboard only**.
- A file with no board-specific suffix (e.g. `clock_configure_48mhz_dfll_open_loop.c`)
  is **shared**.

### Layer 3 — The `BUILD TARGET:` line in every file's header comment

Every project `.c` and `.h` file contains a comment near the top like:

```
BUILD TARGET: devboard only
```

or

```
BUILD TARGET: mainboard only (PB22 is not wired to an LED on the dev board)
```

or

```
BUILD TARGET: shared (compiled into both devboard AND mainboard builds)
```

This is the fastest way to confirm classification when you open a file.

---

## Complete file inventory

### Application code (`src/`)

| File | Build | Notes |
|---|---|---|
| `src/main.c` | devboard only | Semester demo application. Uses PB10 user LED, PA22 debug UART, and the CHIPS OBC UART on PA10/PA11 through `uart_obc_sercom0_pa10_pa11_on_devboard.c`. |
| `src/eps_demo_chips_command_dispatch.c/.h` | shared | Demo CHIPS command handlers for injected sensor values, state/debug snapshots, fixed duty command, mode command, explicit PWM arm/disarm gating, periodic telemetry streaming, and the mainboard raw-ADC read command. |
| `src/main_mainboard_chips_injection_demo.c` | mainboard only | Real-board CHIPS injection demo. Uses PB22 status LED and the real board J3 UART on PA10/PA11. |
| `src/main_mainboard_blink_pb22.c` | mainboard only reference | Firmware A. Smallest possible "is the chip alive?" test. Kept for recovery/bring-up reference but not compiled by the current `BOARD=mainboard` target. |

### Drivers (`src/drivers/`)

| File | Build | Notes |
|---|---|---|
| `src/drivers/clock_configure_48mhz_dfll_open_loop.c/.h` | shared | DFLL48M is identical between G17D and J17D. Same registers, same calibration NVM addresses, same errata 1.2.1 workaround. |
| `src/drivers/debug_functions.c/.h` | devboard only | DMA UART TX on PA22 → nEDBG → COM6. PA22 is I²C SDA on the mainboard, so this driver would conflict there. |
| `src/drivers/uart_obc_sercom0_pa04_pa05.c/.h` | devboard only | Older dev-board UART bring-up driver on PA04/PA05. Kept for reference, not compiled into the current dev-board demo build. |
| `src/drivers/uart_obc_sercom0_pa10_pa11_on_devboard.c/.h` | devboard only | Current demo OBC UART driver. Uses SERCOM0 PA10/PA11 so the dev-board communication path matches the real board's UART pin pair. |
| `src/drivers/uart_obc_sercom0_pa10_pa11_on_mainboard.c/.h` | mainboard only | Current real-board OBC UART driver. Uses J3 UART nets on PA10/PA11. |
| `src/drivers/mainboard_adc_reader.c/.h` | mainboard only | Read-only ADC reader for `PV_IMON`, `BAT_IMON`, `OUTA1`, `OUTA2`, `OUTV1`, and `OUTV2` on PB04..PB09. |
| `src/drivers/pwm_buck_converter_tcc0_pa12_pa13_on_mainboard.c` | mainboard only | Real TCC0 PWM driver for `PWM_H`/`PWM_L` on PA12/PA13. Uses the documented two-slice DTI workaround with `SWAP2`; pins are forced low at boot, when duty is zero, and when the CHIPS command layer is disarmed. |
| `src/drivers/uart_obc.h` | shared API | Generic OBC UART API used by application code; the Makefile selects the board-specific implementation. |
| `src/drivers/chips_protocol_encode_decode_frames_with_crc16_kermit.c/.h` | shared logic, devboard build today | Pure CHIPS frame builder/parser and CRC-16/KERMIT logic. |
| `src/drivers/millisecond_tick_timer_using_arm_systick.c/.h` | shared logic, devboard build today | SysTick-backed millisecond counter for non-blocking timing and telemetry periods. |
| `src/drivers/led_status_pb22_active_high_on_mainboard.c/.h` | mainboard only | LED2 (green, active-HIGH) on PB22 of the EPS PCU board. PB22 has no LED on the dev board. |

### Top-level C files

| File | Build | Notes |
|---|---|---|
| `syscalls_min.c` | shared | Minimal newlib system-call stubs. Pure C library plumbing, zero chip-specific code. |

### Startup files (`startup/`)

These are vendor files from the Microchip SAMD21 DFP v3.6.144. **Never edit them.**

| File | Build | Notes |
|---|---|---|
| `startup/startup_samd21g17d.c` | devboard only | G17D vector table + Reset_Handler. Selected by the `devboard` Makefile branch. |
| `startup/system_samd21g17d.c` | devboard only | G17D `SystemInit()` (a stub) + `SystemCoreClock` global. |
| `startup/startup_samd21j17d.c` | mainboard only | J17D vector table. Same as G17D except slots 21/22 are populated with TC6/TC7 handlers (timers that exist on the J package but not the G). |
| `startup/system_samd21j17d.c` | mainboard only | J17D `SystemInit()` + `SystemCoreClock`. Effectively identical to the G17D version; differs only in chip-name comment. |
| `startup/system_samd21.h` | shared (passive) | Vendor header that just declares `SystemInit()` and `SystemCoreClock`. Same for both chips. |

### Linker scripts (project root)

| File | Build | Notes |
|---|---|---|
| `samd21g17d_flash.ld` | devboard only | G17D memory map: 128 KB flash at `0x00000000`, 16 KB RAM at `0x20000000`. |
| `samd21j17d_flash.ld` | mainboard only | J17D memory map. Identical to the G17D version except for one comment line — both chips have the same memory layout. |

### Vendor headers (`lib/samd21-dfp/`, `lib/cmsis/`)

These are vendor files. **Never edit them.** They live on the include path and
are pulled in by `#include` from the source files that need them. The chip-name
macro (`-D__SAMD21G17D__` or `-D__SAMD21J17D__`) controls which chip-specific
header gets selected by the top-level `sam.h`.

| Path | Build | Notes |
|---|---|---|
| `lib/samd21-dfp/sam.h` | shared | Top-level chooser. Has `#if defined(__SAMD21G17D__) → samd21g17d.h; #elif defined(__SAMD21J17D__) → samd21j17d.h`. |
| `lib/samd21-dfp/samd21g17d.h` | devboard only (selected by `__SAMD21G17D__`) | G17D's chip header: peripheral instance pointers, IRQ enums, memory-map macros. |
| `lib/samd21-dfp/samd21j17d.h` | mainboard only (selected by `__SAMD21J17D__`) | J17D's chip header. Identical to G17D's except for two extra IRQ entries (TC6, TC7) and the chip-identification register value. |
| `lib/samd21-dfp/pio/samd21g17d.h` | devboard only | Per-pin alternate-function macros for the G17D's bonded pins. |
| `lib/samd21-dfp/pio/samd21j17d.h` | mainboard only | Per-pin alternate-function macros for the J17D's bonded pins. Strict superset of the G17D version. |
| `lib/samd21-dfp/component/*.h` | shared | Peripheral register struct typedefs (SERCOM, TCC, PORT, GCLK, NVMCTRL, DMAC, etc.). Byte-identical between the two variants. |
| `lib/samd21-dfp/instance/*.h` | shared | Peripheral instance base addresses. Byte-identical between the two variants. |
| `lib/samd21-dfp/system_samd21.h` | shared | Vendor header that declares `SystemInit()` and `SystemCoreClock`. |
| `lib/samd21-dfp/component-version.h` | shared | DFP version stamp (3.6.144 here). |
| `lib/cmsis/*.h` | shared | ARM CMSIS-Core headers for Cortex-M0+. Fully chip-agnostic. |

### Build configuration

| File | Build | Notes |
|---|---|---|
| `Makefile` | (control file) | Selects which sources, defines, startup, and linker script go into each build via the `BOARD` variable. **The executable source of truth.** |
| `openocd.cfg` | shared | OpenOCD adapter + transport config. Same for both boards because OpenOCD auto-detects the SAMD21 variant via the chip's DSU.DID register. The Atmel-ICE works as a CMSIS-DAP probe and needs no `openocd.cfg` change. |

### Documentation (`docs/`, `notes/`)

Not compiled into any binary. Treat as `shared` for reading purposes — but
some docs are written from the perspective of one specific board. The
filename usually says which.

---

## What to do when adding a new file

1. Decide which category it belongs to: `devboard only`, `mainboard only`,
   or `shared`.
2. Choose a filename that reflects this. If it's mainboard-specific, end
   the filename with `_on_mainboard.c/.h`. If it uses specific pins, name
   the pins.
3. Add the `BUILD TARGET:` line to the file's header comment.
4. Add the file to the right place in the Makefile:
   - For `devboard only`: append to `APP_SRCS` inside the
     `ifeq ($(BOARD),devboard)` branch.
   - For `mainboard only`: append to `APP_SRCS` inside the
     `else ifeq ($(BOARD),mainboard)` branch.
   - For `shared`: append to `APP_SRCS` in **both** branches (or factor
     out a common variable).
5. Add a row to the inventory table in this document.
6. If the file is a new driver, also add a brief description to the
   "Driver Modules" section of [`notes/readme.md`](../notes/readme.md).

---

## Worktrees

The repo also has separate **git worktrees** for in-progress phases (Phase
4 CHIPS protocol, Phase 5 PWM, Phase 6 sensors paused, MPPT algorithm).
Those worktrees are not currently merged into `master` and have their own
file layouts. When code from a worktree is ready to merge, its files will
be classified using the same three categories above and added to this
document.

See [`notes/howtomerge.md`](../notes/howtomerge.md) for the worktree
merge procedure.

---

*Last updated: 2026-04-26.*
