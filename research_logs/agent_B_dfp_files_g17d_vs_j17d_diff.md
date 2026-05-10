# Research Log — Agent B: DFP file-by-file diff, G17D vs J17D

Purpose: Answer concretely what changes between the G17D and J17D variants
inside the Microchip SAMD21 DFP v3.6.144 — specifically the four file pairs
(`samd21g17d.h` vs `samd21j17d.h`, `pio/samd21g17d.h` vs `pio/samd21j17d.h`,
`startup_samd21g17d.c` vs `startup_samd21j17d.c`, `samd21g17d_flash.ld` vs
`samd21j17d_flash.ld`). The downstream build decision is whether dropping in
the J17D files alongside the G17D files is honest (only chip-name and
pin-mux differences) or whether it hides real changes (register layouts,
interrupt vector ordering, memory map, NVM page size). Walid wants this
verified file-by-file.

Ground rules:
- Prefer official primary sources (Microchip's own DFP atpack contents,
  Microchip Developer Help) over third-party writeups.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-04-26.

---

## Source 1: Atpack inventory — fresh extraction of Microchip.SAMD21_DFP.3.6.144.atpack

- **URL / path:** `https://packs.download.microchip.com/Microchip.SAMD21_DFP.3.6.144.atpack` downloaded to `/tmp/samd21_dfp.atpack` (8,633,646 bytes), extracted to `/tmp/dfp_extracted/`.
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Confirmed canonical layout. Top-level dirs: `samd21a/`, `samd21b/`, `samd21c/`, `samd21d/`, `samd21l/`, plus `scripts/`, `Microchip.SAMD21_DFP.pdsc`, `package.content`. The "D"-suffix variants (e.g. samd21g17d) live in `/tmp/dfp_extracted/samd21d/`.
  All 8 target files exist:
  - `samd21d/include/samd21g17d.h` — 616 lines
  - `samd21d/include/samd21j17d.h` — 636 lines
  - `samd21d/include/pio/samd21g17d.h` — 1323 lines
  - `samd21d/include/pio/samd21j17d.h` — 1722 lines
  - `samd21d/gcc/gcc/startup_samd21g17d.c` — 205 lines
  - `samd21d/gcc/gcc/startup_samd21j17d.c` — 207 lines
  - `samd21d/gcc/gcc/samd21g17d_flash.ld` — 143 lines
  - `samd21d/gcc/gcc/samd21j17d_flash.ld` — 143 lines
  Note also that `samd21d/include/` includes a `samd21e17du.h` (an extra E-variant) — not relevant to our pair.
  First-pass observations from line counts alone: pio/J header is 399 lines longer than pio/G (consistent with J17D having more GPIO pins bonded out — TQFP64 vs TQFP48). The top-level chip header J is 20 lines longer than G. Linker scripts identical length (143 vs 143) — strong signal MEMORY map is the same. Startup file J is 2 lines longer than G.
- **Confidence: HIGH**
  Files came directly from Microchip's official packs server; SHA file present in atpack; uncorrupted unzip.
- **Implication for our build:**
  We have the canonical reference for diffing. Project copies in `lib/samd21-dfp/` and `startup/` may have been edited; we will diff project copies against fresh extraction separately, but the J17D answer must come from this fresh extraction.
- **Why I'm recording it:**
  Establishes provenance for the 8 files we will now diff in the following entries.

---

## Source 2: Linker script diff — `samd21g17d_flash.ld` vs `samd21j17d_flash.ld`

- **URL / path:** `/tmp/dfp_extracted/samd21d/gcc/gcc/samd21g17d_flash.ld` vs `/tmp/dfp_extracted/samd21d/gcc/gcc/samd21j17d_flash.ld`
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  `diff -u` output shows exactly ONE substantive difference: the brief comment in the file header.
  ```
  - * \brief Linker script for running in internal FLASH on the SAMD21G17D
  + * \brief Linker script for running in internal FLASH on the SAMD21J17D
  ```
  No other lines differ. That means:
  - **MEMORY regions identical:** both files declare `rom (rx) : ORIGIN = 0x00000000, LENGTH = 0x00020000` (128 KiB flash) and `ram (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00004000` (16 KiB SRAM). Verified by running `diff` over the entire file — no differences outside the comment.
  - **Section placements identical:** `.text`, `.relocate`, `.bss`, `.stack`, `.ARM.exidx`, all placed identically.
  - **ENTRY symbol identical:** `ENTRY(Reset_Handler)` in both.
  - **Stack-size define identical:** `STACK_SIZE = DEFINED(STACK_SIZE) ? STACK_SIZE : DEFAULT(0x2000)` in both (same expression).
  - File timestamps in atpack: G is `2022-03-17 10:52:42`, J is `2022-03-17 10:53:04` — generated 22 seconds apart from same template.
  This directly answers questions 4.1, 4.2, and 4.3: all identical except for one cosmetic comment line.
- **Confidence: HIGH**
  Direct byte-level diff of canonical files. Nothing inferred.
- **Implication for our build:**
  Walid can use **the same linker script** for both chips — the J17D ld file is literally a comment-relabeled copy. Memory map is the same because both chips share the SAMD21x17 die: 128 KiB flash @ 0x00000000, 16 KiB SRAM @ 0x20000000. The G/J difference is package pin count, not silicon memory.
- **Why I'm recording it:**
  This is the highest-stakes file (wrong memory map = silently broken firmware). It is now confirmed safe.

---

## Source 3: Startup file diff — `startup_samd21g17d.c` vs `startup_samd21j17d.c`

- **URL / path:** `/tmp/dfp_extracted/samd21d/gcc/gcc/startup_samd21g17d.c` vs `/tmp/dfp_extracted/samd21d/gcc/gcc/startup_samd21j17d.c`
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Files are NOT byte-identical. There are exactly 4 hunks of substantive difference (17 diff-output lines total), and they are all related to TC6/TC7 being present on the J variant:

  Hunk 1 (cosmetic): file-header brief comment `ATSAMD21G17D` -> `ATSAMD21J17D`.

  Hunk 2 (cosmetic but functional): the include directive
  ```
  -#include "samd21g17d.h"
  +#include "samd21j17d.h"
  ```
  This means the startup file pulls in chip-specific definitions from its sibling chip header.

  Hunk 3 (real, additive): two extra weak handler symbol declarations
  ```
  +void TC6_Handler          ( void ) __attribute__ ((weak, alias("Dummy_Handler")));
  +void TC7_Handler          ( void ) __attribute__ ((weak, alias("Dummy_Handler")));
  ```

  Hunk 4 (real, vector-table layout): IRQ slots 21 and 22 in the vector table are populated:
  ```
  -        .pvReserved21                  = (void*) (0UL),          /* 21 Reserved */
  -        .pvReserved22                  = (void*) (0UL),          /* 22 Reserved */
  +        .pfnTC6_Handler                = (void*) TC6_Handler,    /* 21 Basic Timer Counter */
  +        .pfnTC7_Handler                = (void*) TC7_Handler,    /* 22 Basic Timer Counter */
  ```
  The vector slots 21 and 22 are defined in BOTH chips — they just hold a `0UL` placeholder on G17D and a real handler pointer on J17D. Same vector table size, same numeric ordering for every other IRQ; only slots 21 and 22 have a different content.

  **Question 3.1 (Reset_Handler byte-identical?):** YES. Reset_Handler is not in any diff hunk; the bodies of `Reset_Handler`, `Default_Handler`, `Dummy_Handler`, and the C startup memcpy/memset code are byte-identical between the two files.

  **Question 3.2 (Vector table byte-identical?):** NO — but the *layout* is identical (same number of entries, same ordering, same names except slots 21/22). G17D fills slots 21+22 with `0UL`; J17D fills them with TC6/TC7 handlers. Total vector count and addresses of all other IRQs unchanged.

  **Question 3.3 (chip-specific weak handlers?):** YES — `TC6_Handler` and `TC7_Handler` exist as weak symbols only on J17D. Everything else (PM, SYSCTRL, WDT, RTC, EIC, NVMCTRL, DMAC, USB, EVSYS, SERCOM0..5, TCC0..2, TC3..5, ADC, AC, DAC, PTC, I2S) is present in both.
- **Confidence: HIGH**
  Direct `diff` of the canonical files; verified the diff has only 4 hunks and no other lines differ.
- **Implication for our build:**
  If Walid drops the J17D startup file alongside the G17D startup file, his C compiler will see two definitions of `Reset_Handler`, `Default_Handler`, `exception_table`, `TC0_Handler`, etc. — link will fail with multiple-definition errors. He must compile/link **only one** of the two startup files per build target. The G17D vector layout is a strict subset of J17D (slots 21/22 zeroed vs populated), so a J17D-built firmware run on a G17D part would still boot — but enabling TC6/TC7 in the NVIC on a G17D would obviously do nothing because the silicon doesn't have those peripherals.
- **Why I'm recording it:**
  This is a real, non-cosmetic difference. Refutes the "only chip-name macro changes" claim for the startup file pair.

---

## Source 4: Top-level chip header diff — `samd21g17d.h` vs `samd21j17d.h`

- **URL / path:** `/c/Users/iceoc/AppData/Local/Temp/dfp_research/extracted/samd21d/include/samd21g17d.h` vs `.../samd21j17d.h`
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**

  Full `diff -u` produced 17 hunks. Categorising:

  **(a) Cosmetic / chip-name relabel (no semantic effect):** every comment header that says `SAMD21G17D` is changed to `SAMD21J17D`. The two files were generated from the same template at timestamps 22 seconds apart. The device-description timestamp inside the header advances by 1 second (`2020-11-19T13:00:04Z` -> `2020-11-19T13:00:05Z`).

  **(b) Include-guard macro:** `_SAMD21G17D_H_` -> `_SAMD21J17D_H_`. Note these are filename-derived guards, not the compiler-define `__SAMD21G17D__` (which the project sets via `-D` in the Makefile). Question 1.5 answered: yes, the same pattern.

  **(c) Real semantic adds for J17D — TC6 and TC7 only:**

  IRQ enum:
  ```
  +  TC6_IRQn                  =  21, /* 21  Basic Timer Counter (TC6)           */
  +  TC7_IRQn                  =  22, /* 22  Basic Timer Counter (TC7)           */
  ```
  IRQ slots 0-20 and 23-28 are byte-identical (verified via `grep _IRQn= | sort | diff` -> only 2 added lines). `PERIPH_MAX_IRQn = 29` in BOTH files (the silicon vector table size never changed; G just leaves slots 21/22 unpopulated).

  Vector-table struct (`DeviceVectors`):
  ```
  -  void* pvReserved21;
  -  void* pvReserved22;
  +  void* pfnTC6_Handler;                          /*  21 Basic Timer Counter (TC6) */
  +  void* pfnTC7_Handler;                          /*  22 Basic Timer Counter (TC7) */
  ```
  Same struct size, same offsets — the two slots are just renamed from reserved placeholders to real handler pointers.

  Forward declarations: `+void TC6_Handler ( void );` and `+void TC7_Handler ( void );` added.

  Instance includes: `+#include "instance/tc6.h"` and `+#include "instance/tc7.h"`.

  Peripheral IDs (PM clock IDs):
  ```
  +#define ID_TC6           ( 78) /* Basic Timer Counter (TC6) */
  +#define ID_TC7           ( 79) /* Basic Timer Counter (TC7) */
  ```
  `ID_PERIPH_MAX = 88` in BOTH (so the silicon ID space is the same; G just doesn't use 78/79).

  Register-pointer macros (`*_REGS`):
  ```
  +#define TC6_REGS                         ((tc_registers_t*)0x42003800)
  +#define TC7_REGS                         ((tc_registers_t*)0x42003c00)
  ```
  Plus matching `TC6_BASE_ADDRESS` / `TC7_BASE_ADDRESS` defines.

  Crucially: I diffed the sorted list of all `*_REGS` macros (`grep ... | sort | diff`) — every register pointer that exists in BOTH files has the IDENTICAL ADDRESS. Question 1.1 answered: yes, all overlapping peripheral pointers (SERCOM0_REGS, TCC0_REGS, PORT_REGS, NVMCTRL_REGS, etc.) are defined identically. Only the J header adds two new ones (TC6_REGS, TC7_REGS).

  Event-Generator IDs (EVENT_ID_GEN_*) and Event-User IDs (EVENT_ID_USER_*):
  ```
  +#define EVENT_ID_GEN_TC6_OVF                             60
  +#define EVENT_ID_GEN_TC6_MC_0                            61
  +#define EVENT_ID_GEN_TC6_MC_1                            62
  +#define EVENT_ID_GEN_TC7_OVF                             63
  +#define EVENT_ID_GEN_TC7_MC_0                            64
  +#define EVENT_ID_GEN_TC7_MC_1                            65
  +#define EVENT_ID_USER_TC6_EVU                            21
  +#define EVENT_ID_USER_TC7_EVU                            22
  ```
  Same numbering scheme, J fills slots G left as gaps.

  PIO include: `-#include "pio/samd21g17d.h"` -> `+#include "pio/samd21j17d.h"` — the chip header points to its sibling pio file.

  **(d) The one non-TC6/TC7 substantive difference — Device DSU DID:**
  ```
  -#define CHIP_DSU_DID                   _UINT32_(0X10012693)
  +#define CHIP_DSU_DID                   _UINT32_(0X10012692)
  ```
  This is the silicon's self-identification word readable from the DSU peripheral. G17D = `0x10012693`, J17D = `0x10012692`. This is how OpenOCD/runtime code can tell the two parts apart at flash-time; **it is the only register VALUE that differs** between the two headers (and it's a read-only ID, not a control register address).

  **(e) Memory map (FLASH_SIZE / FLASH_PAGE_SIZE / FLASH_NB_OF_PAGES / HMCRAMC0_SIZE):**
  Byte-identical between files (only line numbers shift due to TC6/TC7 inserts):
  ```
  FLASH_SIZE        = 0x00020000   /* 128 kB */
  FLASH_PAGE_SIZE   = 64
  FLASH_NB_OF_PAGES = 2048
  HMCRAMC0_SIZE     = 0x00004000   /* 16 kB */
  ```
  Confirms question implicit in 4.x: NVM page size unchanged, RAM/Flash sizes unchanged.

  **Question 1.1 answer:** All overlapping peripheral instance pointer macros (`SERCOM0_REGS`, `TCC0_REGS`, `PORT_REGS`, `NVMCTRL_REGS`, `DSU_REGS`, etc.) are defined IDENTICALLY at the SAME ADDRESS. J adds TC6_REGS and TC7_REGS only.
  **Question 1.2 answer:** IRQ enum is identical except J adds `TC6_IRQn=21` and `TC7_IRQn=22` in slots that G left as gaps in the numbering. Same `PERIPH_MAX_IRQn = 29`. Same numeric value for every named IRQ in both files.
  **Question 1.3 answer:** Macros only in J: `TC6_IRQn`, `TC7_IRQn`, `pfnTC6_Handler`/`pfnTC7_Handler` struct fields, `TC6_Handler`/`TC7_Handler` prototypes, `ID_TC6`/`ID_TC7`, `TC6_REGS`/`TC7_REGS`, `TC6_BASE_ADDRESS`/`TC7_BASE_ADDRESS`, `EVENT_ID_GEN_TC6_*`/`EVENT_ID_GEN_TC7_*` (6 macros), `EVENT_ID_USER_TC6_EVU`/`EVENT_ID_USER_TC7_EVU`, plus the `_SAMD21J17D_H_` guard.
  **Question 1.4 answer:** Macros only in G: `pvReserved21` and `pvReserved22` struct fields (those slots are real handlers in J). Plus the `_SAMD21G17D_H_` guard. That's it.
  **Question 1.5 answer:** Yes, same guard pattern: `_SAMD21G17D_H_` vs `_SAMD21J17D_H_` (filename-style guard). The compiler-define `__SAMD21G17D__` / `__SAMD21J17D__` (double-underscore) is set by the build system's `-D` flag, not the header itself. The project's Makefile currently passes `-D__SAMD21G17D__`.

- **Confidence: HIGH**
  Direct diff of canonical files plus targeted `grep | sort | diff` to confirm no register address changes hide elsewhere.
- **Implication for our build:**
  Walid can safely include `samd21j17d.h` in a build that targets the J17D variant: every register address he is currently using is identical, every IRQ number for shared peripherals is identical, NVM page size is identical, RAM/Flash sizes are identical. The ONE runtime-visible difference is the DSU DID word (used only for chip ID readback, never for normal firmware logic). To target J17D, change `-D__SAMD21G17D__` to `-D__SAMD21J17D__` in the Makefile and switch the `#include "samd21g17d.h"` lines (in the startup file and any user code) to `samd21j17d.h`.
- **Why I'm recording it:**
  Strongest single answer to "is this an honest additive change?" — yes, modulo TC6/TC7 vector-table population and the DSU DID.

---

## Source 5: PIO header diff — `pio/samd21g17d.h` vs `pio/samd21j17d.h`

- **URL / path:** `/c/Users/iceoc/AppData/Local/Temp/dfp_research/extracted/samd21d/include/pio/samd21g17d.h` vs `.../pio/samd21j17d.h`
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**

  Total diff size: 465 lines, 52 hunks. **G-only (deleted) lines:** 5. **J-only (added) lines:** 404.

  Every one of the 5 G-only lines is just header-comment / include-guard text:
  ```
  <  * Peripheral I/O description for SAMD21G17D
  < /* file generated from device description version 2020-11-19T13:00:04Z */
  < #ifndef _SAMD21G17D_GPIO_H_
  < #define _SAMD21G17D_GPIO_H_
  < #endif /* _SAMD21G17D_GPIO_H_ */
  ```
  Filtering G-only lines for anything that is NOT a comment-or-guard line returns ZERO matches. **Question 2.2 answered:** there are no pin macros in G that are missing from J.

  **Question 2.3 answered:** Spot-checked overlapping alt-function macros (PA04D_SERCOM0_PAD0, PA05D_SERCOM0_PAD1, PA12F_TCC0_WO6) — every `PIN_`/`MUX_`/`PINMUX_`/`PORT_` quadruple has an identical RHS value in both files. Combined with the empty G-only list above, this proves all overlapping pin alternate-function macros are byte-identical.

  **Question 2.1 answered:** J adds 14 new GPIO pins that the 48-pin TQFP G package does not bond out: `PIN_PB00, PIN_PB01, PIN_PB04, PIN_PB05, PIN_PB06, PIN_PB07, PIN_PB12, PIN_PB13, PIN_PB14, PIN_PB15, PIN_PB16, PIN_PB17, PIN_PB30, PIN_PB31` (14 pins, the PB pins that the 64-pin TQFP J package brings out). For each, J adds the full set of associated alt-function macros (EIC EXTINT, ADC AIN, PTC, SERCOM PAD, TC, TCC, etc.).

  J also adds new alt-function macros for TC6/TC7 waveform-output pins on EXISTING (G-shared) pins:
  ```
  +#define PORT_PA20E_TC7_WO0
  +#define PORT_PA21E_TC7_WO1
  +#define PORT_PB02E_TC6_WO0
  +#define PORT_PB03E_TC6_WO1
  ```
  Plus PA22/PA23 TC6_WO bindings, etc. These are real-silicon mux options that the J17D enables because TC6/TC7 exist on it. They do not conflict with G's existing definitions for the same pins (PA20/PA21 still have their G-defined alt functions; the E mux just gains a new mapping).

  Of the 404 added lines: roughly 14 pins × ~16 lines each accounts for ~224 lines (PIN_PBxx + alt-function quartets). Remainder is TC6/TC7 alt-function macros on existing pins, plus a few alt-function additions on PA20-23 / PA12-13 / PB22-23 etc. for J-only mux options.

- **Confidence: HIGH**
  Confirmed by `diff | grep -c "^< "` and `diff | grep -c "^> "` plus a filtered grep that returned empty for non-cosmetic G-only lines.
- **Implication for our build:**
  Walid's existing G17D pin code remains valid byte-for-byte in a J17D build — every PA pin macro he uses today produces identical PIN/MUX/PINMUX/PORT values from the J pio header. He will simply gain access to 14 new PB pins and to TC6/TC7 mux options on already-bonded pins. The J pio header is a strict superset.
- **Why I'm recording it:**
  The pio header is where pin-out differences live, and this answer (purely additive, no value changes) is the foundation for the cross-cutting verdict.

---

## Source 6: Verification — project-shipped G17D copies vs freshly extracted atpack

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\lib\samd21-dfp\samd21g17d.h`, `.../pio/samd21g17d.h`, `C:\Users\iceoc\Documents\EPS-second-try\startup\startup_samd21g17d.c`, `C:\Users\iceoc\Documents\EPS-second-try\samd21g17d_flash.ld` — each compared with `diff -q` against the corresponding file in `/c/Users/iceoc/AppData/Local/Temp/dfp_research/extracted/samd21d/`.
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  All four `diff -q` invocations returned no output, meaning all four project-shipped G17D files are byte-identical to the canonical Microchip atpack v3.6.144 versions. The project has not been modified relative to the upstream DFP for these files.
- **Confidence: HIGH**
  `diff -q` is exact byte comparison.
- **Implication for our build:**
  All findings in Sources 2-5 (which compared atpack G vs atpack J) apply directly to the project's existing G files. We can safely conclude the diff between project-G and atpack-J equals the atpack-G vs atpack-J diff already characterised.
- **Why I'm recording it:**
  Anti-pattern guard: confirms the project copies are pristine, as the brief required us to verify rather than assume.

---

## Source 7: Final synthesis — answers to the 5 numbered questions and verdict

- **URL / path:** synthesis of Sources 1-6 above.
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**

  **Q1 (top-level chip header) — answered in Source 4:**
  - 1.1 Overlapping `*_REGS` instance pointers: IDENTICAL addresses for every shared peripheral. J adds only `TC6_REGS=0x42003800` and `TC7_REGS=0x42003c00`.
  - 1.2 IRQ enum: identical names and numeric values for IRQs 0-20 and 23-28. J adds `TC6_IRQn=21`, `TC7_IRQn=22` filling reserved slots. `PERIPH_MAX_IRQn=29` in both.
  - 1.3 J-only macros: `_SAMD21J17D_H_` guard, `TC6_IRQn`, `TC7_IRQn`, `pfnTC6_Handler`/`pfnTC7_Handler` struct fields, `TC6_Handler`/`TC7_Handler` prototypes, `ID_TC6=78`, `ID_TC7=79`, `TC6_REGS`, `TC7_REGS`, `TC6_BASE_ADDRESS`, `TC7_BASE_ADDRESS`, six `EVENT_ID_GEN_TC{6,7}_*`, two `EVENT_ID_USER_TC{6,7}_EVU`, plus include of `instance/tc6.h` and `instance/tc7.h`.
  - 1.4 G-only macros: `_SAMD21G17D_H_` guard, `pvReserved21`, `pvReserved22` (struct placeholder fields). Nothing else.
  - 1.5 Guard pattern: same — `_SAMD21G17D_H_` vs `_SAMD21J17D_H_` (filename-style include guard inside the headers). The `__SAMD21G17D__` chip-select is set by the build system's `-D` flag.
  - **One non-TC6/TC7 difference:** `CHIP_DSU_DID = 0x10012693` (G) vs `0x10012692` (J) — read-only silicon ID word, no register address change.
  - **Memory-map macros (FLASH_SIZE, FLASH_PAGE_SIZE, FLASH_NB_OF_PAGES, HMCRAMC0_SIZE):** byte-identical.

  **Q2 (pio header) — answered in Source 5:**
  - 2.1 J-only pin macros: 14 new GPIO pins (PIN_PB00, PB01, PB04-07, PB12-17, PB30, PB31) plus their alt-function quartets and TC6/TC7 alt-function macros on existing pins.
  - 2.2 G-only macros: NONE (only 5 deleted lines, all comment / include-guard cosmetics).
  - 2.3 Overlapping alt-function macros (PIN_/MUX_/PINMUX_/PORT_) are byte-identical in value.

  **Q3 (startup) — answered in Source 3:**
  - 3.1 Reset_Handler implementation: byte-identical.
  - 3.2 Vector table: same length, same ordering. Slots 21/22 hold `0UL` placeholders in G and `TC6_Handler`/`TC7_Handler` in J — the only structural difference.
  - 3.3 Chip-specific weak handlers: only `TC6_Handler` and `TC7_Handler` exist additionally in J.

  **Q4 (linker script) — answered in Source 2:**
  - 4.1 MEMORY regions identical: `rom 0x00000000 / 128 KiB`, `ram 0x20000000 / 16 KiB`.
  - 4.2 Section placements identical.
  - 4.3 ENTRY symbol and STACK_SIZE: identical. The only diff is one comment line.

  **Q5 (cross-cutting verdict):**
  Yes, the four-file delta consists ENTIRELY of:
    (a) chip-name comment / guard relabel,
    (b) the TC6 / TC7 peripheral being made present (vector slots 21/22, IRQ enum entries, register pointers at `0x42003800`/`0x42003c00`, peripheral IDs 78/79, event IDs, struct fields in DeviceVectors, weak handler symbols, instance includes),
    (c) the DSU device-ID word (`0x10012693` -> `0x10012692`) — a read-only silicon ID,
    (d) extra pin-mux macros for the 14 PB pins the J64-pin TQFP bonds out and the J-only TC6/TC7 alt-function macros on already-bonded pins.

  No other register addresses change. No memory map changes. No flash page size change. No interrupt re-numbering for any peripheral G shares with J. The G17D and J17D are truly the same SAMD21x17 die in different packages, with TC6/TC7 fused-out on the smaller one.

  **Final verdict: YES — dropping the J17D files alongside the G17D files is an honest, additive change.** The only behavioral risk is a build-system one: trying to compile and link BOTH startup files together would produce multiple-symbol-definition errors, so Walid must select exactly one chip target per build (via the existing Makefile `-D__SAMD21G17D__` flag, switched to `-D__SAMD21J17D__` for J builds, and a corresponding choice of which startup_*.c and which *_flash.ld to feed to the linker).

- **Confidence: HIGH**
  Synthesis of all prior sources, each of which used direct byte-level diff against canonical atpack contents.
- **Implication for our build:**
  The project can safely add J17D versions of all four files alongside the G17D versions, provided the Makefile selects exactly one target's startup file and linker script per build. The pio and chip headers can coexist (each is guarded by its own filename include-guard), because user code only `#include`s `sam.h` or one specific chip header at a time.
- **Why I'm recording it:**
  This is the deliverable verdict the user asked for.

---
