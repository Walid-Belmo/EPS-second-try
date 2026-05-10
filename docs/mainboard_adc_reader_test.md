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

The firmware returns raw 12-bit ADC counts. Nominal conversions for these
channels, including the TPS25940 eFuse IMON scaling for `PV_IMON` and
`BAT_IMON`, are documented in
[Mainboard Analog Scaling](mainboard_analog_scaling.md). Keep raw counts visible
even after adding converted values, because the conversions still need bench
confirmation.

## Safety Scope

This step is intentionally read-only.

The firmware does not drive:

- `PWM_H` / `PWM_L`
- `EN_1` / `EN_2`
- `HEATER_SW`
- `POWER_SW`

The current mainboard build still uses `pwm_buck_converter_disabled_stub.c`,
so the buck gate driver path remains disabled.

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
outv2_raw_u16_le
```

On the devboard build, this command returns `COMMAND_NOT_AVAILABLE`.

## ESP32 Bridge Command

From the ESP32 USB serial console:

```text
adc
```

Expected mainboard output format:

```text
[RX] seq=N cmd=GET_MAINBOARD_ADC rsp=1 payload_len=13
     status=0 (SUCCESS)
     raw_adc pv_imon=... bat_imon=... outa1=... outa2=... outv1=... outv2=...
     channels: PB04/AIN12 PB05/AIN13 PB06/AIN14 PB07/AIN15 PB08/AIN2 PB09/AIN3
```

Expected devboard output:

```text
[RX] seq=N cmd=GET_MAINBOARD_ADC rsp=1 payload_len=1
     status=4 (COMMAND_NOT_AVAILABLE)
```

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
6. Record the six raw ADC values.
7. Change one known bench input at a time and verify the corresponding raw ADC
   field changes while unrelated fields remain reasonably stable.

Pass criteria:

- `adc` returns `SUCCESS`.
- Every raw field stays between `0` and `4095`.
- `OUTV1` changes when the input/PV-side voltage changes.
- `OUTV2` changes when the output/battery-side voltage changes.
- `OUTA1` changes when PV/buck-input current changes.
- `OUTA2` changes when output/battery-side current changes.

`PV_IMON` and `BAT_IMON` are logged for visibility. Their nominal eFuse scaling
is documented in [Mainboard Analog Scaling](mainboard_analog_scaling.md), but
the converted current should still be checked against measured current before it
is used as a hard pass/fail value.
