# Documentation Index

Start here when looking for project documentation.

## Current Bring-Up Work

- [Mainboard ADC Reader Test](mainboard_adc_reader_test.md) - raw ADC reader
  for the six mainboard analog monitor nets and how to test it later.
- [Mainboard Analog Scaling](mainboard_analog_scaling.md) - nominal raw ADC to
  volts/amps formulas, including the TPS25940 eFuse IMON scaling for
  `PV_IMON` and `BAT_IMON`.
- [PCB Design Error For PWM](pcb_design_error_for_pwm.md) - proof that the
  mainboard routes buck PWM to `PA12/PA13`, which are not a natural TCC0
  dead-time pair, and the firmware workaround plus PWM arm/disarm gate we will
  use.
- [MPPT Test 1: Mainboard MVP](mpptest1.md) - staged real-board bring-up plan:
  ADC dry run, fixed-duty PWM, voltage regulation, then MPPT.
- [Mainboard Pinout](mainboard_pinout_pcu_v4_1.md) - verified PCU V4.1 pin map.
- [Build Targets and File Map](build_targets_and_file_map.md) - which files
  compile into devboard and mainboard builds.

## Build And Flash

- [How To Build And Flash](how_to_build_and_flash.md)
- [Flashing](flashing.md)
- [How To Flash Mainboard](how_to_flash_mainboard.md)
- [Recover From Stalled Debug Port](how_to_recover_from_stalled_debug_port.md)
- [Toolchain Setup Windows](toolchain_setup_windows.md)

## Firmware References

- [MPPT Algorithm](mppt_algorithm.md)
- [OBC UART Driver](uart_obc_driver.md)
- [DMA UART Logging](dma_uart_logging.md)
- [SAMD21 Architecture](samd21_architecture.md)
- [SAMD21 Clocks](samd21_clocks.md)
- [Newlib And Syscalls](newlib_and_syscalls.md)
- [Project Structure](project_structure.md)
- [Smoke Test](smoke_test.md)
