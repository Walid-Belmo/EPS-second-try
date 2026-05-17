# Manual Control Mode

A new firmware mode (`PDS_REQUESTED_MODE_MANUAL = 5`) that lets an operator
drive each of the four user-facing board outputs independently from a web
page, without the EPS state machine or the MPPT loop being involved.

## What the operator can drive

| # | Output | MCU pin | Mainboard target | Devboard target |
|---|---|---|---|---|
| 1 | PWM duty cycle | PA12 + PA13 (TCC0) | drives the EPC2152 buck gate driver | drives the same TCC0 channels (with the disabled-PWM stub linked in `devboard-pds`) |
| 2 | Solar-panel switch (PV eFuse enable) | PA16 | gates TPS25940 IC4 via the CTRL block | pin not wired |
| 3 | Battery switch (BAT eFuse enable) | PA17 | gates TPS25940 IC7 via the CTRL block | pin not wired |
| 4 | Status LED | PB22 | drives the green LED2 active-HIGH | pin not wired |

## Commands on the wire

Five new CHIPS commands. The operator's web page sends the equivalent text
commands to the ESP32 bridge, which translates them into binary CHIPS
frames before forwarding to the SAMD21.

| Text command (PC → ESP32) | CHIPS ID (ESP32 → SAMD21) | Payload |
|---|---|---|
| `enter_manual` | `0x38` | empty |
| `set_manual_pwm duty=NNNNN` | `0x39` | u16 little-endian duty 0..65535 |
| `set_manual_pv on\|off` | `0x3A` | one byte, 0 or 1 |
| `set_manual_bat on\|off` | `0x3B` | one byte, 0 or 1 |
| `set_manual_led on\|off` | `0x3C` | one byte, 0 or 1 |

`enter_manual` is accepted from any mode and forces every manual output
back to a known safe state on entry (PWM = 0, PV off, BAT off, LED off).
The four `set_manual_*` commands are accepted only while the firmware is
already in `PDS_REQUESTED_MODE_MANUAL`; outside that mode they are
rejected with `CHIPS_RESPONSE_STATUS_COMMAND_NOT_AVAILABLE`. The web
page enforces an "Off first" convention before letting the operator click
**Enter Manual Mode**, mirroring how the MPPT and State pages behave.

## Telemetry it adds to every status reply

Status payload version is bumped from 2 to 3. Eleven additional bytes are
appended to every `get_values` and `stream_values` reply, regardless of
mode:

```
manual_pwm_duty_cycle           u16   # operator-requested duty
manual_pv_switch_requested      u8    # 0 or 1
manual_bat_switch_requested     u8    # 0 or 1
manual_status_led_requested     u8    # 0 or 1
status_led_is_on                u8    # actual PB22 level (= requested on devboard)
pv_efuse_power_good             u8    # PA18 sample (= requested on devboard)
pv_efuse_fault_active           u8    # !PA20 sample (always 0 on devboard)
bat_efuse_power_good            u8    # PA19 sample (= requested on devboard)
bat_efuse_fault_active          u8    # !PA21 sample (always 0 on devboard)
```

The ESP32 bridge prints this block as two human-readable lines so the
Python web app can pattern-match them:

```
[BOARD] manual pwm=NNNNN pv_req=0/1 bat_req=0/1 led_req=0/1 led_is_on=0/1
[BOARD] efuse pv_pgood=0/1 pv_flt=0/1 bat_pgood=0/1 bat_flt=0/1
```

## Devboard test status — important

This mode was tested on the **Microchip Curiosity Nano dev board
(SAMD21G17D), not on the real PCU testing board V4.1 (SAMD21J17D-MUT).**
The dev board does not expose PA16, PA17, PA18, PA19, PA20, PA21, or PB22
to anything useful, so on `BOARD=devboard-pds` the only output that
physically moves is the PWM pair.

The operator-facing demo still works end-to-end on the dev board because
**the firmware mirrors every requested value into the snapshot before the
real GPIO write would happen**. The mirror is unconditional; the real
GPIO writes and the real PA18-21 sampling are guarded by
`#ifdef __SAMD21J17D__` and only run on the mainboard build. Result:

- On the dev board, toggling a switch on the page sends the command, the
  firmware acknowledges it, and the page sees `pv_pgood`/`bat_pgood`
  follow the requested switch state. The pin itself doesn't move because
  it isn't wired.
- On the real board, the same command flow happens, the GPIO actually
  drives the eFuse, and the next loop iteration's call to
  `read_efuse_status_inputs_from_pa18_to_pa21()` overwrites the simulated
  power-good values with the real samples from PA18/PA19 (and the
  inverted PA20/PA21 fault flags).

In short: the visible UI behaviour is identical between the two builds,
but only the mainboard build actually wiggles the eFuse and LED pins.
PWM is the only output that physically moves on both builds.

## Files

Firmware (in `src-pds/`):

- `board_command_contract/board_command_ids_and_payload_layouts.h` — the
  five new command IDs, the new mode enum value, and the new
  `PDS_VALUE_FIELD_MANUAL` field bit.
- `runtime_state/structures_that_describe_pds_runtime_state.h` — four new
  `manual_*` request fields and five new snapshot fields for the
  readbacks.
- `runtime_state/functions_to_access_pds_runtime_state.{c,h}` — adds
  `requested_mode_is_manual()` and zeroes the new fields on boot.
- `externally_controlled_board_behaviors/functions_to_run_manual_control_mode.{c,h}` —
  the per-loop runner that does the snapshot-mirror and (on mainboard
  only) the actual GPIO writes plus the PA18-21 sampling.
- `app/main.c` — adds the `else if (requested_mode_is_manual())` branch
  and a 1 Hz heartbeat blink on PB22 that runs in every other mode.
- `communication_with_esp32/command_execution/functions_to_execute_board_commands_received_from_esp32.c` —
  the five new command handlers and the mode-gating helper.
- `status_reporting_to_esp32/functions_to_build_status_replies_sent_to_esp32.c` —
  appends the 11-byte manual block to every values reply, version 3.
- `board_hardware_startup/functions_to_initialize_board_hardware_before_main_loop_runs.c` —
  on mainboard only, configures PB22 / PA16 / PA17 outputs and PA18-21
  inputs at boot.

Mainboard-only drivers (in `src/drivers/`):

- `gpio_pv_efuse_enable_pa16_on_mainboard.{c,h}`
- `gpio_bat_efuse_enable_pa17_on_mainboard.{c,h}`
- `gpio_efuse_status_inputs_pa18_to_pa21_on_mainboard.{c,h}`
- `led_status_pb22_active_high_on_mainboard.{c,h}` (already existed; now
  also linked into `mainboard-pds`)

ESP32 bridge (in `esp32_test_harness/03_source_pds_bridge/`):

- `source_pds_command_contract.h` — mirror of the five new IDs and field
  bit.
- `functions_to_translate_serial_text_into_source_pds_commands.cpp` —
  five new text-command branches and the help-text update.
- `functions_to_print_source_pds_replies.cpp` — reads the appended block,
  prints the two new `[BOARD] manual ...` and `[BOARD] efuse ...` lines,
  and includes a `manual` entry in `requested_mode_name()`.

Python web app (in `src-pds/local_web_app/`):

- `manual_control_page_actions.py` — five action functions that send the
  text commands through the shared bridge.
- `serial_bridge_shared_by_pages.py` — adds `in_manual_mode` to the
  shared `PageState`, two new regex parsers for the `manual` and `efuse`
  lines, the `disconnect_from_serial_port()` helper, and clears the new
  flag in the global `Off` sequence.
- `run_local_web_app.py` — mounts `/manual` and the five
  `/api/manual/...` POST endpoints, plus a global `/api/disconnect`.
- `static/manual/manual_control.{html,css,js}` — the page itself.
- `static/index.html` — adds the third card linking to `/manual`.

Each page (MPPT, State, Manual) now also has a **Disconnect** button in
the top bar that releases COM3 without restarting the server.

## How to test on the dev board

1. Build and flash:
   ```
   make BOARD=devboard-pds flash
   ```
2. Re-upload the ESP32 sketch via Arduino IDE
   (`esp32_test_harness/03_source_pds_bridge/03_source_pds_bridge.ino`).
3. Start the local web app:
   ```
   python src-pds/local_web_app/run_local_web_app.py
   ```
4. Open `http://127.0.0.1:8000/manual`.
5. Click **Connect**, then **Off**, then **Enter Manual Mode**.
6. Move the PWM slider and click **Apply** — the buck PWM pins (PA12/PA13)
   physically change. On a scope you should see the duty cycle update.
7. Toggle the PV / BAT / LED on/off buttons — the page readbacks
   (`Requested`, `PGOOD`, `LED Actual`) follow the toggles even though no
   physical pin moves. This is the dev-board mirror behaviour.

The Mode tile in the Live Telemetry sidebar will read `manual` while in
manual mode (was `unknown` before the ESP32 sketch was updated to know
the new mode value).
