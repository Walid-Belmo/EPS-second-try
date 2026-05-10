# Research Log — Agent C: SAMD21J17D errata, silicon revisions, and SWD programming compatibility

Purpose: Answer whether there are any silicon errata, silicon-revision
differences, or SWD-programmer-side gotchas that affect the ATSAMD21J17D
specifically, and whether they differ from the ATSAMD21G17D the team has
already programmed successfully on the Curiosity Nano dev board. Also
confirm OpenOCD's handling of the J17D part (chip ID / target script /
flash driver). The downstream build decision is whether the existing
`openocd.cfg` will work on the new chip, and whether any errata workaround
needs to be pre-applied in firmware before we flash the EPS mainboard.

Ground rules:
- Prefer official primary sources (Microchip errata document for SAMD21,
  OpenOCD source / docs, Microchip developer help) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-04-26.

---

## Source 1: Microchip SAM D21/DA1 Family Silicon Errata DS80000760M (latest, 2025)

- **URL / path:** https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/SAM-D21DA1-Family-Silicon-Errata-and-Data-Sheet-Clarification-DS80000760.pdf
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This is the canonical Microchip errata document for the SAM D21 family, latest revision **M, copyright 2025**. Doc number `DS80000760M`. It conforms to data sheet `DS40001882K`.

  CRITICAL — Section 1 opening line: `"The device variant (last letter of the ordering number) is independent of the die revision (DSU.DID.REVISION): The device variant denotes functional differences, whereas the die revision marks evolution of the die."` So the trailing `D` in `ATSAMD21J17D` is a **device variant**, NOT a silicon revision. (Variant `D` is documented in the SAMD21 datasheet as the "automotive" / extended-temperature / re-spun variant.)

  Table 1 (Device Identification) shows DID values:
  - `ATSAMD21J17D` = **DID = 0x10012x92** (rev letter only available as G=0x6 and I=0x8; A,B,C,D,E,F,H,J = N/A)
  - `ATSAMD21G17D` = **DID = 0x10012x93** (same rev pattern as J17D: G=0x6, I=0x8)
  - `ATSAMD21J17A` = DID = 0x10010x01 (rev A=0x0 ... D=0x3; E,F,G N/A; H=0x7)
  - `ATSAMD21G17A` = DID = 0x10010x06
  - `ATSAMD21J17L` = DID = 0x10012x96 (rev G=0x6, I=0x8)
  So DID[31:0] of the J17D will read as **0x10012692** (rev G) or **0x10012892** (rev I) depending on die rev. G17D will read 0x10012693 / 0x10012893. They differ only in the low nibble of the DEVSEL field (0x92 vs 0x93).

  Table 3 — Errata Summary lists every erratum with module, item number, summary, and which silicon revisions (A/B/C/D/E/F/G/H/I/J) are affected. Errata are family-wide with revision-letter scoping; **none of them is scoped to "J variant only" or "G variant only" — every erratum applies to both pinouts identically when their shared revision letter is affected.**

  Specifically verified items:
  - **1.2.1 "Write Access to DFLL Register":** `"The DFLL clock must be requested before being configured; otherwise, a write access to a DFLL register can freeze the device. Workaround: Write a '0' to the DFLL ONDEMAND bit in the DFLLCTRL register before configuring the DFLL module."` — **Affects ALL revisions A through J** (every cell in the row marked X). This is exactly the erratum that the project's `notes/samd21_clocks.md` references.
  - **1.5.9 "Program and Debug Interface Disable":** `"PDID is not available on some silicon revisions."` Affects revs A, B, C, D, F. Not E, G, H, I, J.
  - **1.5.1 APB Clock**, **1.5.7 Standby Entry**, **1.5.8 Standby Wake-up** — affect all revs.
  - **1.13.1, 1.13.3, 1.13.4** — PA24/PA25 input/pull/pull-down quirks (USB pins). Family-wide.
  - **1.5.4 NVM User Row Mapping Value for WDT** — affects only revs E and F.
  - No NVMCTRL erratum that distinguishes flash/row size by package.

- **Confidence: HIGH**
  Primary source. Latest revision (M). Copyright 2025. Read directly from the PDF, no transcription chain.

- **Implication for our build:**
  1. The `D` suffix is **device variant only**, NOT silicon revision. The J17D and the G17D the team already programs are the same variant family.
  2. Every erratum in the document applies to BOTH G17D and J17D; there are no J-specific or pin-count-specific errata.
  3. Errata 1.2.1 (DFLL ONDEMAND) applies to **every silicon revision**, so the existing workaround in our code (write `DFLL ONDEMAND = 0` before configuring) is equally needed on the J17D — no change required.
  4. To distinguish G17D from J17D programmatically read DID, low byte: `0x92` = J17D, `0x93` = G17D.
  5. Silicon revisions actually shipped for the `D` variant are revs **G and I** only (table shows N/A for everything else). So a "new from factory" J17D today should be either rev G or rev I.

- **Why I'm recording it:**
  Authoritative answer to questions 1, 2, 3, 4, 5 in one source. Establishes the bedrock fact that the trailing `D` ≠ silicon revision.

---

## Source 2: Revision History page from DS80000760M (same PDF, pages 33-35)

- **URL / path:** Same as Source 1 (Appendix A: Revision History).
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Tracks how silicon revisions evolved in the document.
  - Rev A (4/2018) — initial release, only silicon revs A, B existed
  - Rev L (01/2025) — `"Updated the document throughout to reflect new silicon revisions H, I, and J"` — so revs H, I, J are recent additions to circulation
  - Rev M (04/2025) — minor wording fix for `1.5.9 Program and Debug Interface Disable` (was "One-Time Programmable Lock")
  - Section 2 Data Sheet Clarifications: `"There are no new Data Sheet Clarifications to report."` so the data sheet DS40001882K stands as-is.

- **Confidence: HIGH** — same primary source.

- **Implication for our build:**
  - Silicon revs H, I, J are recently introduced (post-2024). This is consistent with the J17D shipping today as silicon rev G or rev I.
  - The 1.5.9 erratum that was renamed in Rev M is the "Program and Debug Interface Disable" — present on revs A through G but **NOT** present on revs H, I, J. So a brand-new J17D rev I should NOT have PDID issues.

- **Why I'm recording it:**
  Confirms the silicon revision timeline and that newer revs (G, I, which is what J17D ships as) have FEWER errata than older revs.

---

## Source 3: Project's existing DFLL errata workaround (clock_configure_48mhz_dfll_open_loop.c)

- **URL / path:** `c:\Users\iceoc\Documents\EPS-second-try\src\drivers\clock_configure_48mhz_dfll_open_loop.c` (lines 10-21, 64-115)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Project's existing workaround for the DFLL ONDEMAND bug. Comment says:
  `"ERRATA 1.2.1 WORKAROUND: The DFLL ONDEMAND bit is set by default after reset. If ANY DFLL register (DFLLVAL, DFLLMUL, etc.) is written while ONDEMAND is set, the device hangs..."` Refs `Microchip Errata DS80000760G, Section 1.2.1`. The fix: write DFLLCTRL with ENABLE (which clears ONDEMAND because the entire register is written and ONDEMAND bit is left as 0) BEFORE touching any other DFLL register. This was confirmed working on G17D Curiosity Nano (proved out in code_samples 02 and 03).

- **Confidence: HIGH** — direct read of project source.

- **Implication for our build:**
  - The project's workaround is correct and applies equally to J17D. **Source 1 confirms 1.2.1 affects revs A through J — every revision ever shipped.** No code change needed.
  - The comment cites the older `DS80000760G` doc; the latest is M, but the erratum text and workaround are identical. (Cosmetic: could update reference to `DS80000760M` but not required.)

- **Why I'm recording it:**
  Answers question 3 directly and confirms the existing workaround is portable.

---

## Source 4: OpenOCD `at91samd.c` flash driver source (master branch)

- **URL / path:** https://raw.githubusercontent.com/openocd-org/openocd/master/src/flash/nor/at91samd.c (read directly via curl)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Verified by direct grep of the actual source file. Lines 144-192 of `samd21_parts[]`:
  ```c
  static const struct samd_part samd21_parts[] = {
    { 0x1,  "SAMD21J17A", 128, 16 },
    { 0x6,  "SAMD21G17A", 128, 16 },
    ...
    { 0x92, "SAMD21J17D", 128, 16 },   // <-- our new target
    { 0x93, "SAMD21G17D", 128, 16 },   // <-- existing target on Curiosity Nano
    { 0x96, "SAMD21G17L", 128, 16 },
  };
  ```
  Both J17D (0x92) and G17D (0x93) are first-class entries with identical 128 KB flash / 16 KB RAM. Match is by DEVSEL = low byte of DID register.

  Driver behavior: Page size and row size are READ DYNAMICALLY from the chip via `NVMCTRL_PARAM` register at flash-bank-probe time. There is **no per-variant override and no special-case code** for J vs G or A vs D variants. Chip-erase uses DSU (~240 ms). No automotive- or D-specific unlock sequence.

- **Confidence: HIGH**
  Fetched directly from upstream openocd-org repo (master). Verified by line-numbered grep, not just summary.

- **Implication for our build:**
  - The user's existing `openocd.cfg` (which sources `target/at91samdXX.cfg`) **will work as-is** for the J17D. The flash bank line in `at91samdXX.cfg` is `flash bank $_FLASHNAME at91samd 0x00000000 0 1 1 $_TARGETNAME`, and the driver autodetects the part from DID. J17D (0x92) is in the table.
  - No `openocd.cfg` change needed for the part itself.
  - Possible cosmetic improvement: the existing `adapter speed 4000` (4 MHz) overrides the default `adapter speed 400` in the target script. That's fine — it worked on G17D and the J17D Cortex-M0+ DAP runs at the same max SWD clock.

- **Why I'm recording it:**
  Answers questions 6 and 8 — confirms OpenOCD support is identical for both parts and the existing config is portable.

---

## Source 5: OpenOCD `tcl/target/at91samdXX.cfg` target script (master branch)

- **URL / path:** https://raw.githubusercontent.com/openocd-org/openocd/master/tcl/target/at91samdXX.cfg
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This is the script the user's `openocd.cfg` sources. Key lines:
  - `transport select swd` — this driver is SWD-only, no JTAG.
  - `set _CPUTAPID 0x4ba00477` — Cortex-M0 expected DAP IDCODE. (This is the standard ARM Cortex-M0 / M0+ ID and is identical for J17D and G17D — both are M0+.)
  - `$_TARGETNAME configure -event reset-deassert-post { at91samd dsu_reset_deassert }` — DSU-based reset handling.
  - Comment: `"SAMD DSU will hold the CPU in reset if TCK is low when RESET_N deasserts"` — this is part of the chip's "cold-plug" mechanism for protected/locked devices.
  - `cortex_m reset_config sysresetreq` — uses SYSRESETREQ rather than physical SRST. This works equally on G17D and J17D since SRST availability depends on the BOARD wiring, not the chip.

- **Confidence: HIGH** — fetched directly from upstream.

- **Implication for our build:**
  - The script does not bind to a specific package or pin count. It works with any SAMD/SAMR/SAML/SAMC Cortex-M0+.
  - On the EPS PCU V4.1 board, if SRST/RESET_N is NOT broken out to the SWD header, the `sysresetreq` fallback covers it. Recommend the user verify whether RESET is wired to the programming header — if not, it's still fine.

- **Why I'm recording it:**
  Confirms the target script does not encode any J-vs-G assumption.

---

## Source 6: Project's existing AP-stall recovery doc (`docs/how_to_recover_from_stalled_debug_port.md`)

- **URL / path:** `c:\Users\iceoc\Documents\EPS-second-try\docs\how_to_recover_from_stalled_debug_port.md`
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This documents a real incident on 2026-04-01 where bad clock code on the G17D Curiosity Nano caused an AP stall (`SWD DPIDR 0x0bc11477` then `Connecting DP: stalled AP operation, issuing ABORT`). OpenOCD could not recover, but **MPLAB X IDE → Production → Erase Device Memory** did. The doc lists what does NOT work: power cycling, OpenOCD `at91samd chip-erase`, OpenOCD `connect_assert_srst`, pyOCD, MPLAB IPE command-line, lowering SWD clock.

- **Confidence: HIGH** — direct read of project doc, written by the team.

- **Implication for our build:**
  - This same risk exists on the J17D (it's the same chip family, same DSU, same DAP) — bad clock code can cause AP stall on either part.
  - If the J17D PCU board has its programming header connected to the Curiosity Nano nEDBG (with cut straps), MPLAB X IDE recovery still works because the same nEDBG firmware is involved.
  - **Caveat:** the doc says MPLAB X erase requires opening "any project targeting ATSAMD21G17D". For the J17D PCU board, the user will need to create a placeholder MPLAB X project targeting `ATSAMD21J17D` to perform the erase. The DEVSEL is different (0x92 vs 0x93), so MPLAB X will refuse to erase if the project target doesn't match.
  - Recommend: pre-create an empty MPLAB X project targeting ATSAMD21J17D as a recovery tool BEFORE first flashing the EPS PCU board.

- **Why I'm recording it:**
  Important pre-flash safety net. Establishes recovery procedure for question 9.

---

## Source 7: SAM D21 Curiosity Nano Evaluation Kit User's Guide DS70005409D (2020)

- **URL / path:** https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/UserGuides/SAMD21-Curiosity-Nano-Evaluation-Kit-User-Guide-DS70005409D.pdf
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This is **revision D**, copyright 2020 (project notes had earlier said "G" — the project notes are wrong, the current published version is D). Key sections:

  **Section 3.4 "Disconnecting the On-Board Nano Debugger"** (p. 9-10):
  - The on-board SAMD21G17D target is connected to the nEDBG through **six cut straps** on the bottom side of the board, plus a separate **VTG strap** on the top side for target power.
  - The cut straps carry: `DBG0` (SWDATA), `DBG1` (SWCLK), `DBG2` (GPIO/SW0), `DBG3` (nRESET), `CDC TX`, `CDC RX`.
  - Quote: `"By cutting the GPIO straps with a sharp tool ... all I/Os connected between the debugger and the SAMD21G17D can be disconnected. To disconnect the target regulator, cut the VTG strap."`
  - Note 2: `"Solder in 0Ω resistors across the footprints or short-circuit them with tin solder to reconnect any cut signals."`
  - Crucially: `"The signals will also be disconnected from the board edge next to the On-Board Nano Debugger section."` — this means the SWD pads on the EDGE of the Curiosity Nano (labeled DBG0/DBG1/DBG2/DBG3 + VTG + GND) are wired DIRECTLY to the nEDBG and cutting the straps frees them from the on-board target.

  **Curiosity Nano Standard Pinout (Section 3.2)**, mapping debugger-side pins to standard ICSP target pins (Table 3-2):
  - `DBG0` → `SWDATA`
  - `DBG1` → `SWCLK`
  - `DBG2` → `GPIO` (DGI GPIO, optional)
  - `DBG3` → `nRESET`
  - `VTG` → target voltage (1.7-3.6 V, set by MIC5353 LDO controlled by debugger firmware)
  - `GND` → ground
  - `CDC RX/TX` → UART bridge

  **Section 3.3.2 External Supply** (p. 8):
  - `"When the Voltage Off (VOFF) pin is shorted to ground (GND), the On-Board Nano Debugger firmware disables the target regulator and it is safe to apply an external voltage to the VTG pin."`
  - Warning: applying voltage to VTG without shorting VOFF→GND causes permanent damage. Absolute max external voltage is 5.5 V.

- **Confidence: HIGH** — official user guide, latest published revision.

- **Implication for our build (this is the answer to question 10):**
  The Curiosity Nano DM320119 **CAN** be used as a standalone CMSIS-DAP programmer for an external SAMD21J17D target. Procedure:

  1. **Cut the 6 GPIO straps + VTG strap** on the Curiosity Nano (or solder jumper wires from the back-side strap pads to the edge connector if reversibility matters).
  2. Wire the Curiosity Nano edge pads to the EPS PCU V4.1 SWD header:
     - `DBG0` (Curiosity Nano edge) → `SWDIO` (PCU board)
     - `DBG1` (Curiosity Nano edge) → `SWCLK` (PCU board)
     - `DBG3` (Curiosity Nano edge) → `nRESET` (PCU board) — optional but recommended
     - `GND` ↔ `GND` (mandatory)
     - **Power option A:** if PCU has its own 3.3 V supply, leave VTG disconnected (the level shifters in the nEDBG still need to know the target voltage — they have a sense pin called `VTG` that detects the target's logic level; just connect VTG to the PCU's 3.3 V rail as a SENSE input, no current draw).
     - **Power option B:** use Curiosity Nano's internal MIC5353 LDO to power the PCU at 3.3 V via VTG (max 500 mA — fine for SAMD21 only, NOT enough to power the EPS PCU's full circuit).
  3. The nEDBG enumerates as a CMSIS-DAP probe (VID 0x03eb PID 0x2175, same as today) regardless of whether the strap is cut. **No firmware changes are needed on the nEDBG itself.**
  4. The user's existing `openocd.cfg` will continue to work — it already targets a CMSIS-DAP CDC-NEDBG probe and loads the at91samd flash driver, which auto-detects DEVSEL = 0x92 = J17D.

  Two practical cautions:
  - On the EPS PCU V4.1 board, ensure the PCU's own 3.3 V rail is established BEFORE connecting SWD/VTG, OR power the PCU through VTG. Connecting only SWCLK/SWDIO to an unpowered SAMD21J17D will cause the SWD lines to clamp through the chip's ESD diodes and the nEDBG level shifters.
  - The VTG line is bidirectional — the nEDBG senses the target voltage on it. The level shifters need this to operate. If you forget to connect VTG (or short VOFF to GND while not feeding a voltage), the SWD lines will be stuck at logic 0 and OpenOCD will fail to connect with "DPIDR read failed" or similar.

- **Why I'm recording it:**
  Definitively answers question 10 — yes, the Curiosity Nano can program the J17D on the PCU board, and gives the wiring procedure.

---

## Source 8: SAM D21/DA1 Family Data Sheet DS40001882H — Section 3 "SAM D21 Ordering Information"

- **URL / path:** `c:\Users\iceoc\Documents\EPS-second-try\datasheets\samd21_datasheet.pdf` (page 16)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Definitively explains the meaning of every letter in `ATSAMD21J17D`:
  - `ATSAMD` = product family (general-purpose Microchip MCU)
  - `21` = product series (Cortex-M0+ basic feature set + DMA + USB)
  - `J` = pin count → `"J = 64 Pins"`
  - `17` = flash density → `"17 = 128 KB"`
  - `D` = device variant → `"D = Silicon Revision G with RWWEE Support in 128KB memory options"`

  Other variant letters in the table:
  - `A = Default Variant`
  - `B = Added RWWEE support for 32 KB and 64 KB memory options`
  - `C = Silicon revision F for WLCSP45 package option`
  - `L = Pinout optimized for Analog and PWM`

  Then the suffix block (`-MUT` etc.):
  - First letter: package grade — `U = -40-85°C`, `N = -40-105°C`, `F = -40-125°C`, `Z = AEC-Q100`
  - Second letter: package type — `A = TQFP`, `M = VQFN`, `MM = 64-Lead VQFN`, `U = WLCSP`, `C = UFBGA`
  - Third (optional) letter: package carrier — blank = tray, `T = tape and reel`

  The user's PCU V4.1 carries an `ATSAMD21J17D-MUT` → 64-pin J variant + 128 KB flash + variant D (silicon rev G with RWWEE) + `-MU` (-40 to 85°C in VQFN package) + `T` (tape and reel).

- **Confidence: HIGH** — primary datasheet, ordering-info section.

- **Implication for our build:**
  - The trailing `D` in the part number is the **variant code** (a marketing/feature designator), NOT the silicon revision letter (which is a separate field DSU.DID.REVISION).
  - However, the datasheet itself ties variant `D` to "Silicon Revision G" at the time of introduction. The errata document's Table 1 confirms that in 2026 the `D` variant ships as die rev G or die rev I (newer) — both are in circulation.
  - Practical consequence: when the user reads `DSU.DID` on the new chip, the **REVISION** field will be `0x6` (rev G) or `0x8` (rev I). The G17D Curiosity Nano typically reads rev G (0x6). The new J17D could be rev G or rev I.
  - **No erratum applies only to rev G or rev I** that doesn't also apply to the G17D — except some VERY OLD-rev-only errata that don't apply to either chip.

- **Why I'm recording it:**
  Closes the question on what `D` means and reconciles the apparent tension between the errata's "variant ≠ revision" statement and the datasheet's "D = Silicon Revision G" definition. The reconciliation: the variant letter is fixed at `D`, the die revision continues to evolve.

---

## Source 9: Datasheet DS40001882H — Section 2 "Configuration Summary" — feature comparison J17D vs G17D

- **URL / path:** `c:\Users\iceoc\Documents\EPS-second-try\datasheets\samd21_datasheet.pdf` (pages 14-15)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Side-by-side row comparison from Table 2-1:

  | Feature | ATSAMD21G17D | ATSAMD21J17D |
  |---|---|---|
  | Program Memory (KB) | 128 | 128 |
  | Data Memory / SRAM (KB) | 16 | 16 |
  | Pins | 48 | 64 |
  | Packages | QFN, TQFP | QFN, TQFP, UFBGA |
  | Internal oscillators | OSC32K, OSCULP32K, OSC8M, DFLL48M, FDPLL96M | (identical) |
  | External oscillators | XOSC32K, XOSC | (identical) |
  | USB | Y | Y |
  | SERCOM | 6 | 6 |
  | TC instances | 5-2 | 5-2 |
  | TCC | 4 | 4 |
  | TCC waveform channels | 8/4/2/8 | 8/4/2/8 |
  | I2S | Y | Y |
  | DMA channels | 12 | 12 |
  | RTC | Y | Y |
  | WDT | Y | Y |
  | EVSYS channels | 12 | 12 |
  | External interrupt lines | 16 | 16 |
  | I/O Pins | 38 | **52** |
  | ADC channels | 14 | **20** |
  | Analog Comparator | 2 | 2 |
  | DAC | Y | Y |
  | PTC mutual/self | 120/10 | **256/16** |

  Differences: J17D has more pins exposed (52 vs 38 GPIOs), more ADC channels routed out (20 vs 14), more PTC channels (256/16 vs 120/10), and adds the UFBGA package option.

  Memory architecture is **byte-for-byte identical**: 128 KB flash, 16 KB SRAM, NVM page size 64 bytes, NVM row size 256 bytes (4 pages × 64). This follows from both being in the same "17 = 128 KB" flash row.

- **Confidence: HIGH** — primary datasheet feature table.

- **Implication for our build:**
  - **NVM programming procedure is identical between G17D and J17D.** Same page size, same row size, same NVMCTRL register layout. OpenOCD's at91samd flash driver dynamically reads page size from NVMCTRL_PARAM at probe time, so this is auto-handled.
  - The ONLY differences are pin count and analog channel routing. None of these affect SWD or programming.
  - Firmware port consideration (out of scope for this question, but worth flagging): peripheral instance counts and clocks are identical. Pin assignments WILL differ — the user must verify every pin's location on the J variant pinout. But that is a code-port issue, not a flash/program issue.

- **Why I'm recording it:**
  Definitively rules out any J-vs-G difference in NVM programming, answering question 7.

---

## Source 10: Microchip Developer Help — SAMD21 factory-fresh chip programming

- **URL / path:** WebSearch + Microchip product pages (`https://www.microchip.com/en-us/product/atsamd21j17`)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Combined evidence from the WebSearch results and from the at91samdXX.cfg analysis:
  - SAMD21 chips ship factory-blank (all-`0xFF` flash). There is **no factory bootloader** in flash. Some Adafruit/SparkFun boards ship with the SAM-BA bootloader pre-programmed, but those are board-vendor-installed bootloaders, not Microchip-installed.
  - Factory NVM USER ROW defaults: BOOTPROT=0 (no boot protection), EEPROM=7 (no EEPROM emulation reserved), BOD33 enabled, WDT disabled. These defaults DO NOT block SWD access.
  - The NVMCTRL.SECURITY bit is cleared from factory — the chip is unlocked. SWD will respond on first connect.
  - The DSU "cold-plugging" sequence in OpenOCD's `at91samd dsu_reset_deassert` event handles the case where the DSU holds the CPU in reset extension; this is automatically invoked on SWD reset and works for blank chips too.
  - Erratum 1.8.1 (`Debugger and DSU Cold-plugging Procedure`, only affects revs A-D — NOT G or I) is the historical reason for that handler. Since the J17D ships as rev G or I, this old quirk does not apply.

- **Confidence: MEDIUM** — composite of multiple sources; not a single authoritative quote.

- **Implication for our build:**
  - A brand-new factory ATSAMD21J17D will respond to SWD on first attempt. No fuse changes needed before flashing.
  - The user does NOT need to chip-erase before first flash. (`make flash` writes the program directly; OpenOCD writes to flash banks normally.)
  - If the chip has been previously locked (NVMCTRL set SECURITY bit), recovery is via DSU chip-erase: `at91samd chip-erase` in OpenOCD or MPLAB X "Erase Device Memory". For a factory chip this is unnecessary.

- **Why I'm recording it:**
  Answers question 9 — no factory bootloader, no fuse adjustments, SWD works out of the box.

---
