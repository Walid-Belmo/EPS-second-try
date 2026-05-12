# Mainboard ADC Reader Test

This document explains the read-only ADC code added for the EPS PCU testing
board V4.1 mainboard.

## What Was Added

The mainboard firmware now initializes the SAMD21 ADC and can read the six PCB
analog monitor nets:

| Signal | MCU pin | ADC channel | Meaning |
|---|---:|---:|---|
| `PV_IMON` | `PB04` | `AIN12` | PV eFuse current monitor |
| `BAT_IMON` | `PB05` | `AIN13` | battery/output eFuse current monitor |
| `OUTA1` | `PB06` | `AIN14` | PV/buck-input current monitor |
| `OUTA2` | `PB07` | `AIN15` | output/battery current monitor |
| `OUTV1` | `PB08` | `AIN2` | buck input / PV-side voltage monitor |
| `OUTV2` | `PB09` | `AIN3` | buck output / battery-side voltage monitor |

The firmware returns raw 12-bit ADC counts, ADC pin millivolts, and nominal
engineering conversions. The TPS25940 eFuse IMON scaling for `PV_IMON` and
`BAT_IMON` is documented in
[Mainboard Analog Scaling](mainboard_analog_scaling.md). Raw counts remain
visible because all converted values still need bench confirmation.

## Safety Scope

The ADC reader itself is read-only. The same mainboard demo firmware also
contains gated output drivers, so the safe default matters.

At boot, and until explicitly armed, the firmware does not drive:

- `PWM_H` / `PWM_L`

The firmware still does not drive:

- `EN_1` / `EN_2`
- `HEATER_SW`
- `POWER_SW`

The current mainboard build includes the real TCC0 PA12/PA13 PWM driver, but
PWM is now behind an explicit software arm gate:

- boot state is disarmed;
- every `mode ...` command disarms PWM again;
- injected faults disarm PWM again;
- `pwm-disarm` immediately commands zero duty and forces `PWM_H`/`PWM_L` low;
- nonzero PWM is only allowed after `pwm-arm`.

Do not use `pwm-arm` with the buck power path energized until the scope-only PWM
checks in [PCB Design Error For PWM](pcb_design_error_for_pwm.md) pass.

## Verification Sources

The pin map was rechecked against:

- private PCB repo `CHESS-mission/eps_pcu_eng`, file `MCU.kicad_sch`, read with
  `gh api` from the authenticated local GitHub CLI;
- local Microchip SAMD21 datasheet `datasheets/samd21_datasheet.pdf`,
  ADC chapter;
- vendor DFP header `lib/samd21-dfp/pio/samd21j17d.h`, which maps:
  `PB04..PB07` to `ADC_AIN12..15` and `PB08..PB09` to `ADC_AIN2..3`;
- vendor DFP ADC register definitions in `lib/samd21-dfp/component/adc.h`.

## Firmware API

Mainboard-only driver:

- `src/drivers/mainboard_adc_reader.c`
- `src/drivers/mainboard_adc_reader.h`

Public functions:

```c
void mainboard_adc_reader_initialize(void);
uint16_t mainboard_adc_reader_read_raw_channel(uint8_t adc_channel_number);
void mainboard_adc_reader_read_all_channels(mainboard_adc_readings_type *out);
```

The mainboard app calls `mainboard_adc_reader_initialize()` at boot.

## CHIPS Command

New command ID:

```text
0x27 = GET_MAINBOARD_ADC
```

Request payload:

```text
empty
```

Response payload:

```text
status,
pv_imon_raw_u16_le,
bat_imon_raw_u16_le,
outa1_raw_u16_le,
outa2_raw_u16_le,
outv1_raw_u16_le,
outv2_raw_u16_le,
pv_imon_pin_mv_u16_le,
bat_imon_pin_mv_u16_le,
outa1_pin_mv_u16_le,
outa2_pin_mv_u16_le,
outv1_pin_mv_u16_le,
outv2_pin_mv_u16_le,
pv_imon_ma_u32_le,
bat_imon_ma_u32_le,
outa1_ma_u32_le,
outa2_ma_u32_le,
outv1_mv_u32_le,
outv2_mv_u32_le,
scaling_flags_u8
```

On the devboard build, this command returns `COMMAND_NOT_AVAILABLE`.

## Main Telemetry Payload

The normal `GET_TELEMETRY` response is now telemetry protocol version `4`.
Version 3 appended the mainboard analog data block to the older version 2
control/state telemetry. Version 4 appends one more byte:

```text
pwm_armed_u8
```

This distinguishes "the controller wants PWM" from "the firmware is actually
allowed to apply nonzero PWM to the pins."

On the devboard build, the v3 analog block is present but has
`scaling_flags = 0`, meaning no real mainboard ADC readings are available.

## ESP32 Bridge Commands

One-shot ADC read from the ESP32 USB serial console:

```text
adc
```

Expected mainboard output format:

```text
[RX] seq=N cmd=GET_MAINBOARD_ADC rsp=1 payload_len=50
     status=0 (SUCCESS)
     raw_adc pv_imon=... bat_imon=... outa1=... outa2=... outv1=... outv2=...
     pin_mv pv_imon=... bat_imon=... outa1=... outa2=... outv1=... outv2=...
     converted pv_imon_ma=... bat_imon_ma=... outa1_ma=... outa2_ma=... outv1_mv=... outv2_mv=...
     scaling_flags=0xF readings=1 nominal=1 outa_provisional=1 outv_provisional=1
     channels: PB04/AIN12 PB05/AIN13 PB06/AIN14 PB07/AIN15 PB08/AIN2 PB09/AIN3
```

Expected devboard output:

```text
[RX] seq=N cmd=GET_MAINBOARD_ADC rsp=1 payload_len=1
     status=4 (COMMAND_NOT_AVAILABLE)
```

Continuous analog telemetry:

```text
adc-stream 5
```

This enables the normal telemetry stream at `5 Hz`. Each streamed telemetry
frame prints the mainboard raw ADC values, ADC pin millivolts, and converted
engineering values when connected to the mainboard firmware.

Stop streaming:

```text
adc-stop
```

The existing generic commands still work:

```text
stream on 200
stream off
telemetry
```

PWM safety commands:

```text
pwm-arm
pwm-disarm
state
```

`state` prints both `pwm` and `armed`. `pwm=1 armed=1` means the firmware is
actually applying nonzero PWM. `pwm=0 armed=1` means PWM is armed but the current
mode/input state is commanding zero duty. `pwm=0 armed=0` means the safety gate
is blocking PWM regardless of what the control algorithm computed.

## Build Checks

Use PowerShell in the repo root:

```powershell
New-Item -ItemType Directory -Force -Path .\build | Out-Null
make BOARD=mainboard
```

When switching targets, delete `build/` first or use the repo's `make clean`
from an MSYS2 shell. In plain PowerShell, the Makefile's Unix `rm -rf` clean
rule is not portable.

ESP32 compile check:

```powershell
& 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn esp32:esp32:esp32 .\esp32_test_harness\02_chips_injection_demo
```

## Real-Board Test Procedure Later

1. Power the mainboard safely using the agreed bench setup.
2. Flash the mainboard firmware built with `make BOARD=mainboard`.
3. Connect ESP32 UART to the mainboard J3 UART, with TX and RX crossed.
4. Open the ESP32 USB serial console at `115200`.
5. Send `adc`.
6. Record the six raw ADC values, the ADC pin millivolts, and the converted
   values.
7. Send `adc-stream 5` for continuous telemetry while changing bench inputs.
8. Change one known bench input at a time and verify the corresponding raw ADC
   and converted field changes while unrelated fields remain reasonably stable.
9. Send `adc-stop` when finished.

Scope-only PWM bring-up, with the buck power path not energized:

1. Send `mode fixed`.
2. Send a conservative test duty, for example `duty 20000`.
3. Confirm `state` shows `armed=0` and `pwm=0`.
4. Send `pwm-arm`.
5. Confirm `state` or `telemetry` shows `armed=1` and, after the 100 ms control
   loop tick, `pwm=1`.
6. Check `PWM_H` and `PWM_L` on the oscilloscope.
7. Send `pwm-disarm` before moving probes or changing wiring.

Pass criteria:

- `adc` returns `SUCCESS`.
- Every raw field stays between `0` and `4095`.
- Every ADC pin millivolt field stays between `0` and roughly the measured
  `VDDANA` rail.
- `OUTV1` changes when the input/PV-side voltage changes.
- `OUTV2` changes when the output/battery-side voltage changes.
- `OUTA1` changes when PV/buck-input current changes.
- `OUTA2` changes when output/battery-side current changes.

`PV_IMON` and `BAT_IMON` are logged for visibility. Their nominal eFuse scaling
is documented in [Mainboard Analog Scaling](mainboard_analog_scaling.md), but
the converted current should still be checked against measured current before it
is used as a hard pass/fail value.
