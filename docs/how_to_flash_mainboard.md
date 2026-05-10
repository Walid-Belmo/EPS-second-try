# How to Flash the Mainboard

The firmware binary for the real board is built with:

```powershell
make BOARD=mainboard
```

That produces `build/satellite_firmware.bin` for the `SAMD21J17D`.

## First Check

The board must be powered before SWD attach. The debugger normally senses
3.3 V; most probes do not power the target.

For the real board UART header `J3`:

| Pin | Net |
|---|---|
| 1 | `AUX_3V3` |
| 2 | `UART_TX` |
| 3 | `UART_RX` |
| 4 | `GND` |

Driving 3.3 V into J3 pin 1 powers the post-regulator `AUX_3V3` rail directly.

## OpenOCD Probe Choices

CMSIS-DAP, including Curiosity Nano nEDBG, Atmel-ICE, DAPLink, or PICkit/SNAP
after CMSIS-DAP switching:

```powershell
make BOARD=mainboard OPENOCD_CONFIG=openocd.cfg flash
```

ST-Link:

```powershell
make BOARD=mainboard OPENOCD_CONFIG=openocd-stlink.cfg flash
```

J-Link:

```powershell
make BOARD=mainboard OPENOCD_CONFIG=openocd-jlink.cfg flash
```

Successful OpenOCD output must include `Verified OK`.

## If OpenOCD Cannot Find the Probe

Run:

```powershell
Get-PnpDevice -PresentOnly | Where-Object {
    $_.FriendlyName -match 'CMSIS|DAP|J-Link|ST-Link|Atmel|EDBG|PICkit|SNAP'
}
```

If nothing relevant appears, Windows does not see the debugger. Fix USB cable,
driver, or probe firmware before debugging firmware.

## If OpenOCD Finds the Probe But Not the Target

Observed locally with the connected Atmel-ICE:

```text
Info : CMSIS-DAP: Serial# = J42700084466
Error: Error connecting DP: cannot read IDR
```

This means OpenOCD can talk to the programmer, but the programmer cannot read
the SAMD21 debug port over SWD. Lowering SWD speed to 500 kHz and 100 kHz did
not change the failure, so this is probably not an adapter-speed problem.

Check these in order:

1. Board has 3.3 V power.
2. Probe ground and board ground are connected.
3. Probe VTREF sees the board 3.3 V rail.
4. SWDIO, SWCLK, RESET, and GND are on the correct Tag-Connect pins.
5. Reduce `adapter speed` to `500` in the selected config and retry.

## UART After Flash

Connect the ESP32 bridge to J3:

```text
ESP32 RX2 GPIO16 <- J3 pin 2 UART_TX
ESP32 TX2 GPIO17 -> J3 pin 3 UART_RX
ESP32 GND        <-> J3 pin 4 GND
```

Open the ESP32 serial monitor at `115200` and run `telemetry`,
`inject-default`, and `stream on 1000`.
