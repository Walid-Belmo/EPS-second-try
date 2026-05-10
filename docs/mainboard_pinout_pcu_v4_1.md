# Mainboard Pinout — PCU Testing Board V4.1

This document records every microcontroller pin assignment of the actual EPS
mainboard PCB ("PCU testing board V4.1"), as read directly from the KiCad
schematic at https://github.com/CHESS-mission/eps_pcu_eng (default branch
`main`). It is the authoritative reference for porting firmware that was
previously developed on the Curiosity Nano DM320119 dev board.

Every claim below carries a proof reference: the file in the PCB repo and the
exact line number where the claim can be verified. Anyone reading this should
be able to open that file in a browser, search for the quoted string, and
confirm the fact independently.

---

## How to verify anything in this document yourself

You do not need KiCad installed.

1. Open https://github.com/CHESS-mission/eps_pcu_eng on `main` in a browser.
2. Click any `.kicad_sch` file mentioned below — GitHub renders them as plain
   text. The format is S-expressions, similar to Lisp.
3. Press <kbd>Ctrl</kbd>+<kbd>F</kbd> in the browser and paste the proof
   string (e.g. `"UART_TX"`, `(at 142.24 60.96 0)`).
4. The proof for a pin connection is a wire endpoint at a specific (x, y),
   matched against the chip's pin layout. The chip layout is given in
   "How the coordinate trace works" below.

If you would rather see the schematic graphically, install KiCad (free,
https://www.kicad.org/download/) and open `testingPCU.kicad_pro`.

---

## The chip on this PCB is NOT the same as the dev board

| | Curiosity Nano dev board | PCU testing board V4.1 |
|---|---|---|
| Part number | `ATSAMD21G17D` | **`ATSAMD21J17D-MUT`** |
| Package | 48-pin TQFP | **64-pin QFN** |
| Flash | 128 KB | 128 KB |
| RAM | 16 KB | 16 KB |
| Cortex-M0+ core | yes | yes |
| SERCOMs | 6 | 6 |
| TCC0 / TCC1 / TCC2 | yes | yes |
| Reference designator on schematic | n/a | `IC12` |

Proof — `MCU.kicad_sch` line ~5081:

```
(symbol
    (lib_id "PULSE_Library:ATSAMD21J17D-MUT")
    (at 142.24 60.96 0)
    ...)
```

Proof — `MCU.kicad_sch` line ~5098:

```
(property "Value" "ATSAMD21J17D-MUT" ...)
```

Proof — `testingPCU.csv` (bill of materials):

```
IC12,ATSAMD21J17D-MUT,PULSE_Library:QFN50P900X900X100-65N-D,...
```

The two parts share the same Cortex-M0+ core, the same flash and RAM size,
and the same peripherals. The J variant has more I/O pins exposed on the
package. **No driver register code needs to change.** What needs to change
is the build configuration:

- `Makefile` define: `-D__SAMD21G17D__` → `-D__SAMD21J17D__`
- DFP header: `samd21g17d.h` → `samd21j17d.h`
- Startup file: `startup/startup_samd21g17d.c` → `startup_samd21j17d.c`
- Linker script: `samd21g17d_flash.ld` → `samd21j17d_flash.ld`

All four are vendor-supplied and must be re-extracted from the same DFP
v3.6.144 atpack already used for the dev-board firmware (see
`docs/how_to_build_and_flash.md`).

---

## How the coordinate trace works

KiCad schematic files store the chip as a **symbol** placed at an **anchor**
point on the page. Each pin of the symbol has an offset from that anchor.
Each label (named net) has its own (x, y) on the page. A pin and a label
belong to the same electrical net if a sequence of wire segments connects
their coordinates.

For the SAMD21 on this board:

- Symbol anchor: `(142.24, 60.96)` with rotation `0`.
- Coordinate convention: KiCad applies a Y-flip when placing the symbol,
  so an absolute pin position on the page is computed as

  ```
  abs_x = chip_anchor_x + pin_offset_x
  abs_y = chip_anchor_y − pin_offset_y
  ```

- Pin offsets are listed in the embedded symbol definition starting at
  about line 838 of `MCU.kicad_sch` (search for `symbol "ATSAMD21J17D-MUT_1_1"`).
- The chip's body rectangle in the symbol is from `(5.08, 15.24)` to
  `(50.8, -53.34)`. Pins on the left/right have `angle 0` / `angle 180`,
  pins on the top/bottom have `angle 270` / `angle 90`.

Concrete examples used to derive the table below:

- The pin named `PA22` has offset `(55.88, -12.7)` in the symbol → absolute
  position `(142.24 + 55.88, 60.96 − (−12.7))` = `(198.12, 73.66)`. The label
  `"SDA"` is placed at `(208.28, 73.66)`. They share Y exactly, and a wire
  joins them along the right side of the chip → `PA22` is the SDA net.

- The pin named `PB22` has offset `(48.26, 20.32)` → absolute
  `(142.24 + 48.26, 60.96 − 20.32)` = `(190.5, 40.64)`. Two wire segments
  (lines 3664 and 3084 of `MCU.kicad_sch`):

  ```
  (xy 190.5 40.64) (xy 190.5 38.1)        ; up 2.54 mm from the pin
  (xy 190.5 38.1)  (xy 199.39 38.1)       ; right 8.89 mm to a label
  ```

  end at the label `"LED2"` placed at `(199.39, 38.1)` → `PB22` is the LED2
  net.

The same procedure applies to every entry in the table.

---

## Pin map — verified

### Microcontroller signals

| Net (board) | MCU pin | SERCOM / peripheral function | Mux | Proof reference (in `MCU.kicad_sch`) |
|---|---|---|---|---|
| `UART_TX` | **PA10** | SERCOM0 PAD[2] | C | label `(at 154.94 129.54 90)` line ~4102; pin abs (154.94, 119.38) |
| `UART_RX` | **PA11** | SERCOM0 PAD[3] | C | label `(at 157.48 129.54 90)` line ~4012; pin abs (157.48, 119.38) |
| `SWCLK` | **PA30** | SWD clock (dedicated) | n/a | label `(at 170.18 30.48 270)` line ~3972 |
| `SWDIO` | **PA31** | SWD data (dedicated) | n/a | label `(at 167.64 30.48 270)` line ~4042 |
| `~{RESET}` | **RESET** pin | dedicated reset (NOT a GPIO) | n/a | label `(at 182.88 30.48 270)` line ~4022 |
| `PWM_H` | **PA12** | TCC0 / WO[6] | F | label `(at 180.34 121.92 270)` line ~3992; routes to `BUCK_HS_IN` of EPC2152 |
| `PWM_L` | **PA13** | TCC0 / WO[7] | F | label `(at 182.88 121.92 270)` line ~4062; routes to `BUCK_LS_IN` of EPC2152 |
| `LED2` (green, **active-HIGH**) | **PB22** | GPIO | n/a | wires lines 3664 + 3084; label `(at 199.39 38.1 180)` line ~3922 |
| `SDA` | **PA22** | SERCOM3 or SERCOM5 PAD[0] (I²C) | varies | label `(at 208.28 73.66 180)` |
| `SCL` | **PA23** | SERCOM3 or SERCOM5 PAD[1] (I²C) | varies | label `(at 208.28 71.12 180)` |
| `HEATER_SW` | **PA08** | GPIO output (do not drive without context) | n/a | label `(at 149.86 129.54 90)` line ~3842 |
| `POWER_SW` | **PA09** | GPIO output (do not drive without context) | n/a | label `(at 152.4 129.54 90)` line ~3772 |

The remaining MCU pins are either tied to power rails (`VDDIO_1/2/3`, `VDDIN`,
`VDDCORE`, `VDDANA`, `GNDANA`, `GND_1..4`) or are explicitly marked
`(no_connect ...)` in `MCU.kicad_sch` (lines ~2784–2900). They are not
relevant to firmware.

### Headers and connectors

| Reference | Type | Purpose | Proof |
|---|---|---|---|
| `J1` | **Tag-Connect TC2050-IDC-NL**, 10 pins, 1.27 mm pitch, surface contact pads (no soldered header — needs a TC2050 cable with pogo pins) | SWD programming + reset + 3.3 V sense | `MCU.kicad_sch` line ~4749 `(lib_id "PULSE_Library:TC2050-IDC-NL")` |
| `J3` | 1 × 4 pin header, 2.54 mm (0.1″) pitch | UART (carries `UART_TX` / `UART_RX` and likely also a power and ground pin) | `MCU.kicad_sch` line ~6471 `(property "Value" "Conn_01x04_Pin")`, footprint line ~6490 |

### LED disposition (different from the dev board)

| Designator | Colour | Wired between | MCU-controllable? | Active level |
|---|---|---|---|---|
| **LED1** | RED | `AUX_3V3` → R51 (750 Ω) → LED1 → `GND` | **NO** — power-on indicator only | always-on whenever 3.3 V is present |
| **LED2** | GREEN | `PB22` → R52 (750 Ω) → LED2 → `GND` | **YES** | **ACTIVE-HIGH** (drive PB22 high to light it) |

Proof for LED1 (column at `x = 245.11` in `MCU.kicad_sch`):
- `AUX_3V3` label at `(245.11, 64.77, 180)` line ~3803 — top of column
- `R51` 750 Ω at `(245.11, 71.12, 0)` line ~5838
- `LED1` (Device:LED, value `RED`) at `(245.11, 80.01, 90)` line ~6023
- `power:GND` symbol at `(245.11, 90.17, 0)` line ~6279 — bottom of column

Proof for LED2 (column at `x = 265.43` in `MCU.kicad_sch`):
- `LED2` net label at `(265.43, 64.77, 180)` line ~4033 (and `(199.39, 38.1, 180)` line ~3922)
- `R52` 750 Ω at `(265.43, 71.12, 0)` line ~4963
- `LED2` (Device:LED, value `GREEN`) at `(265.43, 80.01, 90)` line ~5716
- `power:GND` symbol at `(265.43, 90.17, 0)` line ~5391 — bottom of column

This is **opposite polarity** to the dev board's PB10 LED, which was active-low.
On the mainboard, drive PB22 **high** to turn LED2 on.

### Gate driver

The buck converter uses an **EPC2152** GaN half-bridge driver (reference `U1`
on `BUCK.kicad_sch`). The MCU's `PWM_H` (PA12) drives `BUCK_HS_IN`, and
`PWM_L` (PA13) drives `BUCK_LS_IN`. This is the same gate driver that the
phase 5 firmware on the dev board was developed against — the difference is
only which MCU pins drive its inputs.

---

## What this means for firmware that was tested on the dev board

| Subsystem | Dev board (`SAMD21G17D`) | Mainboard (`SAMD21J17D-MUT`) | What changes |
|---|---|---|---|
| Build config | `__SAMD21G17D__` | `__SAMD21J17D__` | Makefile define + DFP header + startup + linker |
| User LED (status) | PB10, active-LOW | **PB22, active-HIGH** | Pin number + polarity |
| Debug UART | SERCOM5 PA22, mux D, TXPO=0 | not exposed on this board | Either drop the second UART, or share `J3` for both debug and OBC |
| OBC UART | SERCOM0 PA04 (TX) / PA05 (RX), mux D, TXPO=0, RXPO=1 | **SERCOM0 PA10 (TX) / PA11 (RX), mux C, TXPO=1, RXPO=3** | Pin numbers + mux + TXPO + RXPO; same baud, same SERCOM |
| Buck PWM | PA18 (TCC0/WO[2]) + PA20 (TCC0/WO[6]), DTI pair under DTIEN2 | **PA12 (TCC0/WO[6]) + PA13 (TCC0/WO[7])**, NOT a natural DTI pair — must drive two channels (CC[2], CC[3]) and use INVEN to align them | Pin numbers + WAVE/WEXCTRL configuration |
| User button | PB11 | **none** on this PCB | Button-related test code does not apply |
| I²C | not used yet | PA22 (SDA) / PA23 (SCL) reserved for future sensor work | nothing to do for the first bring-up |

For the very first power-on test, the only pin we touch is **PB22**: configure
it as a GPIO output and toggle it to make the green LED blink. Every other
MCU pin stays in its reset state (input, no peripheral mux).

---

## Pins that must NOT be driven during the first bring-up

These pins exist on the schematic but may be wired to power-stage gates,
heater MOSFETs, or other circuitry whose downstream effects are unknown until
the board is read end-to-end. For safety, the first three test firmwares must
leave them **as inputs** (the chip's reset state):

- `HEATER_SW` (`PA08`) — likely controls a heater MOSFET
- `POWER_SW` (`PA09`) — likely an output enable for a downstream rail
- `PWM_H` (`PA12`), `PWM_L` (`PA13`) — gate driver inputs of the EPC2152;
  must remain inputs (or driven LOW with both buck-rail capacitors discharged)
  until firmware C explicitly tests them

The blink test (firmware A) uses only `PB22`. The UART echo test (firmware B)
uses only `PA10`/`PA11`. The PWM test (firmware C) is the first time
`PA12`/`PA13` are driven, and the very first run drives them at a slow,
visible rate (no risk of resonance in the converter) before any 300 kHz
operation.

---

## Quick-reference table for firmware code

```
/* Mainboard PCU V4.1 — MCU pin map (proof: docs/mainboard_pinout_pcu_v4_1.md) */
#define MAINBOARD_LED2_GREEN_PIN_NUMBER          22u   /* PB22, ACTIVE HIGH */
#define MAINBOARD_LED2_GREEN_PORT_GROUP_INDEX    1u    /* PORT group B */

#define MAINBOARD_UART_TX_PIN_NUMBER             10u   /* PA10, SERCOM0 PAD[2] mux C */
#define MAINBOARD_UART_RX_PIN_NUMBER             11u   /* PA11, SERCOM0 PAD[3] mux C */
#define MAINBOARD_UART_PORT_GROUP_INDEX          0u    /* PORT group A */
#define MAINBOARD_UART_PINMUX_FUNCTION           PORT_PMUX_PMUXE_C_Val  /* mux C */
#define MAINBOARD_UART_SERCOM0_TXPO              1u    /* TX on PAD[2] */
#define MAINBOARD_UART_SERCOM0_RXPO              3u    /* RX on PAD[3] */

#define MAINBOARD_PWM_HIGH_SIDE_PIN_NUMBER       12u   /* PA12, TCC0 WO[6] mux F */
#define MAINBOARD_PWM_LOW_SIDE_PIN_NUMBER        13u   /* PA13, TCC0 WO[7] mux F */
#define MAINBOARD_PWM_PORT_GROUP_INDEX           0u
#define MAINBOARD_PWM_PINMUX_FUNCTION            PORT_PMUX_PMUXE_F_Val  /* mux F */
```

These constants will live in a header in `src/drivers/` once the test
firmwares are written. They are reproduced here so the pin map can be
read in one place without opening source code.

---

## Open questions still to resolve

1. **Programmer hardware.** The board uses a Tag-Connect TC2050-IDC-NL
   footprint (no soldered header). To program the chip the user needs a
   TC2050-IDC-NL cable and a SWD-capable debugger (CMSIS-DAP / J-Link /
   ST-Link / Atmel-ICE / Curiosity Nano nEDBG used as external probe).
   The exact hardware on hand is not yet documented.

2. **Power during programming.** Whether the PCB is self-powered (battery,
   solar input, bench supply on the AUX rail) or whether the debugger's
   target-power pin should provide 3.3 V is not yet documented.

3. **The "message" from the team about flashing.** Once received, the
   relevant points should be merged into a new `docs/how_to_flash_mainboard.md`
   and cross-referenced from `notes/readme.md`.

These will be addressed in a follow-up document once the user provides the
information.

---

*This document was generated by direct schematic reading of the PCB repo
on 2026-04-26. Every assertion is anchored to a specific file and line
number that can be re-read at any time to confirm or refute the claim.*
