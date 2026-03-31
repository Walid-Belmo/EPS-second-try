# Toolchain Setup — Windows

This document covers installing the complete build and flash environment on
Windows without WSL, without any IDE, using only command-line tools.

Everything runs natively in Windows. The terminal is MSYS2 (a Unix-like shell
and package manager for Windows). You write code in VSCode. You build and flash
from the MSYS2 terminal.

---

## Why Not WSL

WSL2 has a known USB passthrough problem. The nEDBG on the Curiosity Nano
appears as a USB device (CMSIS-DAP + CDC serial port). WSL2 does not expose
USB devices to the Linux environment by default. While `usbipd-win` can forward
USB to WSL2, it adds complexity and fragility for little benefit. Native Windows
tools work directly and reliably.

Sources:
- Microsoft docs on WSL2 USB: https://learn.microsoft.com/en-us/windows/wsl/connect-usb
- Community reports of OpenOCD + CMSIS-DAP reliability issues under WSL2

---

## Step 1: Install MSYS2

MSYS2 provides a Unix-like shell, `pacman` package manager, and `make` on Windows.
All subsequent commands run inside the MSYS2 MINGW64 terminal.

Download from: https://www.msys2.org

Install to the default location `C:\msys64`. After installation, open the
**MSYS2 MINGW64** terminal (not MSYS2 MSYS or UCRT64 — use MINGW64 specifically,
as this is the environment where ARM tools integrate cleanly).

Update the package database first:
```bash
pacman -Syu
# Close and reopen terminal when prompted, then:
pacman -Su
```

---

## Step 2: Install the ARM Toolchain

Two approaches exist. The recommended one for this project is the official ARM
installer because it provides the exact version we need and is straightforward
to add to PATH.

**Approach A (recommended): Official ARM installer**

Download the AArch32 bare-metal target installer (arm-none-eabi) from:
https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

Choose the Windows `.exe` installer for the `arm-none-eabi` target.
During installation, check "Add to PATH" — this is not the default, do not skip it.

Verify:
```bash
arm-none-eabi-gcc --version
# Should print: arm-none-eabi-gcc (Arm GNU Toolchain ...) 13.x.x
```

Source: ARM developer portal, confirmed working approach documented at
https://aleaengineering.com/2024/04/24/open-source-arm-based-toolchain-development-and-debug-on-windows-with-no-emulation/

**Approach B (alternative): MSYS2 package**

```bash
pacman -S mingw-w64-x86_64-arm-none-eabi-toolchain
```

This installs version 13.3.0 as of 2024. The downside is the tools are only
accessible from inside the MSYS2 MINGW64 shell, not from a regular Windows
Command Prompt. For this project that is acceptable since we always build from
MSYS2.

Source: https://packages.msys2.org/packages/mingw-w64-x86_64-arm-none-eabi-gcc

---

## Step 3: Install Make

Make is available via MSYS2:
```bash
pacman -S make
```

Verify:
```bash
make --version
# Should print: GNU Make 4.x
```

---

## Step 4: Install OpenOCD

OpenOCD on Windows requires a pre-built binary. Do not try to build from source.

**Recommended: xPack OpenOCD**

xPack provides maintained, up-to-date Windows binaries of OpenOCD with CMSIS-DAP
support compiled in. This is the most reliable source for Windows.

Download from: https://github.com/xpack-binaries/openocd-xpack/releases

Download the `.zip` for Windows x64. Extract to a permanent location such as
`C:\tools\openocd`. Add `C:\tools\openocd\bin` to your Windows PATH environment
variable (System Properties → Environment Variables → Path).

Verify inside MSYS2:
```bash
openocd --version
# Should print: Open On-Chip Debugger 0.12.x
```

Alternative: The OpenOCD project provides Windows binaries at https://openocd.org
but the xPack builds are generally more current and consistently include CMSIS-DAP.

Source: xPack project, widely used in embedded communities for Windows OpenOCD

---

## Step 5: USB Driver for the nEDBG

The nEDBG presents as a CMSIS-DAP HID device. Windows usually installs a driver
automatically when you plug in the Curiosity Nano. OpenOCD communicates with
CMSIS-DAP through this driver.

**If OpenOCD fails with LIBUSB_ERROR_ACCESS or cannot find the device:**

Download Zadig from https://zadig.akeo.ie

- Open Zadig
- Options → List All Devices
- Find "CMSIS-DAP" or "nEDBG" in the dropdown
- Select WinUSB as the driver
- Click "Replace Driver"

Important: The CDC serial port (virtual COM port) appears as a separate USB
interface and should NOT be touched by Zadig. Only replace the driver for the
CMSIS-DAP interface. If you accidentally replace the CDC driver, PuTTY will
no longer see the serial port. To recover, use Device Manager to uninstall the
device and let Windows reinstall the original driver.

The nEDBG USB identifiers: VID 0x03eb, PID 0x2175 (confirmed from USB descriptor,
manufacturer: Microchip Technology Incorporated, product: nEDBG CMSIS-DAP).

---

## Step 6: Serial Terminal — PuTTY

PuTTY is the standard Windows serial terminal. Download from https://putty.org

When the Curiosity Nano is connected, Windows assigns a COM port to the virtual
COM port. Find the port number in Device Manager → Ports (COM & LPT) → look for
"USB Serial Device" or "Curiosity Virtual COM Port". It will be COM3, COM4, or
similar — the number varies.

PuTTY settings for log output:
- Connection type: Serial
- Serial line: COM3 (substitute your actual port number)
- Speed: 115200
- Data bits: 8, Stop bits: 1, Parity: None, Flow control: None

To save a session: enter these settings, type a name in "Saved Sessions", click Save.

**To log to a file while viewing:** In PuTTY configuration → Session → Logging →
set to "All session output" and choose a log file path. PuTTY will write
everything to the file and display it simultaneously.

---

## Step 7: Verify Everything Works Together

With the Curiosity Nano plugged in, run this inside MSYS2:

```bash
openocd -f interface/cmsis-dap.cfg -f target/at91samdXX.cfg
```

Expected output (successful connection):
```
Info : CMSIS-DAP: SWD Supported
Info : CMSIS-DAP: Interface Initialised (SWD)
Info : SWD DPIDR 0x0bc11477
Info : at91samd21.cpu: hardware has 4 breakpoints, 2 watchpoints
Info : Listening on port 3333 for gdb connections
```

If you see this, the entire chain — USB → nEDBG → SWD → SAMD21G17D — is working.
Press Ctrl+C to stop OpenOCD.

---

## Things to Be Careful About

**PATH conflicts.** If you have multiple GCC installations (for example, MinGW
for native Windows C development), the wrong `gcc` might be found first. Always
verify `arm-none-eabi-gcc --version` returns an ARM compiler, not a native x86 one.

**MSYS2 terminal vs Windows Command Prompt.** Makefiles with Unix-style paths
and shell syntax will not work in CMD or PowerShell. Always build from inside
the MSYS2 MINGW64 terminal.

**Line endings.** If you edit files in Windows and they get CRLF line endings,
Makefiles will break (Make is sensitive to line endings in recipes). Configure
VSCode to use LF line endings for all project files: `.editorconfig` or VSCode
settings `"files.eol": "\n"`.

**COM port number changes.** Windows may assign a different COM number when you
plug the board into a different USB port. Always check Device Manager before
opening PuTTY if logs are not appearing.

**OpenOCD and PuTTY on the same device.** OpenOCD connects to the CMSIS-DAP
interface. PuTTY connects to the CDC serial interface. These are separate USB
interfaces on the same physical device. Both can be open simultaneously — they
do not conflict.
