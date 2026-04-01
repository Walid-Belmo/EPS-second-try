# Satellite Firmware Coding Conventions

This document defines the coding standard for all satellite firmware written in C.
It is a binding reference. Every file, every function, every variable must comply.

Three authoritative sources applied together:
- **NASA Power of 10** — Gerard Holzmann, JPL Laboratory for Reliable Software, 2006
- **JPL Institutional Coding Standard for C** — DOCID D-60411, March 2009
- **MISRA C:2004** — the embedded industry safety standard referenced by JPL D-60411

The guiding principle: **code must read like English sentences.**

---

## SECTION A: READABILITY AND NAMING

### Rule A1: Names Must Be Sentences, Not Abbreviations

Every variable, function, struct field, and **file name** must be an unambiguous
English description of what it is or what it does. There is no length limit.
Long names are correct. Short cryptic names are wrong.

This applies to `.c` and `.h` file names as well. A file name describes the
module's responsibility. Someone reading the project file listing should
understand what each file does without opening it.

**Wrong file names**
```
clk.c, dma_log.c, uart.c, sys.c
```

**Correct file names**
```
clock_configure_48mhz_dfll_open_loop.c
debug_log_dma_uart_sercom5.c
uart_initialize_sercom5_on_pb22.c
```

The name must answer "what is this?" without any surrounding context.

**Wrong**
```c
uint16_t adc_to_mv(uint16_t raw, uint16_t vref, uint8_t res);
int n_bytes;
uint8_t buf[256];
void uart_cfg(void);
```

**Correct**
```c
uint16_t convert_raw_adc_reading_to_millivolts(
    uint16_t raw_adc_reading_from_hardware,
    uint16_t reference_voltage_in_millivolts,
    uint8_t  adc_resolution_in_bits);

int     number_of_bytes_currently_in_receive_buffer;
uint8_t buffer_for_storing_uart_received_bytes[256];
void    uart_initialize_sercom5_at_115200_baud(void);
```

---

### Rule A2: Functions Have One Job. The Name Describes That One Job.

A function does exactly one thing. Its name is a verb phrase describing that thing.
If you cannot describe what a function does in one sentence, split it.

The public entry point of a module reads like a table of contents.
Every private helper is `static` and does exactly one sub-task.

**Wrong** — one function doing everything, unreadable name:
```c
void setup(void) {
    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM5;
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 |
                        GCLK_CLKCTRL_ID(SERCOM5_GCLK_ID_CORE);
    while (GCLK->STATUS.bit.SYNCBUSY);
    SERCOM5->USART.CTRLA.reg = SERCOM_USART_CTRLA_SWRST;
    while (SERCOM5->USART.SYNCBUSY.bit.SWRST);
    SERCOM5->USART.CTRLA.reg = SERCOM_USART_CTRLA_DORD |
        SERCOM_USART_CTRLA_MODE_USART_INT_CLK | SERCOM_USART_CTRLA_TXPO(1);
    SERCOM5->USART.CTRLB.reg = SERCOM_USART_CTRLB_TXEN;
    while (SERCOM5->USART.SYNCBUSY.bit.CTRLB);
    SERCOM5->USART.BAUD.reg = 63019;
    SERCOM5->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
    while (SERCOM5->USART.SYNCBUSY.bit.ENABLE);
}
```

**Correct** — public entry point is a readable sequence; every step is named:
```c
void uart_initialize_sercom5_at_115200_baud(void) {
    enable_sercom5_bus_clock_so_cpu_can_write_its_registers();
    connect_48mhz_clock_to_sercom5_so_it_can_operate();
    configure_pb22_as_uart_transmit_pin_not_gpio();
    reset_sercom5_to_known_clean_state();
    configure_sercom5_as_uart_with_internal_clock();
    set_sercom5_baud_rate_to_115200_assuming_48mhz_clock();
    enable_sercom5_uart_hardware();
}

static void enable_sercom5_bus_clock_so_cpu_can_write_its_registers(void) {
    // By default SERCOM5 is unpowered on the APB bus.
    // Writing to its registers does nothing until this bit is set.
    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM5;
}

static void connect_48mhz_clock_to_sercom5_so_it_can_operate(void) {
    // The bus clock lets the CPU access registers. SERCOM5 also needs a
    // functional clock to actually shift bits and generate baud timing.
    // This connects GCLK0 (48 MHz) to SERCOM5's functional clock channel.
    GCLK->CLKCTRL.reg =
        GCLK_CLKCTRL_CLKEN                     |   // enable this connection
        GCLK_CLKCTRL_GEN_GCLK0                 |   // source: GCLK0 = 48 MHz
        GCLK_CLKCTRL_ID(SERCOM5_GCLK_ID_CORE);     // destination: SERCOM5
    // This write crosses a clock domain. Writing SERCOM5 registers before
    // SYNCBUSY clears produces undefined behaviour.
    while (GCLK->STATUS.bit.SYNCBUSY);
}

static void configure_pb22_as_uart_transmit_pin_not_gpio(void) {
    // PB22 defaults to GPIO. PMUXEN=1 routes it to the peripheral mux.
    // Mux D selects SERCOM-ALT, which is SERCOM5 PAD[2] on PB22.
    PORT->Group[1].PINCFG[22].reg  |= PORT_PINCFG_PMUXEN;
    PORT->Group[1].PMUX[22/2].bit.PMUXE = PORT_PMUX_PMUXE_D_Val;
}

static void reset_sercom5_to_known_clean_state(void) {
    // Always reset before configuring. Clears any state left by a previous
    // boot, watchdog reset, or the debugger leaving registers in a dirty state.
    SERCOM5->USART.CTRLA.reg = SERCOM_USART_CTRLA_SWRST;
    // Reset propagates across clock domains. Writes before this clears
    // are silently discarded.
    while (SERCOM5->USART.SYNCBUSY.bit.SWRST);
}

static void configure_sercom5_as_uart_with_internal_clock(void) {
    SERCOM5->USART.CTRLA.reg =
        SERCOM_USART_CTRLA_DORD               |    // LSB first — standard UART
        SERCOM_USART_CTRLA_MODE_USART_INT_CLK |    // baud clock is internal
        SERCOM_USART_CTRLA_TXPO(1);                // TX on pad group 1 = PB22

    SERCOM5->USART.CTRLB.reg = SERCOM_USART_CTRLB_TXEN;  // TX only, no RX needed
    // CTRLB crosses a clock domain — must wait before proceeding.
    while (SERCOM5->USART.SYNCBUSY.bit.CTRLB);
}

static void set_sercom5_baud_rate_to_115200_assuming_48mhz_clock(void) {
    // BAUD = 65536 * (1 - 16 * (115200 / 48000000)) = 63019
    // Actual timing error: < 0.01%. UART tolerates up to ~3%.
    SERCOM5->USART.BAUD.reg = 63019;
}

static void enable_sercom5_uart_hardware(void) {
    // Before this bit, the UART does not transmit anything even though
    // all configuration registers are written correctly.
    SERCOM5->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
    // Do not use the UART until this clears.
    while (SERCOM5->USART.SYNCBUSY.bit.ENABLE);
}
```

---

### Rule A3: main() Must Read Like a Table of Contents

`main()` contains no register names, no hardware constants, no raw addresses.
It calls named functions only.

**Wrong**
```c
int main(void) {
    NVMCTRL->CTRLB.bit.RWS = 1;
    PM->APBAMASK.reg |= PM_APBAMASK_GCLK;
    while (1) {
        ADC->SWTRIG.reg = ADC_SWTRIG_START;
        WDT->CLEAR.reg  = WDT_CLEAR_CLEAR_KEY;
        __WFI();
    }
}
```

**Correct**
```c
int main(void) {
    read_reset_cause_and_store_it_for_telemetry(&satellite_system_state);
    configure_watchdog_with_16_second_timeout();
    uart_initialize_sercom5_at_115200_baud();
    adc_initialize_for_solar_panel_voltage_and_current();
    pwm_initialize_for_dcdc_converter_duty_cycle_control();
    mppt_algorithm_initialize(&mppt_state);

    while (1) /* @non-terminating@ */ {
        uint16_t raw_panel_voltage = adc_read_solar_panel_voltage_raw();
        uint16_t raw_panel_current = adc_read_solar_panel_current_raw();
        uint16_t new_duty_cycle    = mppt_algorithm_run_one_iteration(
                                         &mppt_state,
                                         raw_panel_voltage,
                                         raw_panel_current);
        pwm_set_dcdc_duty_cycle(new_duty_cycle);
        watchdog_pet();
    }

    return 0;
}
```

---

### Rule A4: Every Register Write Gets a Comment Explaining Why

Comments that restate the constant name are useless. Comments explain what happens
in the hardware and why it is needed at this exact point.

```c
// Wrong: restates the code, adds nothing
SERCOM5->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;  // enable CTRLA

// Correct: explains the hardware consequence
SERCOM5->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
// Activates the UART peripheral. Before this bit, the hardware ignores the
// TX pin and shifts no bits, even though all configuration is complete.
```

---

### Rule A5: SYNCBUSY Waits Are Always Commented

Every `while (something->SYNCBUSY)` must explain what breaks if the wait is skipped.

```c
// Wrong
while (SERCOM5->USART.SYNCBUSY.bit.ENABLE);

// Correct
// ENABLE propagates across two clock domains. Any write before this clears
// is silently discarded — the UART appears enabled but does not function.
while (SERCOM5->USART.SYNCBUSY.bit.ENABLE);
```

---

### Rule A6: OR-assignment Lines Are Commented Per Bit

Each bit being set in a compound register write must have an inline comment
explaining what it controls in hardware.

```c
// Wrong — reader must look up every constant in the datasheet
SERCOM5->USART.CTRLA.reg =
    SERCOM_USART_CTRLA_DORD | SERCOM_USART_CTRLA_MODE_USART_INT_CLK |
    SERCOM_USART_CTRLA_TXPO(1);

// Correct
SERCOM5->USART.CTRLA.reg =
    SERCOM_USART_CTRLA_DORD               |    // LSB first — standard UART
    SERCOM_USART_CTRLA_MODE_USART_INT_CLK |    // baud clock is internal
    SERCOM_USART_CTRLA_TXPO(1);                // TX on pad group 1 = PB22
```

---

## SECTION B: ARCHITECTURAL PRINCIPLES

### Rule B1: Separate Pure Logic From Hardware Access

Every function belongs to one of two categories. Never mix them.

**Category 1 — Pure logic functions.**
Take inputs, return outputs, touch no hardware registers, access no global state.
Given the same inputs, always return the same output.
These can be compiled and tested on a laptop with no chip involved.

**Category 2 — Hardware functions.**
Read or write peripheral registers, interact with DMA, control pins.
These require the chip and cannot be meaningfully tested without it.

```c
// Category 1 — pure. No hardware. Testable on laptop.
uint16_t convert_raw_adc_reading_to_millivolts(
    uint16_t raw_adc_reading,
    uint16_t reference_voltage_in_millivolts,
    uint8_t  adc_resolution_in_bits)
{
    SATELLITE_ASSERT(reference_voltage_in_millivolts > 0u);
    SATELLITE_ASSERT(adc_resolution_in_bits >= 8u);
    SATELLITE_ASSERT(adc_resolution_in_bits <= 16u);
    uint32_t max_count = (1u << adc_resolution_in_bits) - 1u;
    return (uint16_t)(
        ((uint32_t)raw_adc_reading * reference_voltage_in_millivolts) / max_count
    );
}

// Category 2 — hardware. Reads a register. Chip required.
uint16_t adc_read_result_register_from_hardware(void) {
    return (uint16_t)ADC->RESULT.reg;
}
```

This separation is not academic. The MPPT algorithm, the telemetry packet builder,
the command parser, the CRC calculator — all pure. All testable before any hardware
exists. The boundary between categories is a deliberate architectural decision that
must be maintained in every module.

---

### Rule B2: All State Lives in Structs, Passed by Pointer

Module state is declared as a single `static` struct inside the module's `.c` file.
Functions that need to modify state receive a pointer to that struct as a parameter.
No globals. No scattered file-scope variables.

```c
// Wrong — state scattered as separate globals
static uint32_t write_index = 0;
static uint32_t read_index  = 0;
static uint8_t  overflow    = 0;

// Correct — all state in one struct
static struct uart_module_state {
    uint8_t          receive_buffer[256];
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    volatile uint8_t  overflow_has_occurred;
} uart_state;
```

The single-struct rule means that when something goes wrong, every piece of state
that could be relevant is in one place. It also makes module initialization and
reset trivial: zero the struct and start from a known state.

---

## SECTION C: SAFETY RULES (NASA POWER OF 10 + JPL D-60411)

These rules are mandatory for all JPL flight software since 2006.
Sources: Holzmann, IEEE Computer, June 2006; JPL DOCID D-60411, March 2009.

---

### Rule C1: No goto, setjmp, longjmp, or Recursion

Never use `goto`, `setjmp`, `longjmp`, or recursive function calls.

Recursion makes stack depth unbounded and impossible to analyze statically.
On 16 KB of RAM a recursive function that calls itself 100 levels deep overflows
the stack silently, corrupts memory, and crashes with no error message.

```c
// Wrong — recursive, stack grows unboundedly
int32_t factorial(int32_t n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

// Correct — iterative, constant stack usage regardless of input
int32_t factorial_of_non_negative_integer(int32_t n) {
    SATELLITE_ASSERT(n >= 0);
    SATELLITE_ASSERT(n <= 12); // 13! overflows int32_t
    int32_t result = 1;
    for (int32_t i = 2; i <= n; i += 1) {
        result *= i;
    }
    return result;
}
```

---

### Rule C2: All Loops Must Have a Fixed Upper Bound

Every terminating loop must have a compile-time constant bound that a static
analysis tool can verify. The main superloop `while(1)` is intentionally
non-terminating and must be annotated with `/* @non-terminating@ */`.

```c
// Wrong — no provable upper bound, could spin forever
while (some_flag_that_might_never_clear) { do_work(); }

// Correct — compile-time constant bound
#define MAXIMUM_BYTES_TO_SEARCH_IN_RECEIVE_BUFFER  256u
for (uint16_t i = 0; i < MAXIMUM_BYTES_TO_SEARCH_IN_RECEIVE_BUFFER; i += 1) {
    if (buffer[i] == END_OF_PACKET_MARKER) { break; }
}

// Correct — annotated non-terminating main loop
while (1) /* @non-terminating@ */ {
    run_one_satellite_control_loop_iteration();
}
```

---

### Rule C3: No Dynamic Memory Allocation After Initialization

Never call `malloc()`, `free()`, `calloc()`, `realloc()`, or `alloca()` after boot.
All memory is statically allocated at compile time.

```c
// Wrong — can fail unpredictably, causes fragmentation over time
uint8_t *buffer = malloc(256);

// Correct — size known at compile time, never fails at runtime
static uint8_t receive_buffer_for_uart_bytes[256];
```

Dynamic allocators have unpredictable execution time, can fail at runtime, and
cause fragmentation over long missions. A malloc failure six months into orbit
cannot be fixed.

---

### Rule C4: No Function Longer Than 60 Lines

No function shall exceed 60 lines of code (one printed page at standard formatting).
Code that cannot be read in one screenful cannot be reliably reviewed.
Split larger functions into named helpers.

---

### Rule C5: Assertions Are Sanity Checks on Invariants — They Stay in Flight

An assertion is a check on something that must always be true by design.
It is not a debug print. It does not observe normal operation.
It fires only when something that should be impossible has happened —
memory corruption, a caller bug, hardware behaving outside specification.

Assertions remain active in flight builds. They are the early warning system
for unexpected states that cannot be anticipated at design time.

**The SATELLITE_ASSERT macro:**

```c
// assertion_handler.h
#ifndef ASSERTION_HANDLER_H
#define ASSERTION_HANDLER_H

#include <stdint.h>

// satellite_handle_assertion_failure() is always defined.
// Its behaviour differs between debug and flight builds.
void satellite_handle_assertion_failure(const char *file, int line);

#define SATELLITE_ASSERT(condition)                                            \
    do {                                                                        \
        if (!(condition)) {                                                     \
            satellite_handle_assertion_failure(__FILE__, __LINE__);            \
        }                                                                       \
    } while (0)

#endif // ASSERTION_HANDLER_H
```

**What satellite_handle_assertion_failure() does:**

```c
// assertion_handler.c
#include "assertion_handler.h"
#include "debug_log_dma.h"    // for LOG() in debug builds
// obc_uart.h will be included here once the OBC interface is defined

void satellite_handle_assertion_failure(const char *file, int line) {

    // ── Debug build (USB connected, PuTTY open) ───────────────────────────────
    // Send the failure location to PuTTY so the developer can read it,
    // then freeze. The watchdog will reset the chip after its timeout.
    // Freezing rather than resetting immediately gives the developer time
    // to read the message before the chip restarts.
    #ifdef DEBUG_LOGGING_ENABLED
        LOG("!!! ASSERTION FAILED — SYSTEM HALTED !!!");
        LOG(file);
        LOG_I("line", line);
        while (1); // watchdog will reset after timeout
    #endif

    // ── Flight build (in orbit, OBC listening on mission UART) ───────────────
    // Notify the OBC so the fault is recorded and can be relayed to ground.
    // Then reset cleanly. The chip comes back up, reads the reset cause
    // register (PM->RCAUSE), logs a software reset, and resumes operation.
    //
    // NOTE: obc_send_fault_report() is a placeholder.
    // It will be implemented once the OBC UART interface and packet format
    // are defined (pending project hardware specification).
    #ifndef DEBUG_LOGGING_ENABLED
        // obc_send_fault_report(FAULT_CODE_ASSERTION_FAILURE, file, line);
        NVIC_SystemReset(); // ARM CMSIS: triggers immediate software reset
    #endif
}
```

Every function of more than 10 lines must have at least two assertions:
one precondition (validating inputs) and one postcondition (validating the result).

```c
uint16_t convert_raw_adc_reading_to_millivolts(
    uint16_t raw_adc_reading,
    uint16_t reference_voltage_in_millivolts,
    uint8_t  adc_resolution_in_bits)
{
    // Preconditions: verify inputs are within valid hardware ranges
    SATELLITE_ASSERT(adc_resolution_in_bits >= 8u);
    SATELLITE_ASSERT(adc_resolution_in_bits <= 16u);
    SATELLITE_ASSERT(reference_voltage_in_millivolts > 0u);

    uint32_t max_count    = (1u << adc_resolution_in_bits) - 1u;
    uint32_t result_mv    = ((uint32_t)raw_adc_reading
                              * reference_voltage_in_millivolts)
                            / max_count;

    // Postcondition: result cannot exceed the reference voltage
    SATELLITE_ASSERT(result_mv <= reference_voltage_in_millivolts);

    return (uint16_t)result_mv;
}
```

Assertions must be side-effect free. Never put anything inside `SATELLITE_ASSERT()`
that changes state.

---

### Rule C6: Declare Variables at the Smallest Possible Scope

Declare every variable in the innermost scope where it is used.
The fewer places a variable is visible, the fewer places it can be corrupted.

```c
// Wrong — global counter, visible to entire program
int loop_counter;

// Correct — scoped to the loop that uses it
for (int32_t loop_counter = 0; loop_counter < 10; loop_counter += 1) {
    process_item(loop_counter);
}
```

Persistent state belongs in the module state struct (Rule B2), not as a
file-scope or global variable.

---

### Rule C7: Check Every Non-Void Return Value

Every function call that returns a value must have its return value checked.
If it is genuinely irrelevant, cast explicitly to `(void)`.
A silently discarded return value is an undetected failure.

```c
// Wrong — error is silently ignored
uart_transmit_packet(packet, length);

// Correct — failure is detected and handled
int32_t transmit_result = uart_transmit_packet(packet, length);
if (transmit_result != UART_TRANSMIT_SUCCESS) {
    log_transmission_failure_for_telemetry(transmit_result);
}

// Correct — intentional discard is explicit
(void)memcpy(destination, source, byte_count);
```

---

### Rule C8: Validate All Parameters at Function Entry

Every public function must validate all inputs at the start of its body.
Do not assume callers pass valid data.

```c
// Wrong — division by zero if vref is 0
uint16_t convert_adc_to_millivolts(uint16_t raw, uint16_t vref) {
    return (uint32_t)raw * vref / 4095;
}

// Correct — validates before using
uint16_t convert_adc_to_millivolts(uint16_t raw, uint16_t vref) {
    SATELLITE_ASSERT(vref > 0u);
    SATELLITE_ASSERT(vref <= 3300u);
    SATELLITE_ASSERT(raw  <= 4095u);
    return (uint16_t)((uint32_t)raw * vref / 4095u);
}
```

---

### Rule C9: No Pointer-to-Pointer Indirection

Never use `**` or deeper levels of indirection. Single `*` only.

Pointer-to-pointer declarations cannot be reliably reviewed or analyzed by
static tools. On a Cortex-M0+ with 16 KB of RAM, pointer errors that corrupt
memory are not recoverable without a reset. The simplest possible pointer usage
is the only acceptable kind.

```c
uint8_t  *pointer_to_one_byte = ...;   // one level — only this is allowed
uint8_t **double_pointer       = ...;  // two levels — never use this
```

If you think you need `**`, reconsider the data structure. Pass a single pointer
and restructure the interface. There is always a way.

---

### Rule C10: Function Pointers Are Used Conservatively

Function pointers are allowed only when a reviewer can always determine
statically which function the pointer actually points to.
Never use function pointers whose target comes from external input.

```c
// Allowed — target is statically determinable by inspection
typedef void (*state_handler_type)(struct mission_state *);
static state_handler_type current_handler = handle_nominal_operating_mode;

// Forbidden — target determined from untrusted external data
void (*handler)(void) = command_table[received_byte]; // never do this
```

---

### Rule C11: Zero Warnings Policy — All Warnings Are Errors

The code must compile with all warnings enabled and produce zero warnings.
Zero errors is not sufficient. Zero warnings is the requirement.

Required compiler flags:
```
-Wall -Wextra -Werror -Wshadow -Wpointer-arith -Wcast-qual
-Wstrict-prototypes -Wmissing-prototypes -std=c99 -pedantic
```

If the compiler warns about code that appears correct, rewrite the code until
the warning disappears. Do not suppress warnings with `#pragma`. The compiler
is right.

---

### Rule C12: Make Evaluation Order Explicit With Parentheses

In compound expressions with multiple operators, always use parentheses to make
the intended order of evaluation unambiguous. Do not rely on operator precedence.

```c
// Wrong — order depends on memorizing C precedence rules
uint32_t result = a + b * c >> 2 & mask;

// Correct — unambiguous to any reader
uint32_t result = ((a + (b * c)) >> 2) & mask;
```

---

### Rule C13: No Side Effects in Boolean Expressions

Boolean expressions in conditions must not change any state.

```c
// Wrong — function call with side effect inside condition
if (uart_read_byte_and_advance_index() == START_OF_PACKET) { ... }

// Correct — separate the action from the test
uint8_t received_byte = uart_read_byte_and_advance_index();
if (received_byte == START_OF_PACKET) { ... }
```

---

### Rule C14: No Function-Like Macros — Use static inline

The preprocessor may only be used for symbolic constants and include guards.
Never define macros that take arguments. Use `static inline` functions instead.

```c
// Wrong — MAX(x++, y++) evaluates the larger argument twice
#define MAX(a, b)  ((a) > (b) ? (a) : (b))

// Correct — type-safe, no argument side effects
static inline int32_t return_larger_of_two_int32_values(
    int32_t first_value,
    int32_t second_value)
{
    return (first_value > second_value) ? first_value : second_value;
}
```

---

### Rule C15: All enum Values Explicitly Assigned or None

In an enum list, either assign values to all members or to none.
Never assign to some members and leave others implicit.

```c
// Wrong — partial assignment, values 2-4 silently skipped
typedef enum {
    RESET_CAUSE_POWER_ON = 0,
    RESET_CAUSE_WATCHDOG,
    RESET_CAUSE_SOFTWARE = 5,
} reset_cause_type;

// Correct — all values explicit
typedef enum {
    RESET_CAUSE_POWER_ON  = 0,
    RESET_CAUSE_WATCHDOG  = 1,
    RESET_CAUSE_SOFTWARE  = 2,
    RESET_CAUSE_EXTERNAL  = 3,
    RESET_CAUSE_BROWN_OUT = 4
} reset_cause_type;
```

---

### Rule C16: All extern Declarations in Header Files Only

Never write `extern` inside `.c` files. Put it in a `.h` file included by both
the defining file and every file that uses the object. This lets the compiler
catch type mismatches between declaration and definition.

---

### Rule C17: One Statement Per Line

Each line of code contains exactly one statement or one declaration.

```c
// Wrong
int a = 0; int b = 1; int c = a + b;

// Correct
int32_t first_value   = 0;
int32_t second_value  = 1;
int32_t sum_of_values = first_value + second_value;
```

---

## SECTION D: EMBEDDED SAFETY RULES

### Rule D1: `static` on Everything Private to a Module

Every function and variable not intended for use outside its `.c` file is `static`.
This is enforced by the linker — a `static` symbol cannot be referenced from any
other translation unit. It is not optional.

```c
// Public — declared in uart_driver.h
void uart_initialize_sercom5_at_115200_baud(void) { ... }

// Private — not in any header, invisible outside uart_driver.c
static void reset_sercom5_to_known_clean_state(void) { ... }
```

---

### Rule D2: `volatile` on Every Variable Shared With an ISR

Any variable written by an ISR and read by the main loop (or vice versa) must
be `volatile`. Without it, the compiler caches the value in a register and the
main loop never sees updates made by the ISR.

```c
static struct uart_module_state {
    uint8_t           receive_buffer[256];
    volatile uint32_t write_index;              // ISR writes this
    volatile uint32_t read_index;               // main loop writes this
    volatile uint8_t  overflow_has_occurred;    // ISR sets, main clears
} uart_state;
```

---

### Rule D3: Fixed-Width Integer Types Everywhere

Never use `int`, `unsigned int`, `char`, `short`, or `long`.
Their size is platform-dependent. Always use `<stdint.h>` types.

```c
// Wrong:   int, char, unsigned, long
// Correct: int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t
```

---

### Rule D4: No Magic Numbers — Every Constant Explained

Every numeric constant that is not 0 or 1 must have either a named constant
or a comment explaining its derivation.

```c
// Wrong
SERCOM5->USART.BAUD.reg = 63019;

// Correct
// BAUD = 65536 * (1 - 16 * (115200 / 48000000)) = 63019. Error < 0.01%.
SERCOM5->USART.BAUD.reg = 63019;
```

---

### Rule D5: ISRs Only Set Flags and Store Data — Nothing Else

ISRs must complete in the shortest possible time. They read hardware registers,
store data into buffers, and set flags. All logic runs in the main loop.

```c
// Wrong — complex logic in ISR
void SERCOM5_Handler(void) {
    if (buffer_full) { uart_send_error_to_obc(); } // never do this
}

// Correct — minimal ISR, main loop handles consequences
void SERCOM5_Handler(void) {
    if (uart_state.write_index >= 256u) {
        uart_state.overflow_has_occurred = 1u;
        return;
    }
    uart_state.receive_buffer[uart_state.write_index] =
        (uint8_t)SERCOM5->USART.DATA.reg;
    uart_state.write_index += 1u;
}
```

ISR names must match the vector table in `startup_samd21.c` exactly.
Common names: `SERCOM0_Handler`, `ADC_Handler`, `TC3_Handler`,
`RTC_Handler`, `DMAC_Handler`, `HardFault_Handler`.

Never call from an ISR: any blocking function, any function with unpredictable
execution time, LOG(), malloc(), printf().

Maximum ISR body length: 10 lines.

---

### Rule D6: Include Guards in Every Header File

```c
// uart_driver.h
#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>

void uart_initialize_sercom5_at_115200_baud(void);

#endif // UART_DRIVER_H
```

---

## SECTION E: FILE STRUCTURE TEMPLATE

Every `.c` file follows this structure in this order. No exceptions.

```c
// =============================================================================
// module_name.c
// One sentence describing exactly what this module is responsible for.
//
// Category: PURE LOGIC (no hardware) or HARDWARE DRIVER (register access)
// Peripheral: which peripheral (if hardware driver)
// Pins: which pins, for what purpose (if hardware driver)
// Clock: which GCLK at what frequency (if hardware driver)
// Interrupt: which ISR, what event triggers it (if hardware driver)
// =============================================================================

// Standard library includes first
#include <stdint.h>
#include <stdbool.h>

// Hardware-specific includes second (omit for pure logic modules)
#include "samd21g17d.h"

// Project includes third
#include "assertion_handler.h"
#include "module_name.h"

// ── Module state (static — invisible outside this file) ───────────────────────
static struct module_name_state {
    uint8_t           buffer[256];
    volatile uint32_t write_index;
    volatile uint8_t  error_flag;
} module_state;

// ── ISR (hardware driver modules only) ───────────────────────────────────────
void PERIPHERAL_Handler(void) { ... }

// ── Public functions (declared in module_name.h) ──────────────────────────────
void module_initialize(void) { ... }

// ── Private functions (all static, one job each) ──────────────────────────────
static void do_one_specific_thing(void) { ... }
```

---

## SECTION F: QUICK REFERENCE

| Rule | Requirement |
|---|---|
| Names | Long, explicit, sentence-like. No abbreviations ever. |
| Functions | One job. Verb phrase name. Max 60 lines. |
| Pure vs hardware | Separate deliberately. Never mix in the same function. |
| State | All module state in one static struct. Passed by pointer. |
| `main()` | No register names. Named function calls only. |
| Register writes | Every one commented with why, not what. |
| `SYNCBUSY` waits | Always comment what breaks without the wait. |
| `\|=` lines | Inline comment per bit explaining hardware effect. |
| Loops | Fixed compile-time bound always. Annotate `while(1)`. |
| Memory | No `malloc`. All allocation static at compile time. |
| Recursion | Never. Use iterative loops. |
| Integers | `uint8_t`, `int32_t`, etc. Never `int` or `char`. |
| `volatile` | On every variable shared between ISR and main loop. |
| `static` | On every function and variable private to a module. |
| Assertions | Min two per function >10 lines. Stay in flight builds. |
| Assertion behaviour | Debug: log to PC + freeze. Flight: notify OBC + reset. |
| Return values | Check every non-void return. Cast `(void)` if discarding. |
| Parameters | Assert validity at entry of every public function. |
| Warnings | Zero warnings. `-Werror` always enabled. |
| Magic numbers | Every constant has a name or derivation comment. |
| Scope | Declare at innermost scope where variable is used. |
| ISRs | Max 10 lines. Flags and buffer writes only. |
| Pointers | Single `*` only. Never `**`. |
| Function pointers | Only when target is statically determinable. |
| `enum` | All values assigned or none. Never partial. |
| Parentheses | Always explicit in compound expressions. |
| Side effects | Never inside boolean conditions. |
| Preprocessor | Constants and include guards only. No function macros. |
| `extern` | In header files only. Never in `.c` files. |
| Include guards | Every `.h` file has `#ifndef / #define / #endif`. |
| One statement | One statement or declaration per line. |

---

*Sources: NASA Power of 10 (Holzmann, 2006); JPL Institutional Coding Standard
D-60411 (2009); MISRA C:2004. These are the standards used on Mars rovers and
all JPL flight software. Rationale: you cannot update the code after launch.*
