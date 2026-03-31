# Project Structure and Build System

This document describes the file layout, where each file comes from,
what it does, and how the Makefile ties everything together.

---

## File Layout

```
satellite-firmware/
│
├── src/                        ← Your application code
│   ├── main.c                  ← Superloop, top-level state structs
│   ├── debug_log_dma.c/.h      ← DMA UART logging system
│   ├── uart_driver.c/.h        ← Mission UART (not the debug port)
│   ├── adc_driver.c/.h         ← ADC peripheral driver
│   ├── mppt_algorithm.c/.h     ← MPPT algorithm (no hardware dependencies)
│   ├── mission_state.c/.h      ← Satellite state machine
│   └── ...                     ← One .c/.h pair per module
│
├── lib/
│   ├── cmsis/                  ← ARM CMSIS headers (downloaded, never edited)
│   │   ├── core_cm0plus.h
│   │   ├── cmsis_gcc.h
│   │   └── cmsis_compiler.h
│   └── samd21-dfp/             ← Microchip device support files (never edited)
│       ├── samd21g17d.h        ← Top-level device header, include this one
│       ├── component/          ← One header per peripheral (sercom.h, dmac.h, ...)
│       ├── instance/           ← Peripheral base addresses
│       └── pio/
│           └── samd21g17d.h    ← Pin definitions
│
├── startup/                    ← From DFP, never edited
│   ├── startup_samd21.c        ← Vector table, Reset_Handler, weak ISR stubs
│   ├── system_samd21.c         ← SystemInit(): clocks, flash wait states
│   └── system_samd21.h
│
├── tests/                      ← Host-side tests (compile and run on laptop)
│   └── mppt_algorithm_test.c   ← Tests mppt_algorithm.c with no hardware
│
├── syscalls_min.c              ← C library stubs (write once, never touch again)
├── samd21g17d_flash.ld         ← Linker script from DFP
├── Makefile                    ← Build system
├── openocd.cfg                 ← OpenOCD connection config for DM320119
├── .gdbinit                    ← GDB auto-setup (optional, for crash diagnosis)
├── conventions.md              ← Mandatory coding standards
├── readme.md                   ← This project's entry point
├── plan.md                     ← Development phases
└── docs/                       ← Technical reference documents
```

---

## Where the DFP Files Come From

DFP = Device Family Pack. This is a Microchip-provided archive containing all
the chip-specific headers, startup code, and linker scripts for the SAMD21 family.

Download from Microchip's pack repository:
https://packs.download.microchip.com/

Look for "SAMD21" and download the latest `.atpack` file. An `.atpack` is a ZIP
archive. Extract it. The files you need are inside:

```
Microchip.SAMD21_DFP.x.x.xxx.atpack (this is a ZIP)
  └── samd21a/
      ├── include/
      │   ├── samd21g17d.h           → copy to lib/samd21-dfp/
      │   ├── component/             → copy to lib/samd21-dfp/component/
      │   ├── instance/              → copy to lib/samd21-dfp/instance/
      │   └── pio/                   → copy to lib/samd21-dfp/pio/
      └── gcc/
          ├── startup_samd21.c       → copy to startup/
          ├── system_samd21.c        → copy to startup/
          ├── system_samd21.h        → copy to startup/
          └── samd21g17d_flash.ld    → copy to project root
```

The CMSIS headers come from ARM:
https://github.com/ARM-software/CMSIS_5/tree/develop/CMSIS/Core/Include

You only need: `core_cm0plus.h`, `cmsis_gcc.h`, `cmsis_compiler.h`, `cmsis_version.h`.
Copy them to `lib/cmsis/`.

---

## The Makefile Explained

```makefile
# ── Tool definitions ──────────────────────────────────────────────────────────
CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

TARGET := satellite_firmware
BUILD  := build/

# ── Add every new .c file here ────────────────────────────────────────────────
SRCS :=
SRCS += src/main.c
SRCS += src/debug_log_dma.c
SRCS += startup/startup_samd21.c
SRCS += startup/system_samd21.c
SRCS += syscalls_min.c

OBJS := $(addprefix $(BUILD), $(addsuffix .o, $(basename $(notdir $(SRCS)))))

INC := -I lib/cmsis -I lib/samd21-dfp -I startup -I src

# ── CPU flags — must match SAMD21G17D exactly ─────────────────────────────────
CPU := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft

CFLAGS  := $(CPU) $(INC)
CFLAGS  += -D__SAMD21G17D__
CFLAGS  += -DDEBUG_LOGGING_ENABLED
CFLAGS  += -std=c99
CFLAGS  += -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS  += -ffunction-sections -fdata-sections -fno-common
CFLAGS  += -g -O0

LDFLAGS := $(CPU)
LDFLAGS += -T samd21g17d_flash.ld
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD)$(TARGET).map
LDFLAGS += --specs=nano.specs --specs=nosys.specs

all: $(BUILD)$(TARGET).bin
	@$(SIZE) $(BUILD)$(TARGET).elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)$(TARGET).elf: $(OBJS) | $(BUILD)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)$(TARGET).bin: $(BUILD)$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: startup/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

flash: all
	openocd -f openocd.cfg \
		-c "program $(BUILD)$(TARGET).bin verify reset exit 0x00000000"

clean:
	rm -rf $(BUILD)

# Flight build: no logging, optimise for size
flight: CFLAGS := $(filter-out -DDEBUG_LOGGING_ENABLED -O0, $(CFLAGS))
flight: CFLAGS += -Os
flight: all

.PHONY: all flash clean flight
```

Key flags explained:

`-mcpu=cortex-m0plus -mthumb` — tells the compiler the exact CPU. Wrong value here
produces code the chip cannot run.

`-mfloat-abi=soft` — the Cortex-M0+ has no hardware FPU. Soft float means the
compiler emulates floating point in software. If you use floats in the MPPT
simulation, this is fine. If you need high-frequency float operations, consider
fixed-point arithmetic instead.

`-ffunction-sections -fdata-sections` combined with `-Wl,--gc-sections` — each
function goes into its own section so the linker can remove unused functions.
Reduces flash usage significantly.

`--specs=nano.specs` — uses newlib-nano, a reduced C library for embedded systems.
Smaller than full newlib. No heap-dependent functions unless you call them explicitly.

`--specs=nosys.specs` — provides stub implementations of system calls (open, write,
etc.) that the C library needs but we do not have an OS to provide. If you want
to write your own stubs instead, use `syscalls_min.c` and omit this flag.

---

## Build Targets

```bash
make          # compile only, show size report
make flash    # compile + flash + verify + reset
make clean    # delete build/ directory
make flight   # compile with -Os and no logging (checks size before flight)
```

The `SIZE` output after every build:
```
   text    data     bss     dec     hex filename
   8432      24     512    8968    2308 build/satellite_firmware.elf
```

`text` = flash consumed. Must stay under 131072 (128KB).
`bss` = uninitialized globals in RAM. Must stay under 16384 (16KB) total with `data`.
`data` = initialized globals copied from flash to RAM at boot.

Run `make flight` periodically to see what the real flight binary size looks like
without debug overhead.

---

## syscalls_min.c

The C library (`newlib-nano`) references a set of system calls that an operating
system would provide: `_write`, `_read`, `_sbrk`, etc. On bare metal there is
no OS, so you provide stubs. The minimum required stubs:

```c
// syscalls_min.c
// Minimal C library system call stubs for bare-metal SAMD21.
// Write once. Never touch again.

#include <sys/stat.h>
#include <stdint.h>

// _sbrk: heap growth. We return -1 (error) because we never use malloc.
void *_sbrk(int incr) {
    (void)incr;
    return (void *)-1;
}

// _write: used by printf. We do not use printf — return 0.
int _write(int fd, char *ptr, int len) {
    (void)fd; (void)ptr; (void)len;
    return 0;
}

int _read(int fd, char *ptr, int len) {
    (void)fd; (void)ptr; (void)len;
    return 0;
}

int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; (void)st; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int ptr, int dir) { (void)fd; (void)ptr; (void)dir; return 0; }
```

---

## openocd.cfg

```
# openocd.cfg — connection config for Curiosity Nano DM320119
# Debugger: nEDBG (appears as CMSIS-DAP, VID 0x03eb PID 0x2175)
# Target:   SAMD21G17D

adapter driver cmsis-dap
transport select swd
source [find target/at91samdXX.cfg]
adapter speed 4000
```

The `at91samdXX.cfg` target file ships with OpenOCD and covers the entire SAMD21
family. The 4 MHz SWD clock is a safe default. The nEDBG supports up to 8 MHz,
but 4 MHz avoids issues with long cables or noisy environments.

---

## Things to Be Careful About

**Adding a new .c file.** You must add it to `SRCS` in the Makefile. If you
forget, the linker will not include it and your functions will be "undefined
reference" errors at link time.

**Header-only modules do not go in SRCS.** Only `.c` files go in SRCS.

**The linker script is chip-specific.** `samd21g17d_flash.ld` sets flash origin
to 0x00000000 and size to 128KB, RAM origin to 0x20000000 and size to 16KB. If
you use a different SAMD21 variant, use the correct linker script for it.

**-O0 vs -Os.** Debug builds use `-O0` (no optimization) so GDB shows real
variable values. Flight builds use `-Os` (size optimization). The behaviour
should be identical, but floating-point results can differ slightly due to
compiler reordering. Test the flight build too, not just the debug build.
