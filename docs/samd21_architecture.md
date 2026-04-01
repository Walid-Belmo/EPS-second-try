# SAMD21G17D Architecture Overview

This document covers the hardware fundamentals needed to understand why the
firmware is structured the way it is. It is a reference, not a tutorial.

---

## Memory Map

```
0x00000000 ── Flash (128 KB) ──────────────────────────────────────────────────
             Vector table at 0x00000000
             - Word 0: initial stack pointer (SP) value
             - Word 1: address of Reset_Handler
             - Words 2..n: addresses of all ISR handlers
             Application code follows immediately after
0x0001FFFF ── End of flash ────────────────────────────────────────────────────

0x20000000 ── SRAM (16 KB) ────────────────────────────────────────────────────
             .data section (initialized globals, copied from flash at boot)
             .bss section (zero-initialized globals)
             Heap (if malloc is used — we never use it)
             Stack (grows downward from top of RAM)
0x20003FFF ── End of RAM ──────────────────────────────────────────────────────

0x40000000 ── Peripheral registers ────────────────────────────────────────────
             SERCOM0..5, ADC, DAC, TC, TCC, DMAC, PORT, GCLK, PM, etc.
             All peripheral access is through these addresses
             Defined in lib/samd21-dfp/instance/ headers
```

The linker script (`samd21g17d_flash.ld`) defines these regions. The startup
code (`startup_samd21.c`) places the vector table at 0x00000000.

---

## Startup Sequence — What Happens Before main()

When the chip powers on or resets:

1. **Hardware reads the vector table.** The CPU reads address 0x00000000 to get
   the initial stack pointer value, then reads 0x00000004 to get the Reset_Handler
   address. The CPU jumps to Reset_Handler. This is pure hardware — no software
   involved yet.

2. **Reset_Handler runs** (in `startup_samd21.c`, provided by Microchip DFP).
   - Copies the `.data` section from flash to RAM (initialized global variables)
   - Zeros the `.bss` section in RAM (uninitialized global variables)
   - Calls `SystemInit()`
   - Calls `main()`

3. **SystemInit() runs** (in `startup/system_samd21.c`, provided by Microchip DFP).
   - Sets flash wait states (NVM_CTRLB.RWS = 1, required before running at 48 MHz)
   - Starts the DFLL48M clock locked to the internal 32 kHz oscillator
   - Switches the CPU clock (GCLK0) to DFLL48M → CPU now runs at 48 MHz

4. **main() runs.** This is where your code starts.

The startup files are provided by Microchip's DFP. They are never edited.
Understand them, but do not touch them.

Source: Microchip SAMD21 datasheet, Section 9 (System Control), Section 16 (NVMCTRL)

---

## The nEDBG — The Second Chip on Your Board

The Curiosity Nano DM320119 has two microcontrollers on it:

**SAMD21G17D** — your target. This is the chip you program. All satellite code
runs here.

**nEDBG** — a second ARM MCU pre-programmed by Microchip. You cannot reprogram it.
It presents itself to your laptop as a composite USB device with four interfaces:

| USB Interface | What it does | What you use it for |
|---|---|---|
| CMSIS-DAP (HID) | SWD debug/flash | OpenOCD connects here |
| CDC Serial | UART bridge | PuTTY connects here (your logs) |
| Mass storage | Drag-and-drop flash | Alternative to OpenOCD |
| DGI GPIO | Logic analyzer | Data Visualizer (rarely used) |

The nEDBG connects to the SAMD21G17D through:
- SWD: PA30 (SWCLK) and PA31 (SWDIO) — for programming and debugging
- UART: PA22 (SAMD21 TX) and PB22 (SAMD21 RX) — for the virtual COM port

These are physical copper traces on the PCB. They are always connected.
You do not configure the nEDBG. You configure your SAMD21G17D to use SERCOM5
on PA22 as TX, and the nEDBG automatically forwards bytes to your laptop.

Source: DM320119 User Guide, Microchip DS70005409D

---

## SERCOM Blocks

The SAMD21G17D has 6 SERCOM (Serial Communication) blocks: SERCOM0 through SERCOM5.
Each can be independently configured as UART, SPI master/slave, or I2C master/slave.

This flexibility means you can have multiple independent serial buses simultaneously.
The tradeoff is that pin muxing must be configured correctly for each SERCOM.

For this project:
- **SERCOM5** is reserved for the virtual COM port (debug logging). PA22=TX, PB22=RX.
  This is hardwired on the board — we cannot use other pins for this SERCOM.
- Other SERCOMs are available for mission use (sensors, radio, etc.)

Each SERCOM needs two clocks:
1. A **bus clock** (APB clock) — lets the CPU read/write the SERCOM's registers.
   Enabled by setting the corresponding bit in `PM->APBCMASK`.
2. A **functional clock** (GCLK channel) — the actual clock the SERCOM uses to
   generate baud rates and shift bits. Connected via the GCLK system.

Both clocks must be enabled before the SERCOM can be configured or used.
Missing either one causes register writes to be silently ignored.

Source: SAMD21 datasheet, Section 26 (SERCOM)

---

## DMA Controller (DMAC)

The SAMD21G17D has 12 DMA channels. All 12 can run simultaneously and independently
of the CPU.

DMA works through a descriptor-based system:
- A **descriptor** is a 16-byte struct in RAM containing source address, destination
  address, byte count, and control flags.
- The DMAC reads the descriptor to know what to transfer.
- Each channel has a dedicated trigger source that initiates one "beat" (one byte
  in our case) per trigger.

For UART TX: SERCOM5 fires a trigger every time its TX data register becomes empty.
The DMAC responds by reading one byte from RAM and writing it to the SERCOM5 DATA
register. The CPU is not involved between beats.

The descriptor arrays must be placed at a 16-byte aligned address in RAM:
```c
__attribute__((aligned(16)))
static DmacDescriptor dma_base_descriptors[DMAC_CH_NUM];
```

This alignment requirement is a hardware constraint documented in the SAMD21
datasheet, Section 21 (DMAC).

Source: SAMD21 DMAC application note AT07683,
        http://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-42257-SAM-Direct-Memory-Access-Controller-Driver-DMAC_ApplicationNote_AT07683.pdf
Source: Practical SAMD21 DMA tutorial:
        https://aykevl.nl/2019/09/samd21-dma

---

## Reset Cause Register

Every time the chip boots, the Power Manager (PM) records why it reset.
Reading `PM->RCAUSE.reg` at the start of `main()` tells you whether this was
a normal power-on, a watchdog reset, a software reset, or a brown-out.

```c
// Bit positions in PM->RCAUSE.reg:
// Bit 0: PM_RCAUSE_POR  — power-on reset
// Bit 1: PM_RCAUSE_BOD12 — brown-out on 1.2V internal
// Bit 2: PM_RCAUSE_BOD33 — brown-out on 3.3V supply
// Bit 4: PM_RCAUSE_EXT  — external reset pin
// Bit 5: PM_RCAUSE_WDT  — watchdog timer reset
// Bit 6: PM_RCAUSE_SYST — software reset (SCB->AIRCR)
```

A watchdog reset indicates the firmware was stuck or in an infinite loop.
This is a critical diagnostic event. Log it, count it, and include it in telemetry.

The RCAUSE register is cleared after each read. It must be read as the very
first action in `main()` before anything else modifies or resets state.

Source: SAMD21 datasheet, Section 16.6 (PM Register Summary)
