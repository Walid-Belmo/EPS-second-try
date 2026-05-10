# MPPT Test 1: Mainboard MVP

This document defines the first useful firmware test for Alexander's bench
setup. The goal is to prove the real PCB can generate buck PWM, read the PCB's
analog measurement outputs, and log enough data to support the first MPPT
experiment.

This is not the full flight EPS firmware. It is a controlled bring-up firmware
for the PCU testing board V4.1.

---

## Goal

Build firmware that runs on the real mainboard MCU and does four things:

1. Generate PWM for the buck converter on the real PCB pins.
2. Read the PCB-provided analog measurement signals with the SAMD21 ADC.
3. Convert ADC readings into voltage/current estimates once the scaling
   constants are verified.
4. Log the measurements and duty cycle so the bench test can be inspected.

The first closed-loop target is a simple output-voltage regulator. MPPT comes
after that, because MPPT needs the same two foundations: reliable PWM control
and reliable input/output measurement.

---

## Confirmed Hardware Model

The PCB already contains analog measurement circuits. Firmware does not create
these measurement signals. Firmware only reads them.

The physical chain is:

```text
real voltage/current on the power stage
-> PCB analog measurement component
-> small MCU-safe analog voltage
-> SAMD21 ADC pin
-> raw ADC number
-> firmware scaling formula
-> voltage/current estimate
```

The INA226 path is different: it is a digital I2C sensor path. For this MVP we
do not rely on INA226, because the current PCB BOM marks `IC5,IC8`
`INA226AIDGSR` as `Excluded from BOM`.

Proof:
https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.csv#L45

---

## Measurement Sources

### Voltage: `OUTV1` and `OUTV2`

These come from voltage-divider circuits in `CTRL.kicad_sch`.

Confirmed proof:

- The schematic explicitly says the divider is used for MCU monitoring:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L1185
- `CTRL_OUTV1` is exported by the CTRL sheet:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L3055
- `CTRL_OUTV2` is exported by the CTRL sheet:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L2989
- The top-level sheet routes the CTRL outputs to MCU-facing nets `OUTV1` and
  `OUTV2`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L5270
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.kicad_sch#L5280

Working interpretation:

- `OUTV1` is the buck input / PV-side voltage measurement.
- `OUTV2` is the buck output / battery-side voltage measurement.

Working scaling hypothesis from the schematic:

- `OUTV1` appears to use divider `R32 = 100K` and `R33 = 3.6K`.
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L4815
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L4126
- `OUTV2` appears to use divider `R36 = 100K` and `R37 = 11.8K`.
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L3108
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L6490

If those net traces are correct, the firmware conversion is:

```text
OUTV1_real = OUTV1_adc_pin_voltage * (100K + 3.6K) / 3.6K
           = OUTV1_adc_pin_voltage * 28.7778

OUTV2_real = OUTV2_adc_pin_voltage * (100K + 11.8K) / 11.8K
           = OUTV2_adc_pin_voltage * 9.4746
```

Open item before hardcoding scaling:

- Verify the exact divider ratios for `OUTV1` and `OUTV2` from the schematic
  and with a bench measurement. The firmware can read raw ADC values before
  this is finished, but correct volts require the final ratios. This matters
  because another battery-side divider, `R34 = 100K` and `R35 = 8.45K`, also
  exists in the same CTRL schematic area and should not be accidentally used
  for the exported `OUTV2` scaling unless the net trace proves it.

### Current: `OUTA1` and `OUTA2`

These come from `LT6108IMS8-1#PBF` current-sense amplifiers in
`CTRL.kicad_sch`. These are analog current-sense parts, not I2C sensors.

Confirmed proof:

- The BOM lists `IC10,IC11` as `LT6108IMS8-1#PBF`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.csv#L13
- `IC10` and `IC11` are placed in the CTRL schematic:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L5120
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L5258
- The schematic note gives the intended current-sense design values:
  `Rshunt = 10 mOhm`, `R_OUT ~= 750 Ohm`, `R_IN = 100 Ohm`.
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L1277
- The CTRL sheet exports `CTRL_OUTA1` and `CTRL_OUTA2`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L3000
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L2978

Working interpretation:

- `OUTA1` is PV-side / buck-input-side current after the eFuse.
- `OUTA2` is output / battery-side current.

For the first MPPT test, use `OUTA1` and `OUTA2` as the current measurements.
Do not start with `PV_IMON` or `BAT_IMON` unless `OUTA1/OUTA2` fail on the
bench.

Exact component values found so far:

| Signal | Active part | Sense resistor | `R_IN` | `R_OUT` | MCU ADC pin |
|---|---|---|---|---|---|
| `OUTA1` | `IC10` `LT6108IMS8-1#PBF` | `R17 = 10mOhm` on PV sheet | `R41 = 100Ohm` | `R42 = 750Ohm` | `PB06` / `AIN14` |
| `OUTA2` | `IC11` `LT6108IMS8-1#PBF` | `R29 = 10mOhm` on BAT sheet | `R44 = 100Ohm` | `R45 = 750Ohm` | `PB07` / `AIN15` |

Proof for those resistor values:

- `R17 = 10mOhm`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/PV.kicad_sch#L2993
- `R29 = 10mOhm`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/BAT.kicad_sch#L4156
- `R41 = 100Ohm`, `R42 = 750Ohm`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L3346
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L5395
- `R44 = 100Ohm`, `R45 = 750Ohm`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L4444
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/CTRL.kicad_sch#L3465

If the LT6108 output is wired as expected, the firmware conversion is:

```text
OUTA_adc_pin_voltage = raw_adc / adc_max_count * adc_full_scale_voltage

current = OUTA_adc_pin_voltage * R_IN / (R_SENSE * R_OUT)
        = OUTA_adc_pin_voltage * 100 / (0.010 * 750)
        = OUTA_adc_pin_voltage * 13.333 A/V
```

Example: if `OUTA1` reads `0.300 V` at the ADC pin, the estimated current is
approximately `4.0 A`.

Open item before hardcoding scaling:

- Confirm the exact sense-resistor placement and sign convention on the bench.
  We need to know whether increasing physical current always increases ADC
  voltage and whether zero current has any offset worth subtracting.

### Current Backup: `PV_IMON` and `BAT_IMON`

These come from the `IMON` pins of the TPS25940 eFuse chips.

Confirmed proof:

- The BOM lists `IC4,IC7` as `TPS25940ARVCR` eFuses:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/testingPCU.csv#L12
- The TPS25940 symbol contains an `IMON` pin:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/PV.kicad_sch#L986
- The PV sheet exports `PV_IMON`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/PV.kicad_sch#L2895
- The BAT sheet exports `BAT_IMON`:
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/BAT.kicad_sch#L2977

Working interpretation:

- `PV_IMON` gives an eFuse-side estimate of PV current.
- `BAT_IMON` gives an eFuse-side estimate of battery/output current.
- These are useful as backup measurements and cross-checks.

Reason they are not the first MVP current source:

- The pin spreadsheet says the IMON conversion uses `R_IMON = 10K`.
- The schematic trace appears to show `PV_IMON` using `R12 = 12.1K` and
  `BAT_IMON` using `R24 = 12.1K`.
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/PV.kicad_sch#L4241
  https://github.com/CHESS-mission/eps_pcu_eng/blob/4747eadfdc23fe6b2538e0610939b4853921e87c/BAT.kicad_sch#L5180

That mismatch does not block the MVP, because `OUTA1/OUTA2` have a clearer
LT6108 resistor chain. It only needs to be resolved if we decide to use
`PV_IMON/BAT_IMON` as active measurements instead of backup/cross-check signals.

---

## Mainboard Pins

The pin mapping below comes from `C:\Users\iceoc\Downloads\pin_definition.xlsx`
and the SAMD21J17D DFP pin definitions.

| Signal | MCU pin | ADC channel | Meaning |
|---|---:|---:|---|
| `PV_IMON` | `PB04` | `AIN12` | PV eFuse current monitor |
| `BAT_IMON` | `PB05` | `AIN13` | BAT eFuse current monitor |
| `OUTA1` | `PB06` | `AIN14` | PV/buck-input current monitor |
| `OUTA2` | `PB07` | `AIN15` | output/battery current monitor |
| `OUTV1` | `PB08` | `AIN2` | buck input / PV-side voltage |
| `OUTV2` | `PB09` | `AIN3` | buck output / battery-side voltage |

PWM pins:

| Signal | MCU pin | Peripheral intent |
|---|---:|---|
| `PWM_H` | `PA12` | TCC0 waveform output for buck high-side input |
| `PWM_L` | `PA13` | TCC0 waveform output for buck low-side input |

Optional control pins:

| Signal | MCU pin | Important logic |
|---|---:|---|
| `EN_1 (PV)` | `PA16` | high-Z enables eFuse, drive `0` disables |
| `EN_2 (BAT)` | `PA17` | high-Z enables eFuse, drive `0` disables |

Do not casually drive the eFuse enable pins high. The pin definition says the
enabled state is high-Z through a pull-up, and the disabled state is an MCU
pull-down.

---

## Firmware MVP Behavior

The first test firmware should have three modes, in this order.

### Mode 1: ADC and UART dry run

Purpose: prove the MCU can read the measurement pins without driving the buck.

Implementation status: first raw-read version is implemented in
`src/drivers/mainboard_adc_reader.c` and exposed through CHIPS command
`GET_MAINBOARD_ADC` / ESP32 console command `adc`. See
`docs/mainboard_adc_reader_test.md`.

Expected behavior:

- Configure ADC on `PB04` through `PB09`.
- Keep PWM disabled.
- Print raw ADC readings over UART.
- Confirm raw ADC values move when Alexander changes bench voltages/currents.

Pass condition:

- UART prints stable raw readings.
- `OUTV1` changes when the input-side voltage changes.
- `OUTV2` changes when the output-side voltage changes.
- `OUTA1` changes when PV-side / buck-input current changes.
- `OUTA2` changes when output / battery-side current changes.
- `PV_IMON` and `BAT_IMON` may also be logged, but they are not required to
  pass the first test.

### Mode 2: Fixed-duty PWM

Purpose: prove the real MCU pins drive the buck gate input path.

Expected behavior:

- Configure `PA12`/`PA13` for buck PWM.
- Start at zero duty or a very low duty.
- Allow fixed duty values selected at compile time or through a simple UART
  command.
- Log duty plus raw ADC readings.

Pass condition:

- Alexander sees the expected PWM on the scope.
- Increasing duty produces the expected change in `OUTV2`.
- No fault pins trip during the low-duty test.

### Mode 3: Simple output-voltage regulation

Purpose: prove the firmware can close a basic control loop before MPPT.

Expected behavior:

- Read `OUTV2`.
- Compare it to a configurable voltage target.
- If `OUTV2` is low, increase duty slightly.
- If `OUTV2` is high, decrease duty slightly.
- Clamp duty to a safe range.
- Log target, duty, `OUTV2`, current estimate, and fault state.

Important:

- The target must be configurable. Do not treat `8.4 V` as guaranteed until
  Alexander confirms the safe bench target. Earlier bench notes mentioned an
  output range closer to `6.1 V` to `7.7 V`, so hardcoding `8.4 V` would be
  risky.

Pass condition:

- The output voltage moves toward the target without unstable oscillation.
- Duty remains inside the configured safe limits.
- Fault pins remain inactive.

---

## MPPT Test After The MVP

Once PWM and ADC measurements are trusted, the MPPT experiment should use the
project's existing Incremental Conductance algorithm, not a new Perturb and
Observe implementation.

Existing algorithm reference:

- `C:\Users\iceoc\Documents\EPS-mppt-algorithm\src\mppt_algorithm.c`
- `C:\Users\iceoc\Documents\EPS-mppt-algorithm\src\mppt_algorithm.h`

The existing algorithm:

- takes two raw 12-bit ADC readings: solar panel voltage and solar panel current;
- uses an 8-sample moving average;
- compares incremental conductance without division:

```text
delta_I * V   versus   -I * delta_V
```

- outputs a new `uint16_t` duty cycle in the range `0..65535`;
- clamps duty cycle to `5%..95%`;
- uses a `0.5%` duty-cycle step.

For this mainboard test, the expected MPPT inputs are:

```text
voltage_raw_adc_reading = raw_OUTV1
current_raw_adc_reading = raw_OUTA1
```

`OUTV2` and `OUTA2` are still useful, but mainly for output-side logging,
sanity checks, battery-side power estimate, and later safety/regulation logic:

```text
input-side relative power  = raw_OUTV1 * raw_OUTA1
output-side relative power = raw_OUTV2 * raw_OUTA2
```

The important point: the MPPT decision itself should come from the existing
`mppt_algorithm_run_one_iteration()` function. The first mainboard firmware
should only provide the real ADC readings and apply the returned duty cycle to
the real PWM peripheral.

---

## Hypotheses To Verify With Alexander

These are not to be silently assumed in code:

1. `OUTV2` is the correct feedback signal for the output-voltage regulator.
2. The first safe voltage target is known. It may be `8.4 V`, but this needs
   confirmation against the real bench setup.
3. The INA226 chips are not relied on unless Alexander confirms they are
   populated on the PCB.
4. The eFuse enable behavior is high-Z equals enabled, MCU-driven low equals
   disabled.
5. `OUTA1` and `OUTA2` are acceptable as the first current measurements for
   MPPT test 1.
6. The `OUTV1/OUTV2` voltage-divider ratios above are the right ones for the
   exported MCU signals.
7. The `OUTA1/OUTA2` current-sense formula above is the right one for the
   exported MCU signals.
8. The sign and offset of the current measurements are acceptable for control.
9. The `PV_IMON/BAT_IMON` resistor mismatch only needs to be resolved if we use
   them instead of `OUTA1/OUTA2`.
10. The UART header is available for logging during the bench test.

---

## Code To Write

The MVP code should reuse existing hardware-independent code wherever possible.
We only add mainboard-specific driver modules where the old dev-board code
hardcodes pins, muxes, or peripheral wiring that are different on the real PCB.
That keeps the dev-board build working while giving the mainboard its own
correct pin setup.

Expected modules:

- Mainboard ADC driver for `PB04` to `PB09`.
- Mainboard buck PWM driver for `PA12`/`PA13`.
- Mainboard UART driver for `PA10`/`PA11`, if logging through J3 is used.
- Small test application, likely `src/main_mainboard_mppt_test_1.c`.

Expected output format:

```text
mode,duty,raw_outv1,raw_outa1,raw_outv2,raw_outa2,raw_pv_imon,raw_bat_imon,vin_mv,iin_ma,vout_mv,iout_ma,pin_mw,pout_mw
```

The first version may print raw ADC readings only. Converted fields can be
filled once scaling constants are verified.

---

## How To Verify The Schematic Yourself

Open the PCB repo:

```text
https://github.com/CHESS-mission/eps_pcu_eng
```

Use commit:

```text
4747eadfdc23fe6b2538e0610939b4853921e87c
```

Without KiCad:

1. Open the `.kicad_sch` file in GitHub.
2. Press `Ctrl+F`.
3. Search for `CTRL_OUTV1`, `CTRL_OUTV2`, `CTRL_OUTA1`, `CTRL_OUTA2`,
   `PV_IMON`, `BAT_IMON`, `IC10`, `IC11`, `IC4`, and `IC7`.

With KiCad:

1. Clone `https://github.com/CHESS-mission/eps_pcu_eng`.
2. Open `testingPCU.kicad_pro`.
3. Open the top-level schematic.
4. Enter the `CTRL`, `PV`, `BAT`, and `MCU` sheets.
5. Trace the nets named above visually.
