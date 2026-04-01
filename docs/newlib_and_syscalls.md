# Newlib, System Call Stubs, and Prototype Fixes

This document explains why `syscalls_min.c` exists, why `-DUSE_CMSIS_INIT`
is needed in the Makefile, and how we resolved the prototype warnings during
the first build of Phase 0.

---

## The Problem: The C Library Expects an Operating System

The ARM toolchain (`arm-none-eabi-gcc`) ships with a C library implementation
called **newlib**. We use its smaller variant, **newlib-nano**, selected by
`--specs=nano.specs` in the Makefile linker flags.

Newlib provides standard C functions like `printf`, `malloc`, `memcpy`, and
the type definitions in `<stdint.h>`. It was designed to be portable across
many platforms — Linux, Windows, bare metal, RTOS — so it splits itself into
two layers:

```
Layer 1: Standard C functions (printf, malloc, memcpy, fopen...)
    Written by the newlib authors. Platform-independent.
    When these need to interact with the outside world (write to a file,
    allocate memory from the OS), they call down to:

Layer 2: System calls (_write, _read, _sbrk, _close, _fstat, _isatty, _lseek)
    NOT written by newlib. Newlib expects the user to provide them.
    On Linux, these would call the Linux kernel.
    On Windows, these would call the Windows kernel.
    On bare metal, there is no kernel — so we provide empty stubs.
```

We never call `printf` or `malloc` in our code. But newlib gets pulled into
the build anyway, and it demands these system call functions exist.

---

## Why Newlib Gets Pulled In

The Microchip DFP startup code (`startup/startup_samd21g17d.c`) calls this
function inside `Reset_Handler`, before `main()` runs:

```c
__libc_init_array();
```

`__libc_init_array()` is a newlib function that initializes C++ static
constructors and C library internals. We don't use C++ and we don't use
most of the C library, but this call is in the vendor startup code which
we do not edit.

Microchip includes this call because their startup code is designed to work
with multiple toolchains and frameworks:

- MPLAB Harmony (their HAL framework — uses newlib heavily)
- Arduino cores (uses printf, C++ String class, constructors)
- FreeRTOS projects (full RTOS, needs a C library)
- Bare-metal C projects like ours (needs none of it)

They ship one startup file for all cases. The `__libc_init_array()` call
is harmless for us (we have no constructors to initialize), but it creates
a link-time dependency on newlib, which in turn demands the system call stubs.

The dependency chain:

```
startup_samd21g17d.c (vendor code, never edited)
  → calls __libc_init_array()
    → lives in newlib-nano (pulled in by --specs=nano.specs)
      → newlib internally references _write, _sbrk, _read, _close...
        → linker says: "who provides these functions?"
          → if nobody provides them: linker error, build fails
          → syscalls_min.c provides them: build succeeds
```

---

## What syscalls_min.c Does

It provides seven functions that newlib expects the OS to provide. Since
there is no OS, every function does nothing and returns a safe default:

| Function | What newlib uses it for | Our stub returns |
|---|---|---|
| `_sbrk` | Grow the heap for `malloc` | `-1` (error — we never use malloc) |
| `_write` | Write bytes to a file descriptor | `0` (nothing written) |
| `_read` | Read bytes from a file descriptor | `0` (nothing read) |
| `_close` | Close a file descriptor | `-1` (error) |
| `_fstat` | Get file status | `0` (no-op) |
| `_isatty` | Check if fd is a terminal | `1` (yes, pretend it is) |
| `_lseek` | Seek within a file | `0` (no-op) |

No code in our project calls these functions directly. They exist purely
to satisfy the linker. The total cost is negligible — a few hundred bytes
of flash for functions that never execute.

---

## Alternative: --specs=nosys.specs

Before using `syscalls_min.c`, the Makefile used `--specs=nosys.specs` in
the linker flags. This tells the linker to use a pre-built library of empty
stubs that ships with the ARM toolchain. It achieves the same result but
produces linker warnings:

```
warning: _close is not implemented and will always fail
warning: _write is not implemented and will always fail
warning: _read is not implemented and will always fail
warning: _lseek is not implemented and will always fail
```

These warnings are cosmetically annoying and could mask real warnings. Using
our own `syscalls_min.c` eliminates them. It also gives us the option to
make `_write` do something useful later — for example, redirecting `printf`
output through the UART for debugging.

The two approaches are mutually exclusive. Using both causes duplicate symbol
errors because both provide the same functions. The Makefile uses
`syscalls_min.c` in SRCS and does NOT use `--specs=nosys.specs`.

---

## The Prototype Warnings

C requires that a function be **declared** (prototyped) before it is
**defined**. A prototype tells the compiler: "this function exists, here
are its parameter types and return type." The compiler uses this to catch
type mismatches when other files call the function.

```c
/* Prototype (declaration) — tells the compiler what to expect */
void SystemInit(void);

/* Definition — the actual code */
void SystemInit(void) {
    SystemCoreClock = 1000000;
}
```

Our Makefile uses `-Wmissing-prototypes` (warn if a function definition
appears without a prior prototype) combined with `-Werror` (treat all
warnings as fatal errors). This is a safety requirement from conventions.md.

Two files triggered this warning during the first build:

### Fix 1: system_samd21g17d.c — vendor file

The vendor file `system_samd21g17d.c` defines `SystemInit()` and
`SystemCoreClockUpdate()`. The prototypes for both exist in `system_samd21.h`.
But `system_samd21g17d.c` does not include `system_samd21.h` directly.
Instead, it includes `samd21g17d.h`, which includes `system_samd21.h`
only if a preprocessor symbol is defined:

```c
/* Inside samd21g17d.h, lines 228-230: */
#if defined USE_CMSIS_INIT
#include "system_samd21.h"    /* ← prototypes for SystemInit etc. */
#endif
```

Without `-DUSE_CMSIS_INIT`, the prototypes are never seen, and the compiler
reports missing prototypes for a vendor file we cannot edit.

**Fix:** Added `-DUSE_CMSIS_INIT` to CFLAGS in the Makefile. This defines
the symbol, the conditional include activates, the prototypes appear before
the definitions, and the compiler is satisfied.

### Fix 2: syscalls_min.c — our file

The seven newlib stub functions (`_sbrk`, `_write`, etc.) have no standard
header file that declares them. They are an informal contract between newlib
and the platform — newlib's documentation says "you provide these functions"
but doesn't provide a header for them.

**Fix:** Added explicit prototypes at the top of `syscalls_min.c`:

```c
void *_sbrk(int incr);
int   _write(int fd, char *ptr, int len);
int   _read(int fd, char *ptr, int len);
int   _close(int fd);
int   _fstat(int fd, struct stat *st);
int   _isatty(int fd);
int   _lseek(int fd, int ptr, int dir);
```

This is safe because we own the file and the prototypes exactly match the
definitions below them.

---

## Summary of Makefile Changes

| Change | Why |
|---|---|
| `-DUSE_CMSIS_INIT` added to CFLAGS | Makes `samd21g17d.h` include `system_samd21.h`, providing prototypes for `SystemInit` and `SystemCoreClockUpdate` |
| `syscalls_min.c` added to SRCS | Provides empty system call stubs for newlib |
| `--specs=nosys.specs` removed from LDFLAGS | Replaced by `syscalls_min.c` to avoid duplicate symbols and linker warnings |
| `-isystem` used for vendor include paths | Suppresses warnings from DFP/CMSIS headers (which use GCC extensions) while keeping strict warnings for our own code |

---

## Could We Avoid Newlib Entirely?

Yes, by writing a custom startup file that does not call `__libc_init_array()`.
This would eliminate the newlib dependency and the need for `syscalls_min.c`.
However, it would mean writing and maintaining our own vector table,
`.data` copy, and `.bss` zeroing code — the most critical and error-prone
part of any embedded system. Getting any of this wrong causes silent memory
corruption with no error message.

The project rule is: **never write startup code yourself**. The cost of
carrying `syscalls_min.c` (7 trivial functions, ~200 bytes of flash) is
far lower than the risk of a hand-written startup file.

---

*Sources:*
- *newlib documentation: https://sourceware.org/newlib/*
- *ARM GNU Toolchain specs files: shipped with arm-none-eabi-gcc 12.2.1*
- *Microchip DFP v3.6.144: startup_samd21g17d.c, system_samd21g17d.c,*
  *samd21g17d.h (lines 228-230 conditional include)*
