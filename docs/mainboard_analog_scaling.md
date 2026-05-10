# Mainboard Analog Scaling

This document collects the nominal conversions from raw SAMD21 ADC counts to
engineering values for the EPS PCU V4.1 analog monitor signals.

The firmware should continue to report raw ADC counts as well as any converted
values. The formulas below are nominal design conversions; final pass/fail
thresholds still need bench calibration against a multimeter, oscilloscope, or
known electronic load.

## ADC Count To Pin Voltage

The mainboard ADC reader currently configures the SAMD21 ADC so the measured
pin voltage is intended to span approximately the board `VDDANA` rail.

Use:

```text
V_ADC = raw_adc / 4095 * VDDANA
```

For quick bench calculations, use `VDDANA = 3.3 V` unless the rail is measured
more precisely.

## PV_IMON And BAT_IMON EFuse Current Scaling

The PCB uses TI `TPS25940ARVCR` eFuses:

| Signal | PCB source | eFuse | IMON resistor |
|---|---|---|---:|
| `PV_IMON` | `PV.kicad_sch` | `IC4` | `R12 = 12.1 kOhm` |
| `BAT_IMON` | `BAT.kicad_sch` | `IC7` | `R24 = 12.1 kOhm` |

The TI TPS25940 datasheet gives the IMON relation as:

```text
V_IMON = (I_LOAD * GAIN_IMON + I_IMON_OS) * R_IMON
```

with:

```text
GAIN_IMON = 52 uA/A
I_IMON_OS = 0.8 uA typical
R_IMON = 12.1 kOhm on this PCB
```

So on this board:

```text
V_IMON = I_LOAD * 0.6292 V/A + 0.00968 V
I_LOAD = (V_IMON - 0.00968 V) / 0.6292
```

If `VDDANA = 3.3 V`, the direct raw ADC conversion is:

```text
I_LOAD_A = raw_adc * 0.0012808 - 0.0154
```

Clamp negative displayed currents to zero. The small negative value near zero
comes from subtracting the typical IMON offset.

Approximate ADC counts at `VDDANA = 3.3 V`:

| Load current | Expected raw ADC |
|---:|---:|
| 0 A | 12 |
| 1 A | 793 |
| 2 A | 1574 |
| 3 A | 2354 |
| 4 A | 3135 |
| 5 A | 3915 |

The 3.3 V ADC range saturates near `5.23 A` with this resistor value.

## Accuracy And Calibration

Do not treat the computed current as exact before calibration.

Known error sources:

- TI specifies the IMON current monitor as a precision monitor, but still with
  finite accuracy; the product page lists IMON current indicator output at
  about `+/-8%`.
- `I_IMON_OS = 0.8 uA` is a typical offset, not a calibrated per-board value.
- The conversion depends directly on the real `VDDANA` voltage used by the ADC.
- `R12` and `R24` are precise `0.1%` resistors, so they are not expected to be
  the dominant error term.

Recommended firmware behavior:

- always expose the raw ADC count;
- expose nominal converted current for visibility;
- keep any protection or presentation thresholds conservative until one board is
  checked against real measured current.

## Relation To Other Analog Signals

`PV_IMON` and `BAT_IMON` come from the TPS25940 eFuse IMON pins.

`OUTA1` and `OUTA2` are separate current-sense amplifier outputs, so their
conversion must use the current-sense amplifier model and its resistor network,
not the eFuse formula above.

`OUTV1` and `OUTV2` are voltage-divider outputs, so their conversion must use
the divider resistor values.

## Sources

- TI TPS25940 datasheet: <https://www.ti.com/lit/gpn/TPS25940>
- PCB repository `CHESS-mission/eps_pcu_eng`:
  - `PV.kicad_sch`: `IC4 = TPS25940ARVCR`, `R12 = 12.1 kOhm`
  - `BAT.kicad_sch`: `IC7 = TPS25940ARVCR`, `R24 = 12.1 kOhm`
  - `production/PCU_ENG_bom.csv`: confirms `IC4, IC7` and `R12, R24`
