# PCB Design Error For PWM

This document records a firmware-critical PCB design error in the EPS PCU V4.1
PWM routing.

## Verdict

The PCB routes the buck converter PWM inputs to:

| Buck signal | MCU pin | SAMD21 function |
|---|---|---|
| `PWM_H` / `BUCK_HS_IN` | `PA12` | `TCC0/WO[6]` |
| `PWM_L` / `BUCK_LS_IN` | `PA13` | `TCC0/WO[7]` |

Both pins are valid TCC0 waveform outputs, but they are not a natural TCC0
dead-time pair. For a synchronous buck converter, this is the wrong pin-pair
choice because the simplest and safest TCC0 hardware dead-time mode cannot be
used directly on `WO[6]` and `WO[7]` together.

This does not make the board unusable, but it forces a more delicate firmware
workaround that must be verified on an oscilloscope before the power stage is
energized.

## What The PCB Did

The PCB repository commit checked for this document is:

```text
CHESS-mission/eps_pcu_eng @ 4747eadfdc23fe6b2538e0610939b4853921e87c
```

In the MCU sheet, the MCU is `ATSAMD21J17D-MUT`:

- `MCU.kicad_sch` places `IC12` as `ATSAMD21J17D-MUT` at
  [lines 5081-5090](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L5081-L5090).

The MCU symbol defines:

- `PA12` at
  [lines 1588-1598](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L1588-L1598).
- `PA13` at
  [lines 1624-1634](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L1624-L1634).

The same MCU sheet connects those pins to the PWM nets:

- `PA12` is wired vertically to the `PWM_H` output net at
  [lines 3732-3734](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L3732-L3734),
  labelled `PWM_H` at
  [lines 3992-3993](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L3992-L3993),
  and exported as hierarchical output `PWM_H` at
  [lines 4134-4136](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L4134-L4136).
- `PA13` is wired vertically to the `PWM_L` output net at
  [lines 3372-3374](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L3372-L3374),
  labelled `PWM_L` at
  [lines 4062-4063](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L4062-L4063),
  and exported as hierarchical output `PWM_L` at
  [lines 4222-4224](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/MCU.kicad_sch#L4222-L4224).

At top level, those nets are connected to the buck sheet inputs:

- The MCU sheet exposes `PWM_H` and `PWM_L` at
  [testingPCU.kicad_sch lines 5747-5758](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L5747-L5758).
- `PWM_H` is labelled at
  [lines 2975-2976](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L2975-L2976)
  and wired to the buck sheet `BUCK_HS_IN` pin at
  [lines 1805-1808](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L1805-L1808)
  and
  [lines 5534-5535](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L5534-L5535).
- `PWM_L` is labelled at
  [lines 2655-2656](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L2655-L2656)
  and wired to the buck sheet `BUCK_LS_IN` pin at
  [lines 2175-2178](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L2175-L2178)
  and
  [lines 5524-5525](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L5524-L5525).

The buck sheet uses an EPC2152:

- `BUCK.kicad_sch` identifies the part as `EPC2152` and links its datasheet at
  [lines 1693-1730](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/BUCK.kicad_sch#L1693-L1730).
- The buck sheet exposes `BUCK_HS_IN` and `BUCK_LS_IN` as inputs at
  [lines 1409-1422](https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/BUCK.kicad_sch#L1409-L1422).

## Why This Is A Design Error

The SAMD21J17D pin function table says:

- `PA12` function `F` is `TCC0/WO[6]`.
- `PA13` function `F` is `TCC0/WO[7]`.

This is confirmed in the local Microchip DFP header:

- `PIN_PA12F_TCC0_WO6`, `MUX_PA12F_TCC0_WO6`, and
  `PORT_PA12F_TCC0_WO6` are defined in
  [lib/samd21-dfp/pio/samd21j17d.h](../lib/samd21-dfp/pio/samd21j17d.h)
  lines `1490-1493`.
- `PIN_PA13F_TCC0_WO7`, `MUX_PA13F_TCC0_WO7`, and
  `PORT_PA13F_TCC0_WO7` are defined in
  [lib/samd21-dfp/pio/samd21j17d.h](../lib/samd21-dfp/pio/samd21j17d.h)
  lines `1510-1513`.

The current Microchip SAMD21/DA1 errata was also checked. It has a TCC0
`WO[6]`/`WO[7]` availability warning, but that warning is specifically for the
`PA16`/`PA17` alternate mapping, not for `PA12`/`PA13`.

The SAMD21 TCC dead-time insertion unit does not pair arbitrary waveform
outputs. TCC0 has eight waveform outputs, and each dead-time generator pairs a
lower output with the corresponding upper output:

| Dead-time slice | Natural pair |
|---|---|
| `DTI0` | `WO[0]` and `WO[4]` |
| `DTI1` | `WO[1]` and `WO[5]` |
| `DTI2` | `WO[2]` and `WO[6]` |
| `DTI3` | `WO[3]` and `WO[7]` |

Therefore:

- `WO[6]` naturally pairs with `WO[2]`, not `WO[7]`.
- `WO[7]` naturally pairs with `WO[3]`, not `WO[6]`.

The PCB selected `WO[6]` and `WO[7]`, which are both upper-half outputs from
two different dead-time slices. That is the design error.

The correct PCB routing would have used one complete natural pair, for example:

```text
WO[2] + WO[6]
```

or:

```text
WO[3] + WO[7]
```

The already-tested dev-board PWM path used the natural `WO[2] + WO[6]` pair.
The mainboard cannot use that directly because the PCB did not route the
corresponding `WO[2]` or `WO[3]` partner pin to the EPC2152.

## Why It Matters Electrically

The EPC2152 is the GaN half-bridge power stage for the buck converter. Its
inputs are high-side and low-side gate commands. A synchronous buck converter
must never command both switches on at the same time. The required waveform is:

```text
high-side off
short both-off interval
low-side on
low-side off
short both-off interval
high-side on
```

The short both-off interval is the dead time.

The EPC2152 includes input lockout behavior, but that must be treated as backup
protection, not as the normal timing mechanism. The MCU firmware must still
generate non-overlapping PWM before the power stage is energized.

## Firmware Workaround

We will still use TCC0. We will not use software delays, GPIO toggling, or two
independent timers.

The workaround is to use two TCC0 dead-time slices that share the same counter
and the same period. We also use the TCC `SWAP` unit, because `SWAP` is before
the physical port output and preserves the DTI both-off gap. A plain post-DTI
`INVEN` inversion can flip the waveform, but it also flips the inserted
dead-time low interval into a high interval, so it is not the preferred safety
mechanism for the first implementation.

```text
PA12 = TCC0/WO[6] = output of DTI2, with SWAP2 enabled
PA13 = TCC0/WO[7] = output of DTI3, unswapped
```

Firmware configuration:

```text
TCC0 period = 300 kHz period
CC[2] = duty compare value
CC[3] = same duty compare value
WAVE.SWAP2 = 1
WAVE.SWAP3 = 0
WEXCTRL.DTIEN2 = 1
WEXCTRL.DTIEN3 = 1
WEXCTRL.DTLS = selected dead-time count
WEXCTRL.DTHS = selected dead-time count
```

Duty updates must write both buffered compare registers:

```text
CCB[2] = next_compare
CCB[3] = next_compare
```

The goal is to make `PA12` and `PA13` behave like a complementary pair even
though they are not one natural pair.

Initial polarity assumption:

- `PA12` / `PWM_H` gets the DTI2 low-side-shaped waveform via `SWAP2`, so it is
  high during the commanded duty window.
- `PA13` / `PWM_L` gets the DTI3 high-side-shaped waveform, so it is high during
  the complementary freewheel window.

If the oscilloscope proves the board labels need the opposite polarity, the
firmware should change the SWAP choice, not replace this with software timing.

## Test Requirements Before Powering The Buck Stage

This workaround must be proved on an oscilloscope before the buck stage is used
with meaningful input energy.

Required checks:

1. `PWM_H` and `PWM_L` are at 300 kHz.
2. Both outputs are low at boot and after `duty = 0`.
3. At low duty, medium duty, and high duty, both outputs are never high at the
   same time.
4. Both switching edges contain measurable both-off dead time.
5. Updating duty does not produce a one-cycle glitch.
6. Disarming PWM forces both pins low as GPIO outputs.

Until those checks pass, this PWM driver must be treated as a scope-only bring-up
feature, not as a power-stage-ready controller.

## Firmware Safety Gate

The mainboard firmware does not allow nonzero PWM immediately after boot. The
CHIPS demo command layer has a separate software arm gate:

- boot default: `pwm_armed = 0`;
- every `mode ...` change resets `pwm_armed = 0`;
- any injected fault resets `pwm_armed = 0`;
- `pwm-disarm` sends `SET_PWM_ARM = 0` and immediately commands zero duty;
- `pwm-arm` sends `SET_PWM_ARM = 1` and only then allows the control loop duty
  to reach the TCC0 PWM driver.

This is not a replacement for oscilloscope validation. It only prevents an
accidental nonzero PWM command while setting up tests.

## Sources

- PCB repository commit:
  <https://github.com/CHESS-mission/eps_pcu_eng/tree/4747eadfdc23fe6b2538e0610939b4853921e87c>
- Microchip SAMD21/DA1 family datasheet:
  <https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/DataSheets/SAM-D21-DA1-Family-Data-Sheet-DS40001882H.pdf>
- Microchip SAMD21/DA1 family silicon errata:
  <https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/SAM-D21DA1-Family-Silicon-Errata-and-Data-Sheet-Clarification-DS80000760.pdf>
- EPC2152 datasheet:
  <https://epc-co.com/epc/Portals/0/epc/documents/datasheets/EPC2152_datasheet.pdf>
- Internal detailed timing analysis:
  [research_logs/agent_F_tcc0_dual_channel_complementary_pwm.md](../research_logs/agent_F_tcc0_dual_channel_complementary_pwm.md)
