# Research Log — Agent D: SWD programmers and Tag-Connect TC2050 ecosystem

Purpose: Map every viable path for flashing the ATSAMD21J17D-MUT on the
CHESS EPS PCU testing board V4.1 over SWD. Specifically: what programmer
hardware will work, what `openocd.cfg` each one requires, what cable type
is needed for the on-board Tag-Connect TC2050-IDC-NL footprint, and whether
the team's existing Curiosity Nano DM320119 dev board can be repurposed as
a standalone external SWD probe (likely the cheapest path). The downstream
build decision is which programmer the user will be told to use tonight,
and what changes to make to the existing `openocd.cfg`.

Ground rules:
- Prefer official primary sources (Microchip, SEGGER, ST, OpenOCD source,
  Tag-Connect.com product pages) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-04-26.

---

## Source 1: Tag-Connect TC2050-IDC-NL product page (search result)

- **URL / path:** https://www.tag-connect.com/product/tc2050-idc-nl-10-pin-no-legs-cable-with-ribbon-connector
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Tag-Connect's TC2050-IDC-NL "No Legs" Plug-of-Nails programming cable is a
  10-conductor cable fitted with a spring-pin "Tag-Connector" head. It plugs
  directly onto a PCB footprint of pads + locating holes (no through-hole
  header on the board), and terminates in a `0.1"` (2.54 mm) IDC ribbon
  connector on the programmer end. Verbatim: `"10-conductor cable fitted with
  a spring-pin Tag-Connector that conveniently plugs directly into your PCB
  and terminates in a 0.1″ ribbon connector"`. NL = "No Legs". Without legs,
  the smallest PCB footprint is achieved, but the cable must be hand-held or
  retained by a TC2050-CLIP accessory for hands-free debugging.
- **Confidence: HIGH**
  Manufacturer's own product page text returned via search snippet; corroborated
  by independent reseller (Debug Store, tinyosshop) wording.
- **Implication for our build:**
  The CHESS PCU testing board V4.1 schematic uses footprint `TC2050-IDC-NL`,
  meaning the board has the bare 10-pad + 2-locating-hole footprint and NO
  physical connector. The mating cable Walid needs is the **TC2050-IDC-NL**
  (terminating in 0.1" / 2.54 mm IDC) or **TC2050-IDC** (the legged variant —
  same head, identical electrical interface; legs just self-retain). Either
  cable will fit the same footprint. The legged version is more comfortable
  for repeated programming on a single board; the NL version is the
  production-friendly choice. Both terminate in the same 10-way 0.1" IDC.
- **Why I'm recording it:**
  Establishes the cable family that mates the board's SWD footprint.

---

## Source 3: TC2050 standard pinout for ARM SWD

- **URL / path:** Aggregated from https://www.tag-connect.com/product/tc2050-idc-nl-10-pin-no-legs-cable-with-ribbon-connector
  and https://www.tag-connect.com/info/cables-for-arm-cortex (search snippet)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  TC2050 standard pinout (top view, looking into the head against the PCB):
  - Pin 1: VTG / VCC (target voltage reference)
  - Pin 2: TMS / SWDIO
  - Pin 3: GND
  - Pin 4: TCK / SWCLK
  - Pin 5: NC (not connected on `-050` variant; connected on `-IDC-NL` 0.1")
  - Pin 6: TDO / SWO
  - Pin 7: GND (key, marks orientation)
  - Pin 8: TDI (JTAG only — unused for pure SWD)
  - Pin 9: NC (not connected on `-050` variant)
  - Pin 10: nRESET
  This matches the ARM Cortex Debug 10-pin standard, just on a different
  physical connector (pogo-pin head instead of Samtec FTSH-105-01).
- **Confidence: HIGH**
  This pinout is the long-established Tag-Connect / ARM Cortex standard and
  appears identically across vendor and reseller pages and on the official
  Tag-Connect ARM Cortex info page.
- **Implication for our build:**
  For pure SWD on the SAMD21J17D, we need only Pin 1 (VTG), Pin 2 (SWDIO),
  Pin 3 (GND), Pin 4 (SWCLK), Pin 7 (GND), and Pin 10 (nRESET). Pins 5, 6, 8,
  9 are not required for flashing (SWO on Pin 6 is optional trace output
  only). Pin 1 is a **reference** voltage — the cable carries it to let the
  probe match its level shifters; whether the probe also drives target power
  back depends on the probe (J-Link Plus and Atmel-ICE can; ST-Link cannot;
  nEDBG via DGI can supply target power). This means the PCU board MUST be
  externally powered before SWD attach unless the chosen probe explicitly
  supports target power supply.
- **Why I'm recording it:**
  Documents what the cable actually carries — answers questions A.3 and A.4.

---

## Source 4: Local copy — DM320119 SAM D21 Curiosity Nano User Guide (DS70005409D)

- **URL / path:** `c:\Users\iceoc\Documents\EPS-second-try\datasheets\dm320119_user_guide.pdf` (pages 5–13)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This is the authoritative Microchip user guide for our exact dev board.
  Key facts extracted verbatim / paraphrased:
  - §3.1: `"The On-Board Nano Debugger is a complex USB device consists of
    several interfaces, such as a debugger, a mass storage, a data gateway,
    and a Virtual COM port (CDC)."` It can program and debug the SAMD21G17D.
  - §3.2 Curiosity Nano Standard Pinout — 12 edge pads near USB.
    The 8 signals routed through level-shifters to the target are:
    - VTG  (target voltage rail / reference)
    - GND
    - DBG0 = SWDATA (= SWDIO)
    - DBG1 = SWCLK
    - DBG2 = GPIO (also tied to SW0/PB11 user button via cut strap)
    - DBG3 = nRESET
    - CDC TX (debugger → target UART RX)
    - CDC RX (debugger → target UART TX)
    Plus VBUS, VOFF (voltage-off control input), ID, NC.
  - §3.3.2 External Supply: `"When the Voltage Off (VOFF) pin is shorted to
    ground (GND), the On-Board Nano Debugger firmware disables the target
    regulator and it is safe to apply an external voltage to the VTG pin."`
    Programming/debug/data streaming still work in this mode — the debugger
    and level shifters stay USB-powered. Absolute max VTG is 5.5 V.
  - §3.4 "Disconnecting the On-Board Nano Debugger": each of the six debugger
    signals (DBG0, DBG1, DBG2, DBG3, CDC TX, CDC RX) plus the VTG strap
    crosses a **cut strap** on the bottom of the board. `"By cutting the
    GPIO straps with a sharp tool ... all I/Os connected between the debugger
    and the SAMD21G17D can be disconnected. To disconnect the target
    regulator, cut the VTG strap."`
    Note 2: `"Solder in 0Ω resistors across the footprints or short-circuit
    them with tin solder to reconnect any cut signals."` So cut straps are
    reversible.
  - §4.3.1 Connection Details table confirms the debugger-side pin mapping
    on the on-board target: DBG0=SWDIO on PA31, DBG1=SWCLK on PA30,
    DBG3=nRESET, DBG2=GPIO (PB11/SW0).
- **Confidence: HIGH**
  Microchip primary documentation, doc DS70005409D, matched against the
  physical board in front of the user.
- **Implication for our build (this is the critical one):**
  The user guide describes the cut straps explicitly as a "Disconnecting the
  On-Board Nano Debugger" feature so the DEBUGGER side keeps working with
  USB connected, and the level shifters / VTG output edge connector still
  carry the SWD signals to the world. So an unmodified Curiosity Nano can
  drive an external SAMD21J17D over SWD by wiring six pads:
  - VTG  → external 3.3 V rail (level-shifter reference; should match
            external board's VCC)
  - GND  → external GND
  - DBG0 → external SWDIO
  - DBG1 → external SWCLK
  - DBG3 → external nRESET
  - (optional) CDC TX/RX → UART for serial console
  HOWEVER — there is a SHARP CAVEAT: with the on-board SAMD21G17D NOT
  cut off, the debugger is also driving (and seeing the SWD chain on)
  the on-board G17D. Two SWD targets on the same SWD bus = bus
  contention; the SAMD21J17D will likely fail to be discovered. So to
  use the Curiosity Nano as an external programmer cleanly, Walid must
  either:
  (a) **Cut the four GPIO straps** (DBG0, DBG1, DBG3 at minimum;
      DBG2 optional) on the bottom of the Curiosity Nano. Cut strap
      = sever the trace with a knife per Microchip's instruction. Easily
      reconnected later by a 0Ω resistor or solder bridge.
  (b) Hold the on-board G17D in reset (cut DBG3 strap or pull its
      RESET low externally) — but the SWDIO/SWCLK lines are still
      shared, which often still works because both targets remain on
      the same bus and the second one is silent under reset, but is
      not officially supported.
  The cleanest, lowest-risk path is (a): cut DBG0 + DBG1 + DBG3 straps,
  wire VTG/GND/DBG0/DBG1/DBG3 to the PCU board's TC2050 pads, leave
  the on-board G17D untouched. Microchip explicitly says this is
  reversible with solder.
  The nEDBG enumerates as a CMSIS-DAP HID device under VID 0x03EB,
  so the existing `openocd.cfg` (cmsis-dap driver, at91samdXX target)
  should work with **NO change** as long as the target script
  recognises the SAMD21J17D — see Source 7 below where I'll verify
  this in the OpenOCD source.
- **Why I'm recording it:**
  This is the centrepiece of Question 6. The official answer is YES, the
  Curiosity Nano can serve as an external SWD programmer for the SAMD21J17D,
  via the cut-strap procedure documented in §3.4.

---

## Source 5: Microchip onlinedocs — On-Board Nano Debugger reference

- **URL / path:** https://onlinedocs.microchip.com/oxy/GUID-0B6B33DB-9998-427C-A844-7CE2225C211C-en-US-1/GUID-1BEF6F3A-8325-4A62-9B51-9785E01AF7AB.html (search snippet only — fetch timed out twice)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Microchip-hosted "On-Board Nano Debugger" documentation page, returned
  via web search snippet. It confirms generically that "All signals between
  the on-board debugger and the target device are routed through cut straps
  on the bottom of the board, which can be used to disconnect the debugger
  from the target for various purposes." The page is the cross-product
  reference (covers PIC ICSP, AVR UPDI, ARM SWD variants of the Nano family
  in one spec).
- **Confidence: MEDIUM**
  Snippet only — full page kept timing out. But the snippet text matches
  Source 4 verbatim, so I'm confident this is the same Microchip-authored
  cross-family doc.
- **Implication for our build:**
  Confirms that the cut-strap-as-external-programmer approach is the
  Microchip-blessed standard usage pattern across the Nano product line, not
  a hack. No further config changes implied.
- **Why I'm recording it:**
  Independent confirmation that Source 4 isn't a one-off note in one user
  guide.

---

## Source 6: Arduino forum — community confirmation that nEDBG flashes external boards

- **URL / path:** https://forum.arduino.cc/t/atmel-curiosity-nano-board-as-programmers/893634 (search snippet)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Community thread titled "Atmel Curiosity Nano board as programmers" —
  hobbyists confirming the nEDBG can be wired to an external chip and used
  as a CMSIS-DAP probe over OpenOCD or via Microchip's pymcuprog tool.
- **Confidence: LOW**
  Forum thread, not primary documentation. Only used to corroborate that this
  is a known working setup, not for any specific config detail.
- **Implication for our build:**
  Reinforces that we're not the first ones to do this — there is community
  signal that it works. Does not change any config decision.
- **Why I'm recording it:**
  Sanity check; cited under Question 6.

---

## Source 7: OpenOCD source — `tcl/target/at91samdXX.cfg` (master branch)

- **URL / path:** https://raw.githubusercontent.com/openocd-org/openocd/master/tcl/target/at91samdXX.cfg
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Generic Cortex-M0 target script for the entire SAMD/SAMR/SAML/SAMC family.
  Verbatim header: `"# script for Atmel SAMD, SAMR, SAML or SAMC, a Cortex-M0
  chip"`. Forces SWD (`transport select swd`), declares the SWD DAP with
  `-expected-id 0x4ba00477` (the standard ARM Cortex-M0 ID), creates a
  `cortex_m` target, registers a `at91samd` flash bank at `0x00000000`, and
  configures DSU-aware reset handling. Default `adapter speed 400` (kHz!),
  with a comment saying Atmel's EDBG cmsis-dap can run flat-out and
  recommending no more than 10× CPU clock. Critically, the script does
  **not** branch on G vs J vs E variant — variant detection is done at
  runtime by the C-language flash driver `at91samd.c`, not in TCL.
- **Confidence: HIGH**
  Direct read of OpenOCD master-branch source.
- **Implication for our build:**
  The same `source [find target/at91samdXX.cfg]` line in our existing
  `openocd.cfg` will work for SAMD21J17D as well as SAMD21G17D — no edit
  required. The 400 kHz default is conservative; our explicit
  `adapter speed 4000` (4 MHz) override is fine for the on-chip nEDBG with
  short pads, but for first attach over a long Tag-Connect cable I would
  back off to 1000 (1 MHz) to be safe (see Source 9 / Q.16).
- **Why I'm recording it:**
  Answers Question 15 directly: per-variant script change NOT required.

---

## Source 8: OpenOCD source — `src/flash/nor/at91samd.c` device table

- **URL / path:** https://raw.githubusercontent.com/openocd-org/openocd/master/src/flash/nor/at91samd.c
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Verbatim entries from the device-ID lookup table in the SAMD flash driver:
  ```
  { 0x92, "SAMD21J17D", 128, 16 },
  { 0x93, "SAMD21G17D", 128, 16 },
  { 0x94, "SAMD21E17D", 128, 16 },
  ```
  Both J17D and G17D are recognised. Flash size = 128 KB, page size =
  16 (rows), exactly matching the SAMD21J17D datasheet.
- **Confidence: HIGH**
  Direct read of OpenOCD source code, master branch.
- **Implication for our build:**
  When OpenOCD attaches via SWD it reads the DSU DID register, looks up
  variant byte `0x92`, and prints something like
  `"SAMD MCU: SAMD21J17D (128KB Flash, 16KB RAM)"`. So flash erase / program
  / verify will work without any per-variant tweak. This kills any concern
  about "the OpenOCD config is wrong because it was written for the G17D".
  The same config flashes both chips.
- **Why I'm recording it:**
  Definitive proof that our `openocd.cfg` does not need to change to flash
  the J17D on the PCU board (Question 15).

---

## Source 9: OpenOCD User's Guide — J-Link debug adapters chapter

- **URL / path:** https://openocd.org/doc/html/Debug-Adapter-Configuration.html
  + https://kb.segger.com/OpenOCD
  + https://deepwiki.com/openocd-org/openocd/4.2-j-link-debug-adapters
  (search snippets aggregated)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  J-Link family (EDU, EDU Mini, Plus, Pro, Ultra) is a first-class OpenOCD
  driver. Config skeleton:
  ```
  source [find interface/jlink.cfg]
  transport select swd
  source [find target/at91samdXX.cfg]
  adapter speed 2000
  ```
  (`adapter speed` in kHz; "J-Link adapters can default to speeds around
  4000 kHz with short leads"). `jlink.cfg` is a one-liner that does
  `adapter driver jlink`. SEGGER's own KB confirms compatibility but warns
  that some FW/HW combinations need the latest firmware.
  EDU Mini limitation: licensed only for non-commercial use; otherwise full
  SAMD21 SWD support.
- **Confidence: HIGH**
  OpenOCD official docs + SEGGER KB.
- **Implication for our build:**
  If Walid has a J-Link of any flavour, he replaces the first line of
  `openocd.cfg` with `source [find interface/jlink.cfg]`, deletes the
  existing `adapter driver cmsis-dap` line, keeps everything else, and it
  flashes. J-Link Plus/Pro can supply target power; J-Link EDU and EDU Mini
  CANNOT (they sense VTREF but do not source it). No virtual COM port on
  EDU Mini; J-Link Plus has VCOM.
- **Why I'm recording it:**
  Question 7 + Q14.

---

## Source 10: OpenOCD ST-Link support + community SAMD21 examples

- **URL / path:** https://github.com/todbot/samd21-programming-notes ;
  https://learn.adafruit.com/debugging-the-samd21-with-gdb/setup ;
  https://openocd.org/doc/html/Debug-Adapter-Configuration.html
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  ST-Link V2 / V3 are supported by OpenOCD via the `stlink` driver (the
  modern, non-HLA path) since OpenOCD 0.10. SAMD21 works fine; the chip
  family is irrelevant to the ST-Link driver as long as the target script
  selects SWD. Config:
  ```
  source [find interface/stlink.cfg]
  transport select swd
  source [find target/at91samdXX.cfg]
  adapter speed 1000
  ```
  Adafruit's SAMD21 GDB tutorial uses an ST-Link V2 clone; todbot's
  notes give a working `openocd.cfg` for ST-Link + SAMD21. Hardware:
  ST-Link clones expose 4 wires (SWDIO, SWCLK, GND, VCC-sense); they do
  NOT power the target — Walid must externally power the PCU board.
  ST-Link clones have no virtual COM port unless they're V2-1.
- **Confidence: HIGH**
  Multiple corroborating community sources + OpenOCD official driver list.
- **Implication for our build:**
  ST-Link V2 / V2-1 / V3 will all flash a SAMD21J17D. Walid must update
  the ST-Link firmware to a recent ST-Link Utility / `stm32cubeprog` build
  before first use, or OpenOCD will reject it with `"failed to read
  STLink firmware version"` (see Q.17).
- **Why I'm recording it:**
  Questions 8 + 14 + 17.

---

## Source 11: Atmel-ICE OpenOCD config (Omzlo / todbot / EmbedIc)

- **URL / path:** https://omzlo.com/articles/programming-the-samd21-using-atmel-ice-with-openocd-(updated)
  ; https://github.com/todbot/samd21-programming-notes/blob/main/openocd_atmel-ice_samd21.cfg
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Atmel-ICE is a CMSIS-DAP class device under VID `0x03eb` PID `0x2141`.
  Verbatim config from Omzlo:
  ```
  interface cmsis-dap
  cmsis_dap_vid_pid 0x03eb 0x2141
  set CHIPNAME at91samd21e17
  source [find target/at91samdXX.cfg]
  ```
  (Modern OpenOCD: `interface cmsis-dap` → `adapter driver cmsis-dap`.)
  If multiple Atmel-ICE devices, distinguish via
  `cmsis_dap_serial J41800012345`. Atmel-ICE is sold in two trims: the
  full kit (with SAM-50, AVR-10, JTAG-20 cables) and the **Atmel-ICE Basic**
  (which includes only one ribbon cable and PCB header — no Cortex-10 or
  Tag-Connect cable, so for our PCU board you'd still need to source a
  TC2050-IDC-NL separately and a 50-mil-to-100-mil adapter or use the
  AVR-50-to-Cortex-10 cable plus an adapter).
- **Confidence: HIGH**
  Multiple consistent third-party writeups, all using the same VID/PID
  and config.
- **Implication for our build:**
  If Walid has an Atmel-ICE on the bench, it's the most "officially
  supported" path (Microchip's own probe). Existing `openocd.cfg` works
  with ZERO change because the driver is already `cmsis-dap`; only the
  USB device that gets picked up changes (nEDBG VID/PID 0x03eb/0x2175 vs
  Atmel-ICE 0x03eb/0x2141 — both have VID 0x03eb, OpenOCD picks the
  first cmsis-dap device it sees). For deterministic selection on a
  workstation with multiple probes attached, add
  `cmsis_dap_vid_pid 0x03eb 0x2141`.
  Atmel-ICE has a Cortex-10 0.05" ribbon, so to reach the board's
  TC2050-IDC-NL footprint Walid needs:
  EITHER (cleanest) `TC2050-IDC-NL-050` cable (Tag-Connect to 0.05" ribbon)
  OR `TC2050-IDC-NL` (0.1" ribbon) + Atmel-ICE 50→100 mil adapter.
- **Why I'm recording it:**
  Question 9 + 14.

---

## Source 12: Black Magic Probe SAMD21 support

- **URL / path:** https://black-magic.org ; https://1bitsquared.com/products/black-magic-probe ;
  Arduino forum thread `https://forum.arduino.cc/t/samd21-burning-bootloader-with-gdb-black-magic-probe/650575`
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Black Magic Probe (BMP) supports ARM Cortex-M including SAMD-family.
  Crucially, BMP does **not use OpenOCD** — it embeds its own GDB server,
  so you talk to it directly with arm-none-eabi-gdb via a virtual COM port:
  ```
  (gdb) target extended-remote /dev/ttyACM0   # or COMx on Windows
  (gdb) monitor swdp_scan
  (gdb) attach 1
  (gdb) load
  ```
  BMP enumerates two USB CDC interfaces on Windows: GDB server on the first
  COM, free virtual UART on the second. It DOES supply 3.3 V target power
  (jumper-selectable) up to ~100 mA.
- **Confidence: MEDIUM**
  Vendor + community-confirmed; not OpenOCD-bound so does not affect the
  existing config — relevant only as an alternative path.
- **Implication for our build:**
  If Walid has a BMP, the existing `openocd.cfg` does NOT apply at all.
  Flow becomes a `make flash`-replacement that runs gdb scripts. It works,
  but is a bigger build-system change than any of the OpenOCD probes.
  Probably NOT the right answer for tonight unless he's already used BMP
  before.
- **Why I'm recording it:**
  Question 13 + 14.

---

## Source 13: PICkit 5 SWD support (Microchip)

- **URL / path:** Microchip product page + onlinedocs
  https://www.microchip.com/en-us/development-tool/pg164150
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  PICkit 5 supports SWD programming of SAM family in MPLAB X via Microchip's
  proprietary protocol. It is **not a CMSIS-DAP device** — it speaks
  Microchip's own programmer protocol, so OpenOCD does not support it.
  pymcuprog and MPLAB X / IPE work; OpenOCD does not.
- **Confidence: MEDIUM**
  Vendor doc + multiple Microchip community threads converge on this.
  Worth verifying with a fresh search if user actually has one — see Source 14.
- **Implication for our build:**
  PICkit 5 = does-not-work-for-OpenOCD-flow. To use it, Walid would have
  to install MPLAB X / IPE and import the ELF, abandoning `make flash`.
  Verdict: works only if you switch toolchain to MPLAB IPE.
- **Why I'm recording it:**
  Question 10 + 14.

---

## Source 14: MPLAB SNAP SWD support

- **URL / path:** Microchip MPLAB SNAP product page
  https://www.microchip.com/en-us/development-tool/pg164100
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  MPLAB SNAP officially supports SAMD21 family programming/debugging, but
  ONLY through Microchip's MPLAB X / IPE software stack. Like PICkit 5,
  SNAP is **not CMSIS-DAP**, so OpenOCD cannot drive it. Multiple community
  reports confirm this on Microchip Developer Help.
- **Confidence: MEDIUM**
  Vendor product page + repeated community confirmations.
- **Implication for our build:**
  MPLAB SNAP = does-not-work-for-OpenOCD-flow. Same caveats as PICkit 5.
- **Why I'm recording it:**
  Question 11 + 14.

---

## Source 15: Generic CMSIS-DAP / DAPLink boards

- **URL / path:** ARM CMSIS-DAP firmware spec (https://arm-software.github.io/CMSIS_5/DAP/html/index.html)
  + DAPLink reference firmware (https://github.com/ARMmbed/DAPLink)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  CMSIS-DAP is an ARM-defined HID protocol for SWD/JTAG probes. Any device
  that enumerates as CMSIS-DAP — including DAPLink boards (mbed dev boards
  like the LPC11U35-based ones, NXP FRDM, ST Nucleo's onboard CMSIS-DAP
  mode), ARM mbed CMSIS-DAP debugger boards, and cheap clones from
  AliExpress — works with OpenOCD's `adapter driver cmsis-dap` driver.
  Some clones present an unsigned WinUSB interface and need
  `WinUSB`/`libusbk` driver assignment via Zadig on Windows; others install
  cleanly as HID.
- **Confidence: HIGH**
  ARM official spec + widespread community usage.
- **Implication for our build:**
  Any CMSIS-DAP probe will Just Work with our existing `openocd.cfg` (no
  change). Cheap LPC-based "DAPLink V2" or "CMSIS-DAP V2" probes from
  AliExpress (~$8) are a viable backup. They typically don't supply target
  power (~5 mA into VTREF for level shift sense only).
- **Why I'm recording it:**
  Question 12 + 14.

---

## Source 16: OpenOCD User's Guide — adapter speed guidance + first-flash failures

- **URL / path:** https://openocd.org/doc/html/General-Commands.html#adapter-speed
  ; https://openocd.org/doc/html/Debug-Adapter-Configuration.html
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  OpenOCD's `adapter speed` is in kHz. The User's Guide notes adapters
  cannot always run at the specified speed exactly, and that long cables /
  poor signal integrity force a lower clock. Guidance: start at 1 MHz
  (1000) for first connection, raise once stable. The `at91samdXX.cfg`
  default is 400 kHz precisely to avoid first-attach issues on hand-wired
  setups.
  Common SAMD21 first-flash errors and fixes:
  - `Error: jtag status contains invalid mode value - communication failure`
    → wrong target voltage / SWDIO+SWCLK swapped / nRESET held low
  - `Error: invalid ACK (7) in DAP response` → adapter speed too high; drop
    to 500 kHz
  - `Polling target ... failed, GDB will be halted` → target not powered,
    or VTG/VTREF on probe not connected to target VCC
  - ST-Link `"open failed"` → firmware too old, update with STM32CubeProg
  - CMSIS-DAP not enumerating on Windows → install latest WinUSB or use
    Zadig to bind libusbk
- **Confidence: HIGH**
  Primary OpenOCD documentation + repeated community failure-mode list.
- **Implication for our build:**
  For tonight: change `adapter speed 4000` → `adapter speed 1000` for the
  first attempt with the new PCU board to avoid signal-integrity first-flash
  failures. Once known good, raise back to 2000–4000.
- **Why I'm recording it:**
  Questions 16 + 17.

---

## Source 17: Identifying CMSIS-DAP USB devices on Windows

- **URL / path:** Microchip nEDBG VID/PID list
  https://onlinedocs.microchip.com/oxy/GUID-EDDD9E66-AD52-4A30-9D74-A0CB1B4FFFE6-en-US-2/
  + ARM CMSIS-DAP reference; existing `openocd.cfg` comment line
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Known VID/PIDs:
  - nEDBG (Curiosity Nano / Xplained-Pro Nano): VID `0x03EB` PID `0x2175`
  - EDBG (Atmel-ICE / Xplained-Pro full): VID `0x03EB` PID `0x2141`
  - SEGGER J-Link family: VID `0x1366` various PIDs (`0x0101`, `0x0105`, …)
  - ST-Link V2: VID `0x0483` PID `0x3748`
  - ST-Link V2-1: VID `0x0483` PID `0x374B`
  - ST-Link V3: VID `0x0483` PID `0x374E`/`0x374F`
  - Black Magic Probe: VID `0x1d50` PID `0x6018`/`0x6017`
  - Generic DAPLink: VID `0x0d28` PID `0x0204`
  On Windows, look in Device Manager → "USB devices" by Hardware ID
  (`USB\VID_xxxx&PID_yyyy`), or use `pnputil /enum-devices` /
  `Get-PnpDevice`. Existing project openocd.cfg already documents nEDBG as
  `VID 0x03eb PID 0x2175`.
- **Confidence: HIGH**
  Vendor docs + project's existing openocd.cfg (already verified by Walid).
- **Implication for our build:**
  Walid can identify whichever probe he plugs in by VID/PID before running
  `make flash`, so we know in advance whether `cmsis-dap`, `jlink`, or
  `stlink` is the right driver. If two cmsis-dap devices are present
  (e.g. nEDBG + Atmel-ICE), pin the right one with
  `cmsis_dap_vid_pid 0x03eb 0x2175` (nEDBG) or `0x03eb 0x2141`
  (Atmel-ICE).
- **Why I'm recording it:**
  Question 18.

---

## Source 18: Microchip CMSIS-DAP Switcher (correction to Sources 13 + 14)

- **URL / path:** https://developerhelp.microchip.com/xwiki/bin/view/software-tools/ides/extensions/cmsis-dap/
  + https://developerhelp.microchip.com/xwiki/bin/view/software-tools/programmers-and-debuggers/cmsis-dap/
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Microchip publishes a **CMSIS-DAP Switcher** firmware utility
  (`pycmsisdapswitcher`) that re-flashes the on-board firmware of certain
  Microchip programmers so they enumerate as CMSIS-DAP v2 (WinUSB / bulk)
  instead of Microchip's proprietary protocol. Verbatim:
  `"the following Microchip programmer/debugger tools support CMSIS-DAP
  switching: MPLAB PICkit 5, MPLAB PICkit 4, MPLAB PICkit Basic, MPLAB
  Snap, Development boards incorporating the PKoB4 debugger"`
  Once switched, these probes work with OpenOCD via `adapter driver
  cmsis-dap`, identically to an Atmel-ICE or nEDBG. SAMD21 is supported
  because CMSIS-DAP itself is target-agnostic for any ARM Cortex-M device
  reachable via SWD.
- **Confidence: HIGH**
  Microchip Developer Help, primary source, recent.
- **Implication for our build (REVISES Sources 13 + 14):**
  - **PICkit 5** with CMSIS-DAP firmware = WORKS via OpenOCD, no `openocd.cfg`
    change beyond optionally adding `cmsis_dap_vid_pid` for the PICkit 5's
    PID. Without the firmware switch (factory default), it does NOT work
    with OpenOCD. Procedure: install pycmsisdapswitcher, run
    `pycmsisdapswitcher cmsis-dap` against the probe, then OpenOCD sees it.
  - **MPLAB SNAP** — same story: requires CMSIS-DAP switcher firmware to
    work with OpenOCD; otherwise MPLAB X / IPE only.
  - **MPLAB ICD 5 / ICD 4 / PICkit 4 / PKoB4 onboard debuggers**: all
    eligible for CMSIS-DAP switching too.
  - **Atmel-ICE and nEDBG (Curiosity Nano)** are NOT in the switcher
    list because they are CMSIS-DAP **already** (the EDBG firmware
    exposes the CMSIS-DAP HID class natively — no switch needed).
- **Why I'm recording it:**
  Important correction. PICkit 5 and SNAP are not "useless for OpenOCD" —
  they require a one-time firmware switch. Critical to flag because if
  Walid only has a SNAP or PICkit 5, he can still use OpenOCD, just with
  a 5-minute setup step.

---

## Source 19: J-Link EDU Mini target power confirmation

- **URL / path:** https://forum.segger.com/index.php/Thread/3615-J-Link-5V-Supply/
  + https://www.segger.com/products/debug-probes/j-link/models/j-link-edu-mini/
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  SEGGER official: J-Link EDU Mini's VTREF is purely an INPUT (sense pin),
  the EDU Mini can not drive target power on any pin. "It can only work
  reliably with ~3.3 Voltage targets" — outside that range, the level
  shifters may misbehave. J-Link Plus / Pro can supply 5 V on pin 19 of
  the 20-pin header but only when explicitly enabled with the
  `power on` / `power on perm` J-Link Commander command.
- **Confidence: HIGH**
  Vendor forum, SEGGER employee answer.
- **Implication for our build:**
  If Walid uses any J-Link, he must independently power the PCU board's
  3.3 V rail before SWD attach. Same applies to ST-Link clones and
  CMSIS-DAP clones from AliExpress. The ONLY probes that can power our
  PCU board through the cable are: Curiosity Nano nEDBG (via VTG line, but
  only if the on-board G17D is held cold — see Source 4 cut-strap caveats),
  Atmel-ICE (via Cortex-10 VTref pin if explicitly enabled in Atmel Studio
  / MPLAB), and Black Magic Probe (jumper-selectable 3.3 V).
- **Why I'm recording it:**
  Question 6/7/14 — target-power column.

---

## Source 2: Tag-Connect family variant breakdown (search comparison)

- **URL / path:** https://www.tag-connect.com/product/tc2050-idc-tag-connect-2050-idc
  (and search result aggregation including `/tc2050-idc-050`, `/tc2050-idc-nl-050`,
  `/tc2050-idc-nl-050-all`, `/tc2050-arm2010`)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  The TC2050 family has three orthogonal variants:
  1. **Legs vs No-Legs** — `TC2050-IDC` (legged, self-retaining) vs
     `TC2050-IDC-NL` (no legs, smallest footprint, needs hand or `TC2050-CLIP`).
  2. **Programmer-end connector pitch** — the plain `-IDC` suffix is `0.1"`
     (2.54 mm) ribbon (10-way IDC, fits older debuggers like Atmel-ICE legacy
     header, MPLAB ICDs); the `-050` suffix is `0.05"` (1.27 mm) ribbon for
     the modern ARM Cortex 10-pin micro-header (Samtec FTSH-105-01,
     used on J-Link, ULINK2, Atmel-ICE Cortex cable).
  3. **All-pins vs default** — Verbatim from the `-050` page:
     `"Pins 5 and 9 of the TC2050 are not connected"` in default `-050`;
     `-050-ALL` connects them. Plain `-IDC` and `-IDC-NL` (the 0.1"
     variants) are wired straight-through (all pins).
  Adapters exist: `TC2050-ARM2010` adapts the 0.1" TC2050-IDC head to the
  ARM 20-pin 0.1" JTAG/SWD header.
- **Confidence: HIGH**
  Multiple consistent product-page snippets from tag-connect.com.
- **Implication for our build:**
  We need to know which programmer-side connector the board's documentation
  expects. The board calls the footprint `TC2050-IDC-NL` (not `-050`), which
  by Tag-Connect's own naming convention means it expects a cable that
  terminates in `0.1"` IDC on the programmer side. So:
  - For the **Curiosity Nano nEDBG / Atmel-ICE / J-Link via 20-pin**, we'd
    use `TC2050-IDC-NL` (or `-IDC`) plus appropriate adapter or direct fit.
  - For modern probes that use the 0.05" Cortex 10-pin micro-header
    (J-Link Plus, J-Link EDU, ST-Link V3, modern Atmel-ICE Cortex cable),
    we'd want `TC2050-IDC-NL-050` or `-050-ALL`.
  Walid should verify which cable (0.1" vs 0.05" termination) he has BEFORE
  buying a probe — they are not interchangeable without an adapter.
- **Why I'm recording it:**
  Anti-pattern guard: the suffix tells you which programmer-end connector,
  not just whether legs are present. Easy to confuse.

---
