# Sensor Documentation

## How it works

The firmware reads sensor values through a three-layer abstraction. The
state machine and the MPPT algorithm at the top call exactly five
functions — `read_battery_voltage()`, `read_battery_current()`,
`read_panel_voltage()`, `read_panel_current()`, `read_charging_rail_voltage()`
— and get back one number each, in plain engineering units (millivolts or
milliamps). They never touch I²C, ADC, or any specific chip. Where the
number actually comes from is decided one layer down: a single runtime
flag picks between **injected** values (typed by the operator on the
State page, the legacy default that keeps every existing demo working)
and **real board hardware** (the chips actually soldered to the
mainboard). On the dev board the real-hardware branch silently falls
back to the injected value because no real chips exist there. One layer
further down, each physical chip has its own small file —
`ina226_battery_on_mainboard.c`, `lt6108_panel_outa1_on_mainboard.c`,
`voltage_divider_rails_on_mainboard.c`, and so on — and at the very
bottom there are two bus drivers (the existing ADC reader, and a new
I²C master on SERCOM3 PA22/PA23) that just move bytes. The PCU V4.1
board measures every current path three independent ways (INA226 over
I²C, LT6108 amplifier through ADC, TPS25940 eFuse IMON output through
ADC); v1 picks the INA226 reading on mainboard and ignores the other
two, but each redundant module is already written and ready, so adding
voting or fall-back logic later is a one-function change inside Layer 1.

Files involved (under `src-pds/sensor_inputs/` and `src/drivers/`):

| Layer | File | Purpose | Build |
|---|---|---|---|
| 1 | `sensor_inputs/sensor_readings.{c,h}` | Five `read_X()` calls + the source selector | both |
| 1 | `sensor_inputs/ina226_register_protocol.{c,h}` | Pure math: encode/decode INA226 register values, plus a built-in self-test | both |
| 2 | `drivers/ina226_battery_on_mainboard.{c,h}` | INA226 IC8, address 0x41 (initial guess), 2 mΩ shunt | mainboard |
| 2 | `drivers/ina226_panel_on_mainboard.{c,h}` | INA226 IC5, address 0x40 (initial guess), 2 mΩ shunt | mainboard |
| 2 | `drivers/voltage_divider_rails_on_mainboard.{c,h}` | OUTV1 / OUTV2 through resistor dividers | mainboard |
| 2 | `drivers/lt6108_battery_outa2_on_mainboard.{c,h}` | LT6108 IC11, ADC channel OUTA2 | mainboard |
| 2 | `drivers/lt6108_panel_outa1_on_mainboard.{c,h}` | LT6108 IC10, ADC channel OUTA1 | mainboard |
| 2 | `drivers/tps25940_imon_battery_on_mainboard.{c,h}` | TPS25940 IC7 IMON output, ADC channel BAT_IMON | mainboard |
| 2 | `drivers/tps25940_imon_panel_on_mainboard.{c,h}` | TPS25940 IC4 IMON output, ADC channel PV_IMON | mainboard |
| 3 | `drivers/i2c_master_sercom3_pa22_pa23_on_mainboard.{c,h}` | I²C master at 100 kHz, three primitives only (read register, write register, probe address) | mainboard |
| 3 | `drivers/mainboard_adc_reader.{c,h}` | Existing ADC reader, reused unchanged | mainboard |

Two existing files were also edited: the state-machine runner
(`functions_to_run_power_state_machine_with_injected_sensor_values.c`)
and the MPPT runner (`functions_to_run_mppt_algorithm_with_selected_input_source.c`)
both now call Layer 1 instead of reading injected values directly. The
hardware-startup file (`functions_to_initialize_board_hardware_before_main_loop_runs.c`)
initialises the I²C master and the two INA226 chips on the mainboard
build only.

The default sensor source on every boot is `PDS_SENSOR_SOURCE_INJECTED`
(see `pds_sensor_source_type` in `sensor_readings.h`). Switching to
`PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE` is currently a code-only call to
`set_sensor_source()`; the optional CHIPS command (id `0x3D`) and the
matching web-page button were deferred to keep tonight's scope tight.

## Tests to be made

These are the checks that confirm the three-layer split actually works.
Sorted by what is doable on the dev board (tonight) versus what needs
the real PCU V4.1 mainboard.

### Dev board (`BOARD=devboard-pds`)

These tests verify the abstraction itself without needing any real
sensor chip. Every "real-hardware" code path falls back to the injected
value on this build.

1. **Build clean.** `make BOARD=devboard-pds` produces zero warnings,
   zero errors. Already passing as of the latest commit.
2. **State page regression.** Flash the devboard, open the State page,
   run any one scenario (or the full suite via "Run all"). Every
   scenario that passed before the sensor refactor must still pass. This
   is the load-bearing devboard test — it proves the Layer 1 abstraction
   with `INJECTED` source is byte-identical to the previous direct read.
3. **MPPT page regression.** Open the MPPT page, set a curve, hit
   "Start MPPT". The duty cycle should converge the same way it did
   before the refactor, because the `ESP32_MODEL` arm of the MPPT runner
   was untouched and the `BOARD_SENSORS` arm now goes through Layer 1
   (which falls back to injected on devboard).
4. **Manual-control regression.** Open the manual-control page, enter
   manual mode, move the PWM slider, toggle the PV / BAT / LED buttons.
   Behaviour must match the existing manual-control test report — the
   manual-mode runner does not call Layer 1, so it should be unaffected,
   but a quick smoke test is cheap.
5. **INA226 register-protocol self-test (optional).** The function
   `ina226_register_protocol_built_in_self_test()` exists in
   `sensor_inputs/ina226_register_protocol.c` and runs nine
   datasheet-derived encode/decode cases. It is not yet wired into boot.
   If wired, it prints "ok" or "fail" once on every boot through the
   existing log path. This is a pure-logic check — passes or fails the
   same on devboard as on mainboard.

### Mainboard (`BOARD=mainboard-pds`) — needs the real PCU V4.1 board

These tests need the actual chips on the bench and the SERCOM3 I²C bus
physically wired. They have not been run yet.

1. **Build clean.** `make BOARD=mainboard-pds` already produces zero
   warnings; this is the only mainboard step that is verified at the
   time of writing.
2. **Boot doesn't hang.** Flashing the new firmware on the real PCU
   board must reach the main loop. The new boot calls
   `i2c_master_initialize()`, `battery_ina226_initialize()`, and
   `panel_ina226_initialize()`. If the I²C bus is wired wrong the
   initialise calls will time out (they have a bounded poll inside
   `wait_for_intflag_with_timeout`) but should not deadlock.
3. **I²C bus scan.** Add a one-shot diagnostic that calls
   `i2c_master_probe_seven_bit_address()` for every address 0x08..0x77
   and reports which ones acknowledged. Two devices should show up: the
   two INA226 chips. If the addresses are not 0x40 and 0x41, the
   addresses hard-coded in the device modules need correcting (see the
   "INITIAL GUESS, NOT YET VERIFIED" comments in the INA226 headers).
4. **INA226 chip identification.** After init, read the
   `INA226_REGISTER_MANUFACTURER_ID` register from each chip; should
   return 0x5449 (ASCII "TI"). Read `INA226_REGISTER_DIE_ID`; should
   return 0x2260. Anything else means the bus is reading garbage.
5. **Switch source to real hardware.** From the firmware (or from a new
   CHIPS command once added), call
   `set_sensor_source(PDS_SENSOR_SOURCE_REAL_BOARD_HARDWARE)`. Confirm
   that subsequent State-page telemetry shows non-zero values for
   battery and panel voltages and currents that move with the real
   board's actual electrical state instead of staying pinned to whatever
   was injected.
6. **Sanity-check INA226 against a multimeter.** Apply a known voltage
   to the panel input and a known current draw on the battery rail.
   Compare the values reported by the page against a multimeter. The
   datasheet specifies sub-1% accuracy on bus voltage; expect agreement
   to roughly 1-2% in practice once the calibration constant is right.
7. **Compare the three redundant current measurements.** While the
   board is drawing a real current, log
   `battery_ina226_read_current_ma()`,
   `battery_lt6108_read_current_ma()`, and
   `battery_tps25940_read_imon_ma()` simultaneously. They should agree
   to within a known tolerance (the LT6108 reads only the magnitude;
   the INA226 has a sign). The agreement window — and any systematic
   offset — feeds the redundancy/voting logic that will eventually live
   inside `read_battery_current()` in Layer 1.
8. **MPPT convergence with real panel current.** With sensor source set
   to real hardware, run the MPPT algorithm against a real (or
   bench-supplied) solar panel input. Duty cycle should converge to a
   value that matches the actual maximum-power-point voltage of the
   source. Compare against the simulated convergence shape from the
   ESP32-model path to spot regressions in the MPPT loop itself.

### Known caveats to record before any of these tests

- The two INA226 I²C addresses (`0x40` panel, `0x41` battery) are
  initial guesses; the bus scan in mainboard test 3 is the source of
  truth.
- The shunt resistor value (2 mΩ) for the calibration constant is
  taken from the BOM but not independently confirmed against the
  schematic's R49 / R50 placement.
- The mapping of `OUTV1` and `OUTV2` to "panel bus" and "charging
  rail" is a guess based on resistor-divider ratios; mainboard test 6
  (multimeter comparison) will resolve it.
- The I²C master driver itself (`i2c_master_sercom3_pa22_pa23_on_mainboard.c`)
  has not been exercised on real hardware. The init sequence and the
  read/write transaction shape follow the SAMD21 datasheet, but the
  baud register value omits SCL rise-time compensation. Verify SCL is
  near 100 kHz on a scope before declaring the bus correct.
