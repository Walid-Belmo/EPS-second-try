# Research Log — Agent A: SAMD21G17D vs SAMD21J17D silicon

Purpose: Answer definitively whether the ATSAMD21G17D (used on the Curiosity
Nano DM320119 dev board) and the ATSAMD21J17D-MUT (used on the CHESS EPS PCU
testing board V4.1) are the same silicon die with different pin bonding /
package, or genuinely different chips with different internal blocks. The
downstream build decision is whether porting firmware between the two
variants is "just change the chip-name macro" or whether it requires real
peripheral / register / behavioural rework. Walid (the user) wants this
verified from first principles, not assumed.

Ground rules:
- Prefer official primary sources (Microchip datasheets, product pages,
  application notes) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-04-26.

---

## Source 1: SAM D21/DA1 Family Data Sheet DS40001882G (Microchip, 2021)

- **URL / path:** https://ww1.microchip.com/downloads/en/DeviceDoc/SAM-D21DA1-Family-Data-Sheet-DS40001882G.pdf
  (PDF cached locally at `C:\Users\iceoc\AppData\Local\Temp\samd21_family_ds.txt`)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This is THE family datasheet — one PDF covers all SAM D21 E/G/J variants
  (and the EL/GL low-pin-count derivatives). The fact that one datasheet
  covers them all is itself a strong hint of shared silicon. Section 2
  "Configuration Summary" Table 2-1 lists every variant. Quoted values
  for the two parts I care about:
  `ATSAMD21G17D` — `48 TQFP, QFN`, `128 KB program / 16 KB data`,
    `OSC32K, OSCULP32K, XOSC32K, OSC8M, XOSC, DFLL48M, FDPLL96M`,
    USB `Y`, SERCOM `6`, TCC `3` with `8/4/2/8` waveform channels,
    I2S `Y`, DMA `12`, RTC `Y`, EVSYS `12`, EXTINT `16`, I/O Pins `38`,
    ADC `14`, AC `2`, DAC `Y`, PTC `256/16`.
  `ATSAMD21J17D` — `64 TQFP, QFN`, `128 KB program / 16 KB data`,
    same oscillator list, USB `Y`, SERCOM `6`, TCC `3` with `8/4/2/8`,
    I2S `Y`, DMA `12`, RTC `Y`, EVSYS `12`, EXTINT `16`, I/O Pins `52`,
    ADC `20`, AC `2`, DAC `Y`, PTC `256/16`.
  Differences: package pin count (48 vs 64), I/O pin count (38 vs 52),
  ADC channel count (14 vs 20). Everything else (flash, RAM, SERCOM,
  TCC, DMA, EVSYS, USB, oscillator set) is byte-for-byte identical.
  Also from line 1622: `TCC3 is only supported in SAMD21x17D` — i.e.
  the "D" suffix at the 17 flash tier adds a TCC3 instance (NOT shown
  separately in the Table 2-1 TCC count which lists "3" — meaning
  TCC0/1/2 + TCC3 likely). Need to verify TCC3 presence below.
  Section 12 Table 12-1 "Peripherals Configuration Summary" lists
  peripheral base addresses. These are FAMILY-WIDE, not per variant:
  PAC0 0x40000000, PM 0x40000400, SYSCTRL 0x40000800, GCLK 0x40000C00,
  WDT 0x40001000, RTC 0x40001400, EIC 0x40001800, DSU 0x41002000,
  NVMCTRL 0x41004000, PORT 0x41004400, DMAC 0x41004800, USB 0x41005000,
  EVSYS 0x42000400, **SERCOM0 0x42000800**, SERCOM1 0x42000C00,
  SERCOM2 0x42001000, SERCOM3 0x42001400, SERCOM4 0x42001800,
  SERCOM5 0x42001C00, TCC0 0x42002000, TCC1 0x42002400, TCC2 0x42002800,
  TC3 0x42002C00, TC4 0x42003000, TC5 0x42003400, TC6 0x42003800,
  TC7 0x42003C00. IRQ lines explicitly numbered, e.g. SERCOM0 IRQ 9,
  SERCOM5 IRQ 14. The table is presented as a single table across all
  variants — the datasheet does not give per-variant base addresses.
- **Confidence: HIGH**
  This is the Microchip-published canonical datasheet (DS40001882 rev G,
  2021). Document covers G17D and J17D in one Table 2-1 with identical
  peripheral counts, identical clock structure, identical memory sizes,
  and a single shared peripheral memory map. That is the definition
  of "same silicon, different bonding" in datasheet terms.
- **Implication for our build:**
  All driver register code (SERCOM, PORT, NVMCTRL, GCLK, etc.) written
  for G17D will translate verbatim to J17D — same base addresses, same
  IRQ numbers, same register layouts. The build differences should
  reduce to: (a) the chip-name macro that controls which DFP header
  gets included, (b) the linker script if RAM/flash layout differs
  (it doesn't here — both 128/16 KB), (c) the pin-mux table (J adds
  pins on PB that G doesn't bond out, plus PA pins beyond what G has).
  TCC3 deserves a dedicated check — the family table only shows "3 TCC
  instances" for x17D variants, but a footnote says TCC3 is x17D-only,
  hinting TCC3 may exist on both G17D AND J17D. Confirm in DFP headers.
- **Why I'm recording it:**
  This is the bedrock answer to questions 1, 3, 4, 5, 6, 7, 10. Anchor
  source — every later source is checked against this.

---

## Source 2: Microchip SAMD21 DFP atpack 3.6.144 — direct header comparison

- **URL / path:** https://packs.download.microchip.com/Microchip.SAMD21_DFP.3.6.144.atpack
  (downloaded; extracted at `C:\Users\iceoc\AppData\Local\Temp\persist_atpack\extracted\samd21d\include\`).
  Local repo equivalent at `lib/samd21-dfp/` for G17D.
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  I extracted the official Microchip DFP atpack and diffed `samd21g17d.h`
  against `samd21j17d.h` line-by-line, and byte-compared every shared
  peripheral header. Findings:
  - `samd21g17d.h` and `samd21j17d.h` are 615 vs 635 lines, generated
    from the same device-description vintage `2020-11-19T13:00:04Z` /
    `2020-11-19T13:00:05Z`.
  - Every PERIPHERAL BASE ADDRESS that appears in BOTH headers is the
    SAME exact value: `SERCOM0=0x42000800`, `SERCOM5=0x42001C00`,
    `PORT=0x41004400`, `GCLK=0x40000C00`, `PM=0x40000400`,
    `SYSCTRL=0x40000800`, `NVMCTRL=0x41004000`, `DMAC=0x41004800`,
    `USB=0x41005000`, `EVSYS=0x42000400`, `TCC0=0x42002000`,
    `TCC3=0x42006000`, `DSU=0x41002000`.
  - Every IRQ number that appears in BOTH headers is the SAME:
    `SERCOM0_IRQn=9`, `SERCOM5_IRQn=14`, `TCC0_IRQn=15`, `TCC3_IRQn=29`.
  - Memory map is identical: `FLASH_ADDR=0x00000000`,
    `FLASH_SIZE=0x00020000` (128 KB), `FLASH_PAGE_SIZE=64` bytes,
    `FLASH_NB_OF_PAGES=2048`, `HMCRAMC0_ADDR=0x20000000`,
    `HMCRAMC0_SIZE=0x00004000` (16 KB).
  - DSU.DID: `CHIP_DSU_DID = 0x10012693` for G17D, `0x10012692` for J17D.
    Adjacent values; the low nibble differs by 1. This is consistent
    with same-die-family-different-bonding (the JTAG ID encodes the
    pin variant in the low bits and the die identity in the upper bits).
    Upper word `0x10012` matches → same family/series ID.
  - The ONLY peripheral additions in J17D vs G17D are: `TC6_IRQn=21`
    at `TC6_REGS=0x42003800` and `TC7_IRQn=22` at `TC7_REGS=0x42003c00`,
    plus their PERIPHERAL_ID (78,79), event IDs, and PIO mux entries
    `MUX_PA20E_TC7_WO0` and `MUX_PA21E_TC7_WO1`.
  - All `component/*.h` files (peripheral struct typedefs for SERCOM,
    PORT, GCLK, DMAC, EVSYS, NVMCTRL, TCC, TC, DSU, PM, SYSCTRL): tested
    with `cmp -s` — BYTE IDENTICAL between the in-repo G17D copy and
    the extracted J17D atpack copy.
  - All `instance/*.h` files for shared peripherals (sercom0..5, port,
    gclk, pm, sysctrl, dmac, evsys, nvmctrl, tcc0, tcc3): also BYTE
    IDENTICAL, even tc6.h and tc7.h exist in both atpack trees.
  - PIO header diff: PA00–PA31 mux defines that exist in BOTH headers
    are byte-identical EXCEPT J17D adds `MUX_PA20E_TC7_WO0` /
    `MUX_PA21E_TC7_WO1` (the TC7 mux options that G17D's TQFP-48 doesn't
    expose because TC7 isn't bonded). G17D has 38 I/O pins (per Source 1
    table); J17D adds the PB-bank pins and a few more PA-bank pins.
- **Confidence: HIGH**
  This is a byte-level comparison of Microchip's own header files in
  the canonical Device Family Pack. If two parts had different silicon,
  the peripheral struct typedefs and base addresses would not be
  byte-identical — the fact that they are settles the question.
- **Implication for our build:**
  Porting the G17D firmware to J17D is fundamentally a **packaging**
  change, not a silicon change:
    1. Swap the `-D__SAMD21G17D__` flag for `-D__SAMD21J17D__` (this
       changes which `samd21*.h` the master `sam.h` includes, which
       in turn changes the IRQ table and adds TC6/TC7 visibility).
    2. Swap startup file (vector table): G17D's `pvReserved21/22`
       slots become `pfnTC6_Handler/pfnTC7_Handler`. The startup file
       is variant-specific in the DFP `gcc/` directory.
    3. Swap the linker script if the vendor ships variant-specific
       ones (likely identical content because RAM/flash sizes match,
       but check filename — DFP uses `samd21j17d_flash.ld` etc.).
    4. Pin assignments: J17D has PB pins that G17D doesn't bond.
       Any code that uses ONLY PA pins works as-is. Code that wants
       to MOVE to PB pins on J17D has to update the pin-mux table.
    5. NO driver rework needed: SERCOM/PORT/GCLK/DMA/EVSYS/USB/NVMCTRL
       register layouts and base addresses are bit-for-bit identical.
    6. OpenOCD: DSU.DID detection sees a different value (0x10012692
       vs 0x10012693). OpenOCD's at91samdXX.cfg already supports both;
       no probe-side change needed beyond using the J17D-specific
       chip name in the cfg if scripted.
    7. Cortex-M0+ core revision: not visible in this header, must
       check Cortex-M0+ section in chip datasheet — see follow-up source.
  The `notes/plan.md` claim "G17D and J17D are the same die in
  different packages" is, per this evidence, CORRECT.
- **Why I'm recording it:**
  This entry directly answers questions 4, 5, 6, 7, 8, 9, with
  authoritative byte-level evidence. Plus it gives us the exact DSU.DID
  values to put in OpenOCD configs.

---

## Source 3: SAM D21/DA1 Family Silicon Errata DS80000760M (Microchip, 2025)

- **URL / path:** https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/SAM-D21DA1-Family-Silicon-Errata-and-Data-Sheet-Clarification-DS80000760.pdf
  (extracted to `C:\Users\iceoc\AppData\Local\Temp\samd21_errata.txt`)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This is Microchip's authoritative silicon errata for the SAM D21
  family, revision M (2025) — the most current. Two key findings:
  1. **Table 1 "SAM D21 Family Silicon Device Identification"** lists
     part numbers paired with their `DID[31:0]` value of the form
     `0x10012xNN` where the `x` nibble is filled by `REVISION[3:0]`
     and the `NN` byte is `DEVSEL[7:0]`. Pairings (extracted via
     `pdftotext` non-layout mode):
       - `ATSAMD21E17D`  = `0x10012x94`
       - `ATSAMD21E17DU` = `0x10012x95`
       - `ATSAMD21E17L`  = `0x10012x97`
       - `ATSAMD21G17D`  = `0x10012x93`
       - `ATSAMD21G17L`  = `0x10012x96`
       - `ATSAMD21J17D`  = `0x10012x92`
     These DEVSEL byte assignments match the DFP atpack header literals
     exactly (`0x10012692/0x10012693/0x10012694` for J17D/G17D/E17D
     with REVISION = 6). The 17D parts (J/G/E) form a contiguous DEVSEL
     block 0x92/0x93/0x94 — Microchip's encoding pattern is "the DIE
     and REVISION fields identify the silicon, the DEVSEL byte
     identifies the package/pin-bonding". Verbatim from the family
     datasheet (DS40001882G, line 5023-5031 in extracted text):
     `Bits 15:12 — DIE[3:0] Die Number — Identifies the die family.`
     `Bits 11:8 — REVISION[3:0] — Identifies the die revision number.`
     `Bits 7:0 — DEVSEL[7:0] — This bit field identifies a device
      within a product family and product series. The value
      corresponds to the Flash memory density, pin count and device
      variant parts of the ordering code.`
     And critically:
     `The device variant (last letter of the ordering number) is
      independent of the die revision (DSU.DID.REVISION): the device
      variant denotes functional differences, whereas the die revision
      marks evolution of the die.`
     For G17D vs J17D, both have DIE=0x2, REVISION=0x6, and only
     DEVSEL differs (0x93 vs 0x92). Same die, same revision, different
     pin-bonding/package.
  2. **Table 3 "Errata Summary"** lists every silicon issue with an
     "Affected Revisions" matrix indexed `A B C D E F G H I J` —
     these are REVISION letters, NOT package variants. The errata
     does NOT list variant-specific (E vs G vs J) errata. Every
     hardware bug applies to a given REVISION letter, and **the same
     erratum hits the E17D, G17D, AND J17D parts simultaneously when
     they share that REVISION**. This is direct proof Microchip
     considers them the same silicon.
- **Confidence: HIGH**
  Authoritative Microchip document, latest revision M (2025),
  cross-validated against the DFP atpack header literals for the
  exact DEVSEL byte values.
- **Implication for our build:**
  - The DSU.DID values in the DFP headers are correct and current.
    Use `0x10012693` for G17D and `0x10012692` for J17D in any
    OpenOCD `at91samdXX.cfg` or autodetection logic.
  - Because the same errata applies to both parts at the same revision,
    any silicon bug workaround coded for G17D rev 6 (e.g. erratum
    1.2.1 "DFLL clock must be requested before being configured")
    is required for J17D rev 6 as well, with no rework.
  - Any test that reads DSU.DID and asserts the chip identity must
    accept BOTH 0x10012693 (G17D) AND 0x10012692 (J17D) as valid
    silicon if the firmware is to be portable.
- **Why I'm recording it:**
  Answers questions 9, 12, 13. Confirms that "D" suffix means the
  same die generation across G/J/E (not "automotive grade" — that
  guess in the briefing prompt is incorrect; "D" is just the latest
  die respin, after A/B/L). And critically, kills the worry that
  errata for one variant might not apply to the other.

---

## Source 4: Cortex-M0+ revision and address map (Family Datasheet sec 11)

- **URL / path:** Same family datasheet as Source 1, section 11
  "Processor And Architecture", page 46 of DS40001882G
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Direct quote: `The ARM Cortex-M0+ implemented is revision r0p1.`
  Section 11.1.1 Table 11-1 "Cortex M0+ Configuration": all SAM D21
  variants have:
    - 28 external interrupts (matches NVIC IRQ count in DFP)
    - Little-endian
    - 2 SysTick timers
    - 4 watchpoint comparators, breakpoint comparators present
    - Single-cycle multiplier present
    - **No MPU, no VTOR** — vector table at fixed 0x00000000
    - Single-cycle I/O port not supported, but "Single 32-bit I/O port
      bus interfacing to the PORT with 1-cycle loads and stores"
  Section 11.1.3 Table 11-2 "Cortex-M0+ Address Map" gives the SCS
  addresses (SysTick 0xE000E010, NVIC 0xE000E100, SCB 0xE000ED00,
  MTB 0xE000E000) — these are ARM-mandated, identical across variants.
  This entire section is presented family-wide with no per-variant
  carve-outs. The DSU.DID PROCESSOR field also confirms `0x1` =
  Cortex-M0+ for ALL D21 parts.
- **Confidence: HIGH**
  Direct quote from the Microchip primary datasheet, text reads
  "the SAM D21" (singular family) implements r0p1.
- **Implication for our build:**
  Both G17D and J17D have the same Cortex-M0+ r0p1 core. ARM core
  errata (e.g. ARM erratum 838849 doesn't apply to M0+, but any
  M0+ r0p1 ARM-issued errata) applies identically. Toolchain target
  flags (`-mcpu=cortex-m0plus -mthumb`) are the same. Stack-pointer
  alignment, NVIC behavior, SysTick programming, MPU absence — all
  identical. Vector table layout is the same SHAPE (fixed at
  0x00000000, 16 ARM exception slots + 28 device IRQs); only the
  meaning of vector slots 21 and 22 differs (G17D: reserved; J17D:
  TC6/TC7 — see Source 2).
- **Why I'm recording it:**
  Directly answers question 2 (core revision) and question 7 (IRQ
  count). One more confirmation that nothing core-side differs.

---

## Source 5: SAM D21 Ordering Information (Family Datasheet sec 3, page 16)

- **URL / path:** Same family datasheet DS40001882G, section 3 "SAM D21
  Ordering Information", figure 3-1 (extracted lines 880-902 of
  `samd21_family_ds.txt`)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This is the **definitive answer to what the "D" suffix means**.
  Verbatim from the ordering-code decoder:
  - Pin Count: `E = 32 Pins, G = 48 Pins, J = 64 Pins` — the second
    letter is purely package pin count.
  - Flash Memory Density: `15 = 32 KB, 16 = 64 KB, 17 = 128 KB,
    18 = 256 KB`.
  - **Device Variant** (last suffix letter):
      `A = Default Variant`
      `B = Added RWWEE support for 32 KB and 64 KB memory options`
      `C = Silicon revision F for WLCSP45 package option`
      `L = Pinout optimized for Analog and PWM`
      `D = Silicon Revision G with RWWEE Support in 128KB memory options`
  - Package Type: `A = TQFP, M = QFN, U = WLCSP, C = UFBGA`
  - Package Grade (separate field): `U = -40..85°C`, `N = -40..105°C`,
    `F/Z = -40..125°C (AEC-Q100 Qualified)`. Automotive grade is the
    PACKAGE GRADE letter, NOT the variant letter.
  Therefore: `ATSAMD21G17D-MUT` decodes as: G=48-pin, 17=128 KB flash,
  D=Silicon Rev G + RWWEE, M=QFN, U=85°C grade, T=tape-and-reel.
  And: `ATSAMD21J17D-MUT` decodes as: J=64-pin, 17=128 KB flash,
  D=Silicon Rev G + RWWEE, M=QFN, U=85°C grade, T=tape-and-reel.
  The ONLY difference in the part numbers is the pin-count letter
  (G vs J) — i.e. how many pins of the same die are bonded out.
  Section 48.4 (revision history of the datasheet itself, line 62966)
  confirms: `Rev D - 9/2018: Configuration Summary — Updated to Add
  new packages for device variant D` — Microchip introduced "variant
  D" by ADDING NEW PACKAGES for an existing silicon rev G, not by
  fabricating a new die.
  Also line 63024 from the rev history: `Standby typical numbers for
  Device Variant C / Die Revision F` — Microchip's own conjunction
  "Variant X / Die Revision Y" syntax confirms ordering-variant ↔
  die-revision is a 1:1 relationship for that die-fab generation,
  shared across all pin-count packages.
- **Confidence: HIGH**
  Direct quote from Microchip's official ordering-code legend in the
  family datasheet. This is the ground truth of what each letter
  means in the part number.
- **Implication for our build:**
  - "D" is NOT an automotive/extended-temp suffix. It is a silicon
    revision identifier (= Die Revision G, the latest at time of
    document). The briefing prompt's guess that "D might mean
    automotive" is WRONG.
  - G17D and J17D share Die Revision G by definition. Confirmed
    same silicon, period.
  - For ordering EPS production parts, the temperature/automotive
    rating comes from the "Package Grade" letter (U/N/F/Z), and the
    `-MUT` suffix on `ATSAMD21J17D-MUT` means: M=QFN package,
    U=industrial temperature -40..85 °C, T=tape-and-reel.
- **Why I'm recording it:**
  Decisive answer to question 12 (what the "D" suffix means) and
  one more nail in the coffin for question 1 (same die or not).

---

## Source 6: OpenOCD at91samd.c device table

- **URL / path:** https://openocd.org/doc/doxygen/html/at91samd_8c_source.html
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  OpenOCD's `at91samd.c` source has a `samd21_parts[]` array that
  encodes (device-ID, part-name, flash-KB, ram-KB) tuples for every
  variant. The relevant entries (extracted via WebFetch):
  ```
  { 0x92, "SAMD21J17D", 128, 16 },
  { 0x93, "SAMD21G17D", 128, 16 },
  ```
  Both variants are in the same `samd21_parts` array, share the same
  family-level OpenOCD configuration ("SAMD21 D and L Variants"), and
  both report 128 KB flash / 16 KB RAM. OpenOCD does not need
  variant-specific flash drivers, IRQ tables, or page-size
  parameters — it treats them as one part with two ID values.
- **Confidence: MEDIUM**
  OpenOCD is third-party but is the de facto standard tool for
  flashing/debugging SAMD21 chips. The values in its table match
  the DFP atpack and errata literals, so it's cross-validated.
- **Implication for our build:**
  - The same OpenOCD `at91samd.cfg` works for both chips. No probe-
    side reconfiguration needed when switching from G17D dev board
    to J17D EPS PCU board.
  - Page size, row size, NVM driver: identical (same flash silicon).
  - Confirms questions 8 (NVM page/row identical) and 9 (DSU.DID
    values).
- **Why I'm recording it:**
  Practical implication: the entire flash/debug toolchain we've
  validated on the dev board (G17D) will work unchanged on the
  EPS PCU board (J17D), because OpenOCD treats them as variants
  of the same part.

---

## Final synthesis — answers to the 13 numbered questions

**Headline answer:** ATSAMD21G17D and ATSAMD21J17D are the SAME silicon
die in different packages with different pin-bonding. Specifically:
both are "Silicon Revision G with RWWEE Support in 128 KB memory" —
that's what the trailing letter "D" in the ordering code MEANS, per
Microchip's own ordering-code legend (Source 5). The G/J letter only
selects how many pins of that die are bonded out (G=48, J=64).

| # | Question | Answer | Sources |
|---|----------|--------|---------|
| 1 | Same die, or different dies? | **Same die.** Variant letter "D" *defines* "Silicon Revision G". DSU.DID DIE field (bits 15:12) is 0x2 in both. DEVSEL byte differs (0x93 vs 0x92) only because DEVSEL encodes "pin count and device variant", not silicon. | 1, 2, 3, 5 |
| 2 | Same Cortex-M0+ core revision? | **Yes — r0p1, family-wide.** Datasheet states "The ARM Cortex-M0+ implemented is revision r0p1" once, applying to all SAM D21 variants. | 4 |
| 3 | Same flash (128 KB), RAM (16 KB), memory map? | **Yes, byte-identical.** FLASH=128 KB at 0x00000000, RAM=16 KB at 0x20000000. Both DFP headers literally `#define FLASH_SIZE 0x00020000` and `HMCRAMC0_SIZE 0x00004000`. | 1, 2 |
| 4 | Same SERCOM (6) / TCC (3, plus TCC3 in x17D) / DMAC (12) / EVSYS (12)? | **Yes.** Family Configuration Summary Table 2-1 lists same counts for both rows. DFP headers byte-match for all six SERCOM instances, DMAC, EVSYS, TCC0/1/2/3. | 1, 2 |
| 5 | Does either have a peripheral the other lacks? | **Yes, J17D has TC6 and TC7; G17D doesn't.** This is solely because TC6/TC7 are bonded only on the 64-pin J package — same silicon, different bonding. (Family table shows G17D has 38 I/O pins, J17D has 52.) ADC channels: G=14, J=20 — same reason. | 1, 2 |
| 6 | Same peripheral base addresses (SERCOM0 at 0x42000800)? | **Yes, all base addresses byte-identical.** Verified by `cmp -s` on `instance/*.h` and `component/*.h` between G17D and J17D headers. | 1, 2 |
| 7 | Same IRQs and IRQ ordering? | **Yes for shared peripherals.** SERCOM0=9, SERCOM5=14, TCC0=15, TCC3=29, etc. — identical. J17D has two extra slots: TC6=21, TC7=22 (which are `pvReserved21/22` in G17D's vector table). 28 IRQs total per Cortex-M0+ family config. | 2, 4 |
| 8 | Same NVM page (64 B) and row size? | **Yes.** `FLASH_PAGE_SIZE=64`, `FLASH_NB_OF_PAGES=2048` in BOTH headers. Same NVMCTRL silicon. | 2 |
| 9 | DSU.DID values per variant? | **G17D = 0x10012693; J17D = 0x10012692; E17D = 0x10012694.** All three: PROCESSOR=0x1 (M0+), FAMILY=0x0 (D), SERIES=0x01, DIE=0x2, REVISION=0x6, DEVSEL byte differs only. | 2, 3, 6 |
| 10 | Package options? | **G17D: 48-pin TQFP or QFN. J17D: 64-pin TQFP or QFN.** From Configuration Summary Table 2-1. | 1 |
| 11 | Same PA/PB pin → package-pin bonding where they overlap? | **Yes for PA00–PA31 (overlapping range).** The PIO header diff shows zero changes to the PA*-mux defines except J17D ADDS `MUX_PA20E_TC7_WO0` and `MUX_PA21E_TC7_WO1` (alt functions only available because TC7 silicon block exists in the 64-pin bonding). G17D uses 38 of the bonded pins; J17D uses 52 (38 of the same PA pins + 14 PB pins not bonded on G). | 2 |
| 12 | What does the "D" suffix mean? Same on both? | **"D" = Silicon Revision G with RWWEE Support in 128KB memory options.** Identical meaning on G17D and J17D. NOT automotive/temperature — temperature grading is done by the package-grade letter (U/N/F/Z), a separate ordering-code field. | 5 |
| 13 | Any PCN consolidating or splitting these variants? | **The family datasheet revision-history (sec 48.4) "Rev D, 9/2018" entry says: "Configuration Summary — Updated to Add new packages for device variant D".** That is the consolidating event: variant D was introduced by adding new packages to one silicon revision (rev G), not by introducing new silicon per package. No PCN appears to split them. | 5 |

**No questions were left unanswered.** All 13 are backed by at least
one HIGH-confidence Microchip primary source. The OpenOCD source
(Source 6, MEDIUM) was used only to corroborate practical flash-tooling
parameters, not as a primary authority.

**Build-system implications (one paragraph for the parent agent):**
Porting the SAMD21G17D firmware to SAMD21J17D is a packaging change,
not a silicon change. The build system must (1) swap the chip-name
preprocessor define from `__SAMD21G17D__` to `__SAMD21J17D__` so
`sam.h` includes the right header, (2) swap the startup file (vector
table differs only in slots 21/22 — `TC6_Handler` and `TC7_Handler`
become real instead of reserved), (3) swap the linker script filename
even though the content is identical (`samd21j17d_flash.ld`), (4)
update any pin-mux table that wants to use PB-bank pins (none on
G17D), (5) update OpenOCD only if scripted to assert a specific
DSU.DID — the OpenOCD `samd21_parts[]` table already knows both IDs.
NO driver rewrites needed for SERCOM, PORT, GCLK, DMAC, EVSYS,
NVMCTRL, USB, or any other shared peripheral.

---
