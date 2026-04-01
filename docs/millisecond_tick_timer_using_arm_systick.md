# Millisecond Tick Timer — ARM SysTick Driver

This document explains what the millisecond tick timer is, why we need it,
and how it works at the hardware level. No prior knowledge of ARM timers is
assumed.

---

## Why We Need This (The Problem It Solves)

### The situation before this driver

Before Phase 4, the main loop used a **blocking delay** to create timing:

```c
while (1) {
    process_uart_bytes();       /* takes ~1 microsecond */
    toggle_led();
    wait_500_milliseconds();    /* CPU STUCK HERE for 500ms doing nothing! */
}
```

The `wait_500_milliseconds()` function is a busy loop that counts to 4 million:
```c
volatile uint32_t count = 4000000;
while (count > 0) { count -= 1; }  /* wastes 500ms of CPU time */
```

This has two serious problems:

1. **The CPU is blocked for 500ms every loop iteration.** During that time, it
   cannot read UART bytes, process CHIPS frames, or do anything useful. If the
   OBC sends a command during the delay, the bytes pile up in the 256-byte RX
   buffer. If the buffer fills before the delay ends, bytes are lost.

2. **The timing is approximate.** If we add more code to the main loop later
   (sensor reads, MPPT algorithm), the delay changes because the total loop
   time changes. The "500ms" becomes 520ms or 480ms unpredictably.

### What we need instead

Phase 4 requires two things that need accurate, non-blocking timing:

1. **120-second OBC timeout** — If the OBC sends no valid CHIPS frame for
   120 seconds, the EPS enters autonomous mode. We need to measure 120 seconds
   of real elapsed time.

2. **500ms LED heartbeat** — The LED should toggle every 500ms, but without
   blocking the CPU.

### The solution: a hardware timer that counts milliseconds

We configure a hardware timer to fire an interrupt every 1 millisecond. Each
interrupt increments a counter. The main loop reads this counter to know
"how much time has passed?" — and it never needs to wait.

---

## This Is NOT the Clock Driver

The project has two drivers that involve clocks. They are completely different:

| | Clock driver | Millisecond tick timer |
|---|---|---|
| **File** | `clock_configure_48mhz_dfll_open_loop.c` | `millisecond_tick_timer_using_arm_systick.c` |
| **What it does** | Sets CPU speed to 48 MHz | Counts milliseconds since boot |
| **Analogy** | "How fast can this car go?" | "How far have we driven?" |
| **Hardware** | DFLL48M oscillator (SAMD21 peripheral) | SysTick timer (ARM core, built into the CPU itself) |
| **Runs at boot** | Yes — sets CPU speed, then done | Yes — starts the timer, then the interrupt runs forever |
| **Used by** | Everything (UART baud rate, timer periods) | OBC timeout (120s), heartbeat LED (500ms) |

The clock driver runs once at boot and never runs again. The SysTick timer
runs continuously in the background for the entire mission lifetime.

---

## What is SysTick?

SysTick is a **24-bit countdown timer** built into every ARM Cortex-M
processor. It is not a SAMD21-specific peripheral (like SERCOM, TCC, or ADC) —
it is part of the ARM core itself, defined by ARM in the Cortex-M0+ Technical
Reference Manual.

Every Cortex-M chip has it, from the tiniest M0 to the most powerful M7.
It's always there, always available, and requires no SAMD21-specific
configuration.

Key properties:
- **24-bit reload register** — maximum value is 16,777,215 (0xFFFFFF)
- **Counts down** at the CPU clock rate (48 MHz in our case)
- **Fires an interrupt** when it reaches zero
- **Automatically reloads** and starts counting again (no software intervention)
- **Configured by one CMSIS function call** — `SysTick_Config()` — no manual
  register writes needed

---

## How It Works — Step by Step

### Initialization (runs once at boot)

```c
SysTick_Config(48000);
```

This single CMSIS function call does three things:
1. Sets the SysTick RELOAD register to 47,999 (it counts 48,000 to 0 inclusive)
2. Sets SysTick interrupt priority to the lowest urgency
3. Enables the SysTick counter and its interrupt

After this call, the hardware runs autonomously. No further software
intervention is needed.

### The countdown (runs continuously in hardware)

```
Time 0.000 ms:  SysTick counter = 48000
                Counting down... one tick per CPU clock cycle
                48 MHz = 48,000 ticks per millisecond

Time 0.999 ms:  SysTick counter = 1
Time 1.000 ms:  SysTick counter = 0 → INTERRUPT FIRES!
                Counter automatically reloads to 48000
                Start counting again

Time 2.000 ms:  Counter hits 0 again → INTERRUPT FIRES!
                Reload, repeat...

This continues forever: one interrupt every 1.000 millisecond, exactly.
```

### The interrupt handler (runs every 1 ms, takes ~50 nanoseconds)

```c
static volatile uint32_t milliseconds_elapsed_since_boot = 0;

void SysTick_Handler(void)
{
    milliseconds_elapsed_since_boot += 1;
}
```

That's the entire handler — one line of real work. It increments a counter and
returns. The `volatile` keyword ensures the compiler doesn't optimize away
reads of this variable from the main loop (since the interrupt modifies it
"behind the compiler's back").

### Reading the time (called from the main loop)

```c
uint32_t now = millisecond_tick_timer_get_milliseconds_since_boot();
```

This simply returns the counter value. The main loop uses it for timing
decisions:

```c
/* Toggle LED every 500ms — without blocking */
uint32_t now = millisecond_tick_timer_get_milliseconds_since_boot();
if ((now - last_toggle_time) >= 500) {
    toggle_led();
    last_toggle_time = now;
}

/* Check 120-second OBC timeout — without blocking */
if ((now - last_obc_message_time) >= 120000) {
    enter_autonomous_mode();
}
```

The main loop runs thousands of times per second. Each iteration checks the
time, does its work (parse UART, handle commands), and moves on. Nothing ever
blocks.

---

## Why SysTick_Handler Overrides the Default

The startup file (`startup_samd21g17d.c`, line 64) declares:

```c
void SysTick_Handler(void) __attribute__((weak, alias("Dummy_Handler")));
```

The `weak` attribute means: "use this definition UNLESS someone else provides
one." Our `SysTick_Handler` in the timer driver provides a non-weak definition,
so the linker uses ours instead of the dummy. This is standard ARM practice —
all interrupt handlers in the startup file are weak aliases that get replaced
by real handlers when you write them.

---

## Counter Wrap-Around

The counter is a `uint32_t` (32-bit unsigned integer). It wraps after:

```
2^32 milliseconds = 4,294,967,296 ms ≈ 49.7 days
```

After ~50 days of continuous operation, the counter overflows from 0xFFFFFFFF
back to 0x00000000. This must NOT break our elapsed time calculations.

### The fix: unsigned subtraction

Always compute elapsed time as `now - previous` using unsigned arithmetic:

```c
uint32_t elapsed = now - previous;
if (elapsed >= 120000) { /* 120 seconds have passed */ }
```

This works correctly even when `now` has wrapped around and is numerically
smaller than `previous`. Example:

```
previous = 0xFFFFFFF0  (counter just before wrap)
now      = 0x00000010  (counter just after wrap)
now - previous = 0x00000010 - 0xFFFFFFF0 = 0x00000020 = 32
```

Result: 32 milliseconds elapsed. Correct! C's unsigned subtraction wraps around
automatically. This is safe as long as the interval being measured is less than
~24.8 days (half the wrap period), which is true for all our uses (120 seconds
is the longest interval we check).

---

## Interrupt Priority and Coexistence

The SAMD21 has several active interrupts:

| Interrupt | Source | How often | How long |
|---|---|---|---|
| SysTick_Handler | ARM core SysTick | Every 1 ms | ~50 ns |
| SERCOM0_Handler | OBC UART byte TX/RX | Per byte (~86 µs apart) | ~600 ns |
| DMAC_Handler | Debug log DMA transfer | Per DMA block | ~500 ns |
| EIC_Handler | Button press on PB11 | Rare (human input) | ~200 ns |

On the Cortex-M0+, when one interrupt is running, other interrupts are
**pended** (queued) until the current handler returns. Since every handler runs
for less than 1 microsecond, the maximum delay any interrupt experiences is
~1 µs. This is negligible — at 115200 baud, one UART byte takes 86.8 µs, so
a 1 µs jitter is invisible.

`SysTick_Config()` sets SysTick to the **lowest** priority. This means SERCOM0
and DMAC interrupts preempt SysTick if they fire simultaneously. The SysTick
counter might occasionally be off by 1 ms, which is irrelevant for a 120-second
timeout or a 500 ms heartbeat.

---

## API Reference

```c
/* Configure SysTick to fire every 1 ms at 48 MHz CPU clock.
 * Must be called AFTER configure_cpu_clock_to_48mhz_using_dfll_open_loop().
 * If called before the clock is configured, the tick rate will be wrong
 * because SysTick counts CPU clock cycles (default 1 MHz = 48x too slow). */
void millisecond_tick_timer_initialize_at_48mhz(void);

/* Return the number of milliseconds since boot.
 * Read this into a local uint32_t variable for use in comparisons,
 * because the volatile counter can change between reads:
 *
 *   WRONG:  if (get_ms() - get_ms() >= 500)   ← two reads, value can change
 *   RIGHT:  uint32_t now = get_ms();
 *           if (now - previous >= 500)          ← one read, stable */
uint32_t millisecond_tick_timer_get_milliseconds_since_boot(void);
```

---

## Source Files

| File | What it contains |
|---|---|
| `src/drivers/millisecond_tick_timer_using_arm_systick.h` | Public API: two function declarations |
| `src/drivers/millisecond_tick_timer_using_arm_systick.c` | Implementation: SysTick_Handler ISR (1 line), init function (calls SysTick_Config), getter function (returns counter) |
