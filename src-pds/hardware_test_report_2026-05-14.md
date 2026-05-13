# Source PDS Hardware Test Report - 2026-05-14

## Setup

- SAMD21 board: Microchip Curiosity Nano dev board.
- SAMD21 firmware: `make BOARD=devboard-pds flash`.
- ESP32 bridge: `esp32_test_harness/03_source_pds_bridge/03_source_pds_bridge.ino`.
- ESP32 computer port: `COM3`, 115200 baud.
- Wiring used:

```text
ESP32 GPIO17 / TX2  -> SAMD21 PA11 / RX
ESP32 GPIO16 / RX2  <- SAMD21 PA10 / TX
ESP32 GND           <-> SAMD21 GND
```

## Result

The computer -> ESP32 -> SAMD21 -> ESP32 -> computer command path works.

The first test attempt failed because the ESP32 bridge sent commands but did
not receive any SAMD21 replies. After correcting the UART wiring, every tested
command received a valid reply from the SAMD21.

## Commands Tested

| Command | Result | What was observed |
|---|---|---|
| `get_values fields=all` | Passed | SAMD21 replied with status `success` and the full status packet. |
| `off` | Passed | SAMD21 replied with status `success`. |
| `run_pwm duty=32768` | Passed | Mode changed to `fixed_pwm_test`; reported requested and applied PWM became `32768`. |
| `start_mppt_demo ...` | Passed | Mode changed to `mppt_test`; MPPT curve values were stored correctly. |
| `stream_values on period=200 fields=all` | Passed | SAMD21 sent periodic status packets every 200 ms. |
| `stream_values off` | Passed | Periodic status packets stopped. |
| `start_state_demo ...` | Passed | Mode changed to `state_test`; injected state inputs were stored correctly. |
| `inject_state ... panel_voltage=0 panel_current=0 ...` | Passed | Injected zero solar input was accepted. |
| `get_values fields=all` after zero solar injection | Passed | PCU state changed to `BATTERY_DISCHARGE`. |

## MPPT Observation

During MPPT streaming, the reported duty cycle changed over time, which shows
that the SAMD21 was running the MPPT demo loop and updating its requested PWM.

Example observed values:

```text
mppt_duty: 32440 -> 32112 -> 31784 -> 31456
panel_voltage: around 14949 mV -> 15257 mV
panel_power: around 64370 mW -> 65422 mW
```

## State Transition Observation

The state demo started in `MPPT_CHARGE`.

After this command:

```text
inject_state battery_voltage=7200 battery_current=-300 panel_voltage=0 panel_current=0 rail_voltage=7200 temperature=220 heartbeat=1 obc_mode=charging safe_substate=charging faults=0
```

The final status reported:

```text
pcu=BATTERY_DISCHARGE
panel_efuse=0
requested_pwm=0
applied_pwm=0
pwm_enabled=0
```

## Known Limitation

For the dev-board test target, PWM uses the disabled safety driver. The firmware
reports PWM requests and applied PWM values, but the dev board does not drive
physical buck-converter PWM pins in this build.

