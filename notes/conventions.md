# Satellite Firmware Coding Conventions

This document defines the coding style for all satellite flight software written in C.
It is a binding reference. Every file, every function, every variable must follow these rules.

This document is based on three authoritative sources, applied together:
- **NASA Power of 10** — Gerard Holzmann, JPL Laboratory for Reliable Software, 2006
- **JPL Institutional Coding Standard for C** (DOCID D-60411, March 2009)
- **MISRA C:2004** — the embedded industry safety standard referenced by the JPL standard

The guiding principle: **code must read like English sentences.**

---

## SECTION A: READABILITY AND NAMING CONVENTIONS

### Rule A1: Names Must Be Sentences, Not Abbreviations

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
void    uart_initialize_sercom0_at_115200_baud(void);
```

---

### Rule A2: Functions Have One Job. The Name Describes That One Job.

**Wrong** — one function doing many things:
```c
void setup(void) {
    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM0;
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 |
                        GCLK_CLKCTRL_ID(SERCOM0_GCLK_ID_CORE);
    while (GCLK->STATUS.bit.SYNCBUSY);
    SERCOM0->USART.CTRLA.reg = SERCOM_USART_CTRLA_SWRST;
    while (SERCOM0->USART.SYNCBUSY.bit.SWRST);
    SERCOM0->USART.CTRLA.reg = SERCOM_USART_CTRLA_DORD |
        SERCOM_USART_CTRLA_MODE_USART_INT_CLK |
        SERCOM_USART_CTRLA_RXPO(3) | SERCOM_USART_CTRLA_TXPO(1);
    SERCOM0->USART.CTRLB.reg = SERCOM_USART_CTRLB_TXEN | SERCOM_USART_CTRLB_RXEN;
    while (SERCOM0->USART.SYNCBUSY.bit.CTRLB);
    SERCOM0->USART.BAUD.reg = 63019;
    SERCOM0->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
    while (SERCOM0->USART.SYNCBUSY.bit.ENABLE);
    NVIC_EnableIRQ(SERCOM0_IRQn);
}
```

**Correct** — public entry point reads like a table of contents; all private functions are `static`:
```c
void uart_initialize_sercom0_at_115200_baud(void) {
    enable_sercom0_bus_clock_so_cpu_can_write_its_registers();
    connect_48mhz_clock_to_sercom0_so_it_can_operate();
    configure_pa10_as_uart_transmit_pin_not_gpio();
    configure_pa11_as_uart_receive_pin_not_gpio();
    reset_sercom0_to_known_clean_state();
    configure_sercom0_as_uart_with_internal_clock();
    set_uart_baud_rate_to_115200_assuming_48mhz_clock();
    enable_sercom0_uart_hardware();
    enable_sercom0_interrupt_in_cpu_interrupt_controller();
}

static void enable_sercom0_bus_clock_so_cpu_can_write_its_registers(void) {
    // The Power Manager controls which peripherals are powered on the bus.
    // By default SERCOM0 is off — writing to its registers does nothing.
    // This single bit turns on the bus clock, making register writes work.
    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM0;
}

static void connect_48mhz_clock_to_sercom0_so_it_can_operate(void) {
    // The bus clock allows the CPU to read/write SERCOM0 registers.
    // SERCOM0 also needs a functional clock to actually do work:
    // generate baud rate timing, shift bits in and out, etc.
    // This connects GCLK0 (48 MHz) to SERCOM0's core clock channel.
    GCLK->CLKCTRL.reg =
        GCLK_CLKCTRL_CLKEN                     |   // enable this connection
        GCLK_CLKCTRL_GEN_GCLK0                 |   // source: GCLK0 = 48 MHz
        GCLK_CLKCTRL_ID(SERCOM0_GCLK_ID_CORE);     // destination: SERCOM0
    // GCLK writes cross clock domains. Writing to SERCOM0 before this
    // clears produces undefined behavior.
    while (GCLK->STATUS.bit.SYNCBUSY);
}

static void configure_pa10_as_uart_transmit_pin_not_gpio(void) {
    // PA10 defaults to plain GPIO. We need it to be SERCOM0 TX.
    // PMUXEN=1 routes this pin to the peripheral mux instead of plain GPIO.
    PORT->Group[0].PINCFG[10].reg |= PORT_PINCFG_PMUXEN;
    // Mux value "C" selects SERCOM0 function. PA10 is even so we use PMUXE.
    PORT->Group[0].PMUX[10/2].bit.PMUXE = PORT_PMUX_PMUXE_C_Val;
}

static void configure_pa11_as_uart_receive_pin_not_gpio(void) {
    // Same logic as PA10. PA11 is odd-numbered so we use the PMUXO field.
    PORT->Group[0].PINCFG[11].reg |= PORT_PINCFG_PMUXEN;
    PORT->Group[0].PMUX[11/2].bit.PMUXO = PORT_PMUX_PMUXO_C_Val;
}

static void reset_sercom0_to_known_clean_state(void) {
    // Reset before configuring. Clears any leftover state from previous boots,
    // watchdog resets, or the debugger leaving things in an unexpected state.
    SERCOM0->USART.CTRLA.reg = SERCOM_USART_CTRLA_SWRST;
    // Reset propagates across two clock domains.
    // Any write before SYNCBUSY clears is silently discarded.
    while (SERCOM0->USART.SYNCBUSY.bit.SWRST);
}

static void configure_sercom0_as_uart_with_internal_clock(void) {
    SERCOM0->USART.CTRLA.reg =
        SERCOM_USART_CTRLA_DORD               |    // LSB first — standard for UART
        SERCOM_USART_CTRLA_MODE_USART_INT_CLK |    // clock source: internal not external
        SERCOM_USART_CTRLA_RXPO(3)            |    // RX data arrives on pad 3 = PA11
        SERCOM_USART_CTRLA_TXPO(1);                // TX data leaves on pad group 1 = PA10

    SERCOM0->USART.CTRLB.reg =
        SERCOM_USART_CTRLB_TXEN               |    // transmitter on
        SERCOM_USART_CTRLB_RXEN;                   // receiver on
    // CTRLB crosses a clock domain — must wait before proceeding.
    while (SERCOM0->USART.SYNCBUSY.bit.CTRLB);
}

static void set_uart_baud_rate_to_115200_assuming_48mhz_clock(void) {
    // Formula: BAUD = 65536 * (1 - 16 * (baud_rate / clock_frequency))
    //        = 65536 * (1 - 16 * (115200 / 48000000))
    //        = 63019
    // Resulting actual timing error: < 0.01%. UART tolerates up to ~3%.
    SERCOM0->USART.BAUD.reg = 63019;
}

static void enable_sercom0_uart_hardware(void) {
    // Activates the peripheral. Before this bit is set, the UART does not
    // transmit or receive anything even though all registers are configured.
    SERCOM0->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
    // Do not use the UART until this clears.
    while (SERCOM0->USART.SYNCBUSY.bit.ENABLE);
}

static void enable_sercom0_interrupt_in_cpu_interrupt_controller(void) {
    // SERCOM0 can signal interrupts, but the CPU's NVIC must also be told
    // to listen. Without this line, interrupt signals from SERCOM0 are
    // ignored by the CPU entirely.
    NVIC_EnableIRQ(SERCOM0_IRQn);
}
```

---

### Rule A3: main() Must Read Like a Table of Contents

`main()` contains no register names, no hardware constants, no raw addresses.

**Wrong**
```c
int main(void) {
    NVMCTRL->CTRLB.bit.RWS = 1;
    PM->APBAMASK.reg |= PM_APBAMASK_GCLK;
    while (1) {
        ADC->SWTRIG.reg = ADC_SWTRIG_START;
        WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
        __WFI();
    }
}
```

**Correct**
```c
int main(void) {
    read_reset_cause_and_store_it_for_telemetry(&satellite_system_state);
    configure_watchdog_with_16_second_timeout();
    uart_initialize_sercom0_at_115200_baud();
    adc_initialize_for_temperature_sensor_on_pa02();
    rtc_initialize_for_10_second_periodic_wakeup();

    while (1) /* @non-terminating@ */ {
        adc_start_one_conversion();
        uint16_t raw_temperature = adc_wait_for_result_with_timeout();
        uart_transmit_telemetry_packet(raw_temperature, &satellite_system_state);
        watchdog_pet();
        sleep_in_standby_mode_until_rtc_alarm_fires();
    }

    return 0;
}
```

---

### Rule A4: Every Register Write Gets a Comment Explaining Why

```c
// Wrong: comment restates the code
SERCOM0->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;  // enable CTRLA

// Correct: explains hardware effect
SERCOM0->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
// Activates the UART peripheral. Before this bit is set, the hardware ignores
// TX/RX pins and shifts no bits, even though all other registers are written.
```

---

### Rule A5: SYNCBUSY Waits Are Always Commented

```c
// Wrong
while (SERCOM0->USART.SYNCBUSY.bit.ENABLE);

// Correct
// ENABLE change propagates across two clock domains. Any register access
// before this clears is silently discarded — UART appears configured but
// does not actually function.
while (SERCOM0->USART.SYNCBUSY.bit.ENABLE);
```

---

### Rule A6: OR-assignment Lines Are Commented Per Bit

```c
// Wrong
SERCOM0->USART.CTRLA.reg =
    SERCOM_USART_CTRLA_DORD | SERCOM_USART_CTRLA_MODE_USART_INT_CLK |
    SERCOM_USART_CTRLA_RXPO(3) | SERCOM_USART_CTRLA_TXPO(1);

// Correct
SERCOM0->USART.CTRLA.reg =
    SERCOM_USART_CTRLA_DORD               |    // LSB first — standard UART
    SERCOM_USART_CTRLA_MODE_USART_INT_CLK |    // clock: internal not external
    SERCOM_USART_CTRLA_RXPO(3)            |    // RX = pad 3 = PA11
    SERCOM_USART_CTRLA_TXPO(1);                // TX = pad group 1 = PA10
```

---

## SECTION B: NASA POWER OF 10 RULES (MANDATORY)

Source: Gerard Holzmann, JPL, IEEE Computer, June 2006.
Used on all JPL flight software since 2006 including Mars rovers.

---

### NASA Rule 1: No goto, setjmp, longjmp, or Recursion

```c
// Wrong — recursive, stack grows with every call, crashes with large inputs
int32_t factorial_recursive(int32_t n) {
    if (n <= 1) { return 1; }
    return n * factorial_recursive(n - 1);
}

// Correct — iterative, constant stack usage
int32_t factorial_iterative(int32_t n) {
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

### NASA Rule 2: All Loops Must Have a Fixed Upper Bound

Every terminating loop must have a compile-time constant bound.
The main superloop `while(1)` is intentionally non-terminating and must be
annotated with `/* @non-terminating@ */`.

```c
// Wrong — no provable bound
while (some_flag_that_might_never_clear) { do_work(); }

// Correct
#define MAXIMUM_BYTES_TO_SEARCH_IN_COMMAND_BUFFER  256u
for (uint16_t i = 0; i < MAXIMUM_BYTES_TO_SEARCH_IN_COMMAND_BUFFER; i += 1) {
    if (buffer[i] == END_MARKER) { break; }
}
```

---

### NASA Rule 3: No Dynamic Memory Allocation After Initialization

Never call `malloc()`, `free()`, `calloc()`, `realloc()`, `alloca()` after boot.
All memory is statically allocated at compile time.

```c
// Wrong
uint8_t *buffer = malloc(256);

// Correct
static uint8_t receive_buffer_for_uart_bytes[256];
```

---

### NASA Rule 4: No Function Longer Than One Printed Page

Maximum 60 lines per function. Split larger functions into named helpers.
Code that cannot be read in one screenful cannot be reliably reviewed.

---

### NASA Rule 5: Minimum Two Assertions Per Non-Trivial Function

Every function of more than 10 lines must have at least two assertions.

```c
#define SATELLITE_ASSERT(condition)                                        \
    do {                                                                    \
        if (!(condition)) {                                                 \
            satellite_handle_assertion_failure(__FILE__, __LINE__);        \
        }                                                                   \
    } while (0)

uint16_t convert_raw_adc_reading_to_millivolts(
    uint16_t raw_adc_reading,
    uint16_t reference_voltage_in_millivolts,
    uint8_t  adc_resolution_in_bits)
{
    // Precondition assertions
    SATELLITE_ASSERT(adc_resolution_in_bits >= 8u);
    SATELLITE_ASSERT(adc_resolution_in_bits <= 16u);
    SATELLITE_ASSERT(reference_voltage_in_millivolts > 0u);

    uint32_t max_count  = (1u << adc_resolution_in_bits) - 1u;
    uint32_t result_mv  = ((uint32_t)raw_adc_reading * reference_voltage_in_millivolts)
                          / max_count;

    // Postcondition assertion
    SATELLITE_ASSERT(result_mv <= reference_voltage_in_millivolts);

    return (uint16_t)result_mv;
}
```

---

### NASA Rule 6: Declare Variables at the Smallest Possible Scope

```c
// Wrong — global counter visible to entire program
int loop_counter;

// Correct — scoped to where it is actually used
for (int32_t loop_counter = 0; loop_counter < 10; loop_counter += 1) {
    process_item(loop_counter);
}
```

Persistent state belongs in a module-state struct allocated in `main()` and
passed by pointer — not as a global or file-scope variable.

---

### NASA Rule 7: Check Every Non-Void Return Value

```c
// Wrong — error silently discarded
uart_transmit_packet(packet, length);

// Correct
int32_t transmit_result = uart_transmit_packet(packet, length);
if (transmit_result != UART_TRANSMIT_SUCCESS) {
    log_transmission_failure_for_telemetry(transmit_result);
}

// Correct — intentional discard is explicit
(void)memcpy(destination, source, byte_count);
```

---

### NASA Rule 8: No More Than Two Levels of Pointer Indirection

```c
uint8_t  *pointer_to_byte     = ...;  // one level — fine
uint8_t **pointer_to_pointer  = ...;  // two levels — allowed
uint8_t ***triple_indirection = ...;  // three levels — never
```

---

### NASA Rule 9: Function Pointers Are Used Conservatively

Use function pointers only when a reviewer can always determine statically
which function the pointer actually points to.

```c
// Allowed — target is statically determinable
static state_handler_type current_handler = handle_nominal_operating_mode;

// Forbidden — target set from untrusted external data
void (*handler)(void) = command_table[received_byte]; // dangerous
```

---

### NASA Rule 10: Zero Warnings Policy — All Warnings Are Errors

Required compiler flags:
```
-Wall -Wextra -Werror -Wshadow -Wpointer-arith -Wcast-qual
-Wstrict-prototypes -Wmissing-prototypes -std=c99 -pedantic
```

If the compiler warns about code that appears correct, rewrite the code.
Do not suppress warnings with pragmas. The compiler is right.

---

## SECTION C: JPL INSTITUTIONAL STANDARD RULES (D-60411)

Source: JPL DOCID D-60411, March 2009.

---

### JPL Rule C1: Validate All Parameters at Function Entry

```c
// Wrong
uint16_t convert_adc_to_millivolts(uint16_t raw, uint16_t vref) {
    return (uint32_t)raw * vref / 4095; // division by zero if vref == 0
}

// Correct
uint16_t convert_adc_to_millivolts(uint16_t raw, uint16_t vref) {
    SATELLITE_ASSERT(vref > 0u);
    SATELLITE_ASSERT(vref <= 3300u);
    SATELLITE_ASSERT(raw  <= 4095u);
    return (uint16_t)((uint32_t)raw * vref / 4095u);
}
```

---

### JPL Rule C2: Make Evaluation Order Explicit With Parentheses

```c
// Wrong — relies on memorizing C operator precedence
uint32_t result = a + b * c >> 2 & mask;

// Correct — order is unambiguous
uint32_t result = ((a + (b * c)) >> 2) & mask;
```

---

### JPL Rule C3: No Side Effects in Boolean Expressions

```c
// Wrong — function call with side effect inside condition
if (uart_read_byte_and_advance_index() == START_OF_PACKET) { ... }

// Correct
uint8_t received_byte = uart_read_byte_and_advance_index();
if (received_byte == START_OF_PACKET) { ... }
```

---

### JPL Rule C4: One Statement Per Line

```c
// Wrong
int a = 0; int b = 1; int c = a + b;

// Correct
int32_t first_value   = 0;
int32_t second_value  = 1;
int32_t sum_of_values = first_value + second_value;
```

---

### JPL Rule C5: No Function-Like Macros — Use static inline

```c
// Wrong — hidden double-evaluation: MAX(x++, y++) increments the larger twice
#define MAX(a, b)  ((a) > (b) ? (a) : (b))

// Correct — type-safe, no argument side effects
static inline int32_t return_larger_of_two_int32_values(int32_t a, int32_t b) {
    return (a > b) ? a : b;
}
```

---

### JPL Rule C6: All extern Declarations in Header Files Only

Never write `extern` inside `.c` files. Put it in a `.h` file included by both
the defining file and every file that uses the object.

---

### JPL Rule C7: All enum Values Explicitly Assigned or None

```c
// Wrong — partial assignment creates ambiguity
typedef enum {
    RESET_CAUSE_POWER_ON = 0,
    RESET_CAUSE_WATCHDOG,       // value is implicitly 1, unclear
    RESET_CAUSE_SOFTWARE = 5,   // values 2, 3, 4 skipped silently
} reset_cause_type;

// Correct
typedef enum {
    RESET_CAUSE_POWER_ON  = 0,
    RESET_CAUSE_WATCHDOG  = 1,
    RESET_CAUSE_SOFTWARE  = 2,
    RESET_CAUSE_EXTERNAL  = 3,
    RESET_CAUSE_BROWN_OUT = 4
} reset_cause_type;
```

---

## SECTION D: EMBEDDED SAFETY RULES

### Rule D1: `static` on Everything Private to a Module

```c
void uart_initialize_sercom0_at_115200_baud(void) { ... }  // public
static void reset_sercom0_to_known_clean_state(void) { ... } // private
```

### Rule D2: `volatile` on Every Variable Shared With an ISR

```c
struct uart_module_state {
    uint8_t          receive_buffer[256];
    volatile int32_t write_index;              // ISR writes this
    volatile int32_t read_index;               // main loop writes this
    volatile uint8_t overflow_error_occurred;  // ISR sets, main clears
};
```

### Rule D3: Fixed-Width Integer Types Everywhere

```c
// Wrong:   int, char, unsigned, long
// Correct: int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t
```

### Rule D4: No Magic Numbers — Every Constant Explained

```c
// Wrong
SERCOM0->USART.BAUD.reg = 63019;

// Correct — derivation documented in comment
// 65536 * (1 - 16 * (115200 / 48000000)) = 63019. Error < 0.01%.
SERCOM0->USART.BAUD.reg = 63019;
```

### Rule D5: ISRs Only Set Flags — Processing Happens in Main Loop

```c
// Wrong — complex logic in ISR
void SERCOM0_Handler(void) {
    if (buffer_full) { uart_send_error_to_ground(); }
}

// Correct
void SERCOM0_Handler(void) {
    if (uart_state.write_index >= 256) {
        uart_state.overflow_error_occurred = 1;
        return;
    }
    uart_state.buffer[uart_state.write_index] = (uint8_t)SERCOM0->USART.DATA.reg;
    uart_state.write_index += 1;
}
```

### Rule D6: Include Guards in Every Header File

```c
#ifndef UART_DRIVER_H
#define UART_DRIVER_H
// ... declarations ...
#endif // UART_DRIVER_H
```

---

## SECTION E: ISR CONVENTIONS

**Name:** Must match the vector table in `startup_samd21.c` exactly.
Common names: `SERCOM0_Handler`, `ADC_Handler`, `TC3_Handler`,
`RTC_Handler`, `HardFault_Handler`.

**Length:** Maximum 10 lines.

**Allowed:** Read hardware register. Store in buffer. Set error flag. Clear interrupt flag. Return.

**Forbidden:** `printf`, `malloc`, blocking loops, functions with unpredictable execution time.

---

## SECTION F: FILE STRUCTURE TEMPLATE

```c
// =============================================================================
// module_name.c
// One sentence: what this module is responsible for.
//
// Peripheral: which hardware peripheral
// Pins: which pins, for what purpose
// Clock: which GCLK, at what frequency
// Interrupt: which handler, what event triggers it
// =============================================================================

#include <stdint.h>
#include <stdbool.h>
#include "samd21g17d.h"
#include "module_name.h"

// ── Module state (static = invisible outside this file) ───────────────────────
static struct module_state_type {
    uint8_t          buffer[256];
    volatile int32_t write_index;
    volatile uint8_t error_flag;
} module_state;

// ── ISR ───────────────────────────────────────────────────────────────────────
void PERIPHERAL_Handler(void) { ... }

// ── Public functions (declared in module_name.h) ──────────────────────────────
void module_initialize_xxx(void) { ... }

// ── Private functions (all static) ───────────────────────────────────────────
static void do_one_specific_hardware_thing(void) { ... }
```

---

## SECTION G: QUICK REFERENCE

| Rule | Requirement |
|---|---|
| Names | Long, explicit, sentence-like. No abbreviations. |
| Functions | One job. Verb phrase name. Max 60 lines. |
| `main()` | No register names. Named function calls only. |
| Register writes | Every one commented with why, not what. |
| `SYNCBUSY` waits | Always comment what breaks without the wait. |
| `\|=` lines | Inline comment per bit explaining hardware effect. |
| Loops | Fixed compile-time bound always. Annotate `while(1)`. |
| Memory | No `malloc`. All allocation static. |
| Recursion | Never. Use iterative loops. |
| Integers | `uint8_t`, `int32_t`, etc. Never `int` or `char`. |
| `volatile` | On every variable shared between ISR and main loop. |
| `static` | On every function and variable private to a module. |
| Assertions | Minimum two per function > 10 lines. |
| Return values | Check every non-void return. Cast `(void)` if discarding. |
| Parameters | Validate at entry of every public function. |
| Warnings | Zero warnings. `-Werror` always enabled. |
| Magic numbers | Every constant has a name or derivation comment. |
| Scope | Declare at innermost scope where variable is used. |
| ISRs | Max 10 lines. Flags only. No complex calls. |
| `enum` | All values assigned or none. Never partial. |
| Parentheses | Always explicit in compound expressions. |
| Side effects | Never inside boolean conditions. |
| Preprocessor | Constants and include guards only. No function macros. |
| `extern` | In header files only. Never in `.c` files. |
| Include guards | Every `.h` file has `#ifndef / #define / #endif`. |
| One statement | One statement or declaration per line. |

---

*Sources: NASA Power of 10 (Holzmann, 2006); JPL Institutional Coding Standard D-60411 (2009);
MISRA C:2004. These are the standards used on Mars rovers and all JPL flight software.
Rationale: you cannot update the code after launch.*
