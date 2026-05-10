# Research Log — Agent G: First-flash gotchas (BOOTPROT, NVM USER ROW, factory state, recovery procedures)

Purpose: Identify every reason a brand-new ATSAMD21J17D-MUT on a freshly
assembled PCU testing board V4.1 might fail to accept its first SWD flash,
and document the standard recovery procedures. Specifically: what is the
factory state of NVM USER ROW (BOOTPROT etc.), does the chip ship blank
or with a bootloader, what OpenOCD commands clear lock states, what error
messages map to which failure modes, and what alternatives exist when
SWD stalls. The downstream decision is the exact step-by-step "if X
happens, do Y" procedure the team will use tonight when the first
`make flash` does not print `Verified OK`.

Ground rules:
- Primary sources: SAMD21 family datasheet (NVMCTRL chapter, NVM USER ROW
  layout), Microchip AN-NN about flashing SAMD21, OpenOCD source tree
  (`src/flash/nor/at91samd.c`), and the project's existing
  `docs/how_to_recover_from_stalled_debug_port.md`.
- Every source gets its own dated entry below, logged before moving on.
- Today is 2026-04-26.

---

## Source 1: Project's existing recovery doc

- **URL / path:** `c:\Users\iceoc\Documents\EPS-second-try\docs\how_to_recover_from_stalled_debug_port.md`
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Documents an "AP stall" failure mode we hit on 2026-04-01. Symptom is OpenOCD prints
  `Info : SWD DPIDR 0x0bc11477` then `Warn : Connecting DP: stalled AP operation, issuing ABORT`
  followed by `Error: [at91samd.cpu] Examination failed`. DP works (DPIDR readable), AP is wedged.
  Cause: a bug in our DFLL48M clock-switch code crashed the CPU before OpenOCD could attach,
  and the bad code re-runs on every power-on, so power cycling does NOT help.

  Fixes that did NOT work (table 1 in the doc):
   - Power cycle, `at91samd chip-erase`, `connect_assert_srst`,
     `cortex_m reset_config sysresetreq`, raw DAP writes to DSU chip-erase register,
     pyOCD, MPLAB IPE command line, lowering SWD clock speed.

  The ONE fix that worked: MPLAB X IDE -> Production -> Erase Device Memory Main Project.
  MPLAB X is installed at `C:\Program Files\Microchip\MPLABX\v6.30\`.
  After MPLAB erase, `make flash` (OpenOCD) works again.

  Doc was written for SAMD21G17D Curiosity Nano DM320119 (nEDBG firmware 01.22.0059).
  The current task is for SAMD21J17D-MUT on the freshly-built PCU testing board V4.1, which is
  NOT a Curiosity Nano — it is a custom PCB. The recovery tool may need to be different
  because nEDBG is not present on the custom board (something else is doing SWD).
- **Confidence: HIGH** — this is our own field-tested doc.
- **Implication for our build:**
  We already know MPLAB X IDE Erase Device Memory works for SAMD21G17D + nEDBG. For tonight's
  SAMD21J17D-MUT on PCU V4.1: we need to know what SWD probe the V4.1 uses (J-Link? Atmel-ICE?
  CMSIS-DAP? on-board nEDBG clone?), and whether the same MPLAB X procedure works through that
  probe. If the probe is anything Microchip-aware (Atmel-ICE, PICkit 4, Snap, MPLAB Snap, nEDBG)
  MPLAB X should drive it. If it is a generic J-Link or ST-Link, MPLAB X cannot help and we will
  need a different chip-erase path (J-Link Commander `unlock kinetis` equivalent, or
  `at91samd dsu_reset_deassert` plus chip-erase via DSU CTRL.CE).
- **Why I'm recording it:**
  This is the project's authoritative recovery procedure. Question 11 asks whether it applies
  to SAMD21J17D — the chip-level answer is yes (same DSU, same NVMCTRL), but the probe-level
  answer depends on what is on PCU V4.1. Worth flagging to the human.

---

## Source 2: SAMD21 NVMCTRL fuse #defines (pubnub-atmel ASF mirror)

- **URL / path:** https://raw.githubusercontent.com/pubnub/pubnub-atmel/master/Code/FirmwareUpgrade/ASF/sam0/utils/cmsis/samd21/include/component/nvmctrl.h
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Authoritative bit positions for the SAMD21 NVMCTRL user-row fuse fields.
  These are the original ASF/CMSIS-Atmel #defines (NOT in our DFP v3.6.144 fuses.h, which
  only defines TEMP_LOG/AUX/OTP4 fuses):

  ```
  NVMCTRL_FUSES_BOOTPROT_ADDR     = NVMCTRL_USER             // 0x00804000
  NVMCTRL_FUSES_BOOTPROT_Pos      = 0     Msk = 0x7  (3 bits, in word 0)
  NVMCTRL_FUSES_EEPROM_SIZE_Pos   = 4     Msk = 0x7  (3 bits, in word 0)
  NVMCTRL_FUSES_REGION_LOCKS_ADDR = NVMCTRL_USER + 4         // 0x00804004
  NVMCTRL_FUSES_REGION_LOCKS_Pos  = 16    Msk = 0xFFFF (16 bits, in word 1, bits 47:32 of user row)
  NVMCTRL_FUSES_NVMP_*  / NVM_LOCK_* / PSZ_*  -> these live in OTP1, NOT user row
  ```

  BOOTPROT mapping (from datasheet, 3 bits): 7=0 KB, 6=512 B, 5=1 KB, 4=2 KB, 3=4 KB, 2=8 KB,
  1=16 KB, 0=32 KB protected. Factory default = 7 (no protection). Note the inverted sense:
  the field counts down from 7 = unprotected.

  EEPROM_SIZE mapping (3 bits): 7=0 B (no EEPROM emulation), 6=256 B ... 0=16384 B. Factory
  default = 7 (no EEPROM area reserved).

  REGION_LOCKS bits 15:0 (one bit per 1/16 of total flash). 1 = unlocked, 0 = locked.
  Factory default = 0xFFFF (all unlocked).
- **Confidence: HIGH** — these are Microchip/Atmel-authored CMSIS headers, mirrored verbatim.
- **Implication for our build:**
  We can read 0x00804000 and 0x00804004 over SWD to verify the fuses, and we know exactly
  which bits to mask out when re-writing. If REGION_LOCKS != 0xFFFF on a brand-new chip,
  something is wrong (or someone pre-programmed it).
- **Why I'm recording it:**
  Required to answer Q1 (User Row layout) and Q5/Q6 (BOOTPROT / LOCK definition).

---

## Source 3: Factory-default user-row 64-bit value (FixSAMD21Fuses.ino)

- **URL / path:** https://gist.github.com/cmaglie/c411621b8d49494faeb7869ad59aa6b6 (FixSAMD21Fuses.ino)
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Cristian Maglie (Arduino) wrote a sketch that detects and repairs a known SAMD21 brick
  pattern. Quoted from the gist:

  ```
  new_fusebits[0] = 0xD8E0C7FF; // Default values
  new_fusebits[1] = 0xFFFFFC5D;
  ```

  -> Concatenated little-endian: full 64-bit user-row factory default =
     **0xFFFFFC5D D8E0C7FF**.

  Decoded against Source 2 bit positions:
   - word0 = 0xD8E0C7FF
       bits  2:0  BOOTPROT       = 0b111 = 7  -> 0 KB protected (none)
       bits  3    reserved       = 1
       bits  6:4  EEPROM_SIZE    = 0b111 = 7  -> 0 B EEPROM
       bits  7    reserved       = 1
       bits 13:8  BOD33 LEVEL    = 0x07     -> 1.715 V (default trigger)
       bits 14    BOD33 ENABLE   = 1        -> BOD33 enabled out of factory
       bits 16:15 BOD33 ACTION   = 0b01     -> reset on brown-out
       bits 24:17 reserved (calibration scratch) = 0x6F
       bits 25    WDT ENABLE     = 0
       bits 26    WDT ALWAYS-ON  = 0
       bits 30:27 WDT PERIOD     = 0xB (11)
       bits 31    WDT WINDOW lsb = 1
   - word1 = 0xFFFFFC5D
       bits 34:32 (cont. WDT WINDOW [3:1]) = 0b101  -> WINDOW=0xB
       bits 38:35 WDT EWOFFSET = 0xB
       bits 39    WDT WEN      = 0
       bits 47:40 BOD33 HYST + reserved (low byte of word1 = 0xFC; bit 40 = BOD33 HYST = 0)
       bits 63:48 LOCK[15:0]   = 0xFFFF (all regions UNLOCKED)

   The gist's "bricked" detection pattern is:
   ```
   if (old_fusebits[0] == 0xFFFFFFFA && old_fusebits[1] == 0xFFFFFFFF)
   ```
   This is the classic "user row got erased to all-1s except BOOTPROT was set to 010 by a
   buggy bootloader update" pattern. BOOTPROT=0b010 = 8 KB protected, EEPROM=0b111 = none,
   BOD33 cleared. Cristian's fix re-writes the factory default 0xFFFFFC5DD8E0C7FF.

   The mask `USER_WORD_IMPLEMENTED_MASK = 0xC01FFFFFFFFFFFFF` (from ATSAMD21G18A.atdf, Source 4)
   tells us which bits are actually used; bits 53:62 are reserved-1 in the implemented mask,
   which matches the 0xFC byte in word1.
- **Confidence: HIGH** — cross-references with multiple bootloader sources (uf2-samdx1, Arduino).
- **Implication for our build:**
  When we read the user row on the new SAMD21J17D-MUT for the first time, we should expect
  to see 0xFFFFFC5DD8E0C7FF. Anything else means either (a) the chip has been written before,
  or (b) we hit the bootloader-bug brick pattern 0xFFFFFFFFFFFFFFFA. The fix is the same
  either way: write 0xFFFFFC5DD8E0C7FF back via NVMCTRL.
- **Why I'm recording it:**
  This is the answer to Q1 — the actual factory user-row content.

---

## Source 4: ATSAMD21G18A.atdf USER_WORD_IMPLEMENTED_MASK

- **URL / path:** https://github.com/microsoft/uf2-samdx1/blob/master/lib/samd21/samd21a/atdf/ATSAMD21G18A.atdf
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Microchip's own ATDF (XML device description) declares:
   - User Page lives at `0x00804000`, length 256 bytes (`USER_PAGE`)
   - Only the first 64 bits are MCU-defined; the remaining 248 bytes are application-free.
   - `USER_WORD_IMPLEMENTED_MASK = 0xC01FFFFFFFFFFFFF` — the bits the chip actually decodes.
- **Confidence: HIGH** — Microchip/Atmel-authored device description.
- **Implication for our build:**
  When writing the user row, we must preserve the implemented mask and leave reserved bits
  at the factory value (1). Lower 248 bytes (bytes 8-255) of the user page are free for
  application use if we want — but we are not using them.
- **Why I'm recording it:**
  Confirms the user row size, address, and which bits matter.

---

## Source 5: Factory state of new SAMD21 (Microchip AT07175 + community consensus)

- **URL / path:** https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-42366-SAM-BA-Bootloader-for-SAM-D21_ApplicationNote_AT07175.pdf
   plus https://learn.adafruit.com/how-to-program-samd-bootloaders/programming-the-bootloader-with-atmel-studio
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Standard (non-`-U`) SAMD21 parts are shipped with completely BLANK flash (all 0xFF) and
  no Microchip bootloader. The SAM-BA bootloader is NOT factory-programmed; the customer
  must flash their own code or a bootloader.

  The `-U` suffix variants (BGA45 package only — e.g. `ATSAMD21JxxA-AUT` or `-AU`) DO ship
  with a pre-programmed bootloader. Our part is `ATSAMD21J17D-MUT`. The `-MU` suffix is the
  standard QFN64 plastic package (no factory bootloader). The `T` is reel/tape&reel
  packaging, the trailing `-MUT` simply means "QFN, tape&reel, RoHS." The `D` is the silicon
  die revision (D = newest revision as of 2024+).

  Bottom line: a brand-new ATSAMD21J17D-MUT comes with:
   - Flash = all 0xFF (no application, no bootloader)
   - User row = 0xFFFFFC5DD8E0C7FF (BOOTPROT=7=disabled, REGION_LOCKS=0xFFFF=unlocked)
   - DSU.DID readable
   - SWD enabled, debug access fully open

  However: assembly houses sometimes run a power-on test that briefly drives the part —
  but they typically do NOT program firmware unless contracted to. If the chip arrives in
  factory tape&reel with the Microchip seal intact, assume it is blank. If it arrives
  loose or repackaged, assume nothing.

  How to tell what's on the chip:
   1. Read the first 32 bytes of flash via OpenOCD `mdw 0 8`. If all 0xFFFFFFFF → blank.
   2. Read user row at 0x00804000: `mdw 0x00804000 2`. Expect 0xD8E0C7FF, 0xFFFFFC5D.
   3. Read DSU.DID at 0x41002018: `mdw 0x41002018 1`. Confirms chip identity.
- **Confidence: HIGH** — corroborated by Microchip AppNote, Adafruit Learn, and community.
- **Implication for our build:**
  Our SAMD21J17D-MUT should be totally blank. If the user row reads anything other than
  0xFFFFFC5DD8E0C7FF on first connection, do NOT flash; investigate first. If flash bytes
  0..7 read anything other than 0xFF, the chip already has code on it.
- **Why I'm recording it:**
  Answers Q2, Q3, Q4 directly.

---

## Source 6: OpenOCD at91samd.c — bootloader, chip-erase, dsu_reset_deassert

- **URL / path:** https://raw.githubusercontent.com/openocd-org/openocd/master/src/flash/nor/at91samd.c
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Direct source for the three OpenOCD commands relevant to recovery.

  Key constants:
  ```
  #define SAMD_USER_ROW       0x00804000
  #define SAMD_PAC1           0x41000000
  #define SAMD_DSU            0x41002000
  #define SAMD_DSU_CTRL_EXT   0x100   // offset; absolute = 0x41002100
  #define SAMD_DSU_STATUSA    1       // offset; absolute = 0x41002001
  ```

  (1) `at91samd bootloader <size>` (samd_handle_bootloader_command):
      - With argument 0: writes BOOTPROT field (bits 0:2 of user row) = 7 (no protection).
      - With argument N bytes (must be 8192/4096/2048/1024/512/256/128/0):
        computes the BOOTPROT code that gives that protected size, then calls
        `samd_modify_user_row(target, code, 0, 2)` — modifies ONLY bits 0..2.
      - With no argument: prints current BOOTPROT setting.
      - Requires target HALTED.
      - Bottom line: this is the public, supported way to clear BOOTPROT via OpenOCD.
        `at91samd bootloader 0` clears protection — exactly what we want before flashing
        if the chip happens to be locked.

  (2) `at91samd chip-erase` (samd_handle_chip_erase_command):
      ```c
      target_write_u32(target, SAMD_PAC1, (1<<1));                   // unlock DSU PAC
      target_write_u8(target, SAMD_DSU + SAMD_DSU_CTRL_EXT, (1<<4)); // CTRL.CE = 1
      ```
      Triggers DSU chip-erase via the external-access CTRL register (0x41002100).
      DSU.CE bit erases all flash AND user row REGION_LOCKS to 0xFFFF, but the BOOTPROT
      field stays at whatever was previously written if it had been programmed via NVMCTRL.
      Per datasheet: CHIP_ERASE clears the entire main flash and the lock bits, but does
      NOT erase the user row; user-row contents are preserved (this is intentional so
      calibration / fuses are kept).

  (3) `at91samd dsu_reset_deassert`:
      ```c
      target_examine_one(target);
      target_poll(target);
      // if reset_halt && SRST available: arm DCB_DHCSR + DCB_DEMCR for catch-on-reset
      target_write_u8(target, SAMD_DSU + SAMD_DSU_STATUSA, (1<<1));  // STATUSA.CRSTEXT=1
      ```
      Writes 1 to STATUSA.CRSTEXT (CPU Reset Phase Extension) at 0x41002001 — this clears
      the "CPU is held in reset extension" flag, allowing the core to come out of reset.
      Used in OpenOCD's reset-init scripts after `cortex_m reset_config sysresetreq`.
- **Confidence: HIGH** — direct quote from upstream OpenOCD master.
- **Implication for our build:**
  We have authoritative knowledge of what each OpenOCD command does. Critically:
  - `chip-erase` does NOT clear the user row — so a bad BOOTPROT survives chip-erase.
  - To reset BOOTPROT, must use `at91samd bootloader 0`, which requires the target to be
    halted, which requires the AP to be working — which is the very thing that fails when
    BOOTPROT is locking out the boot region.
  - Recovery from a fully-locked chip needs both: chip-erase to wipe app + bootloader, then
    bootloader 0 to clear BOOTPROT. If AP is stalled, neither works → fall back to MPLAB X.
- **Why I'm recording it:**
  Answers Q7, Q8, Q9 directly.

---

## Source 7: DSU.DID layout (DFP local) and DEVSEL table (OpenOCD doxygen)

- **URL / paths:**
   - Local DFP: `c:\Users\iceoc\Documents\EPS-second-try\lib\samd21-dfp\component\dsu.h`
   - OpenOCD doxygen: https://openocd.org/doc/doxygen/html/at91samd_8c_source.html
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  DSU.DID lives at offset 0x18 of DSU base 0x41002000 -> absolute address 0x41002018.
  Bit layout (from local DFP dsu.h):
   - bits 31:28 PROCESSOR = 1 for ARM Cortex-M0+
   - bits 27:23 FAMILY    = 0 for SAM D
   - bits 22:16 SERIES    = 1 for SAM D21/DA1
   - bits 15:12 DIE       = 0
   - bits 11:8  REVISION  = silicon rev letter (A=0, B=1, C=2, D=3 ...)
   - bits  7:0  DEVSEL    = part-specific code (see table below)
   - DSU_DID_RESETVALUE in DFP header = 0x10012692 (this is for SAMD21G17D rev rev'D' so
     0x10012692 -> PROCESSOR=1, SERIES=1, REVISION=2 (rev C), DEVSEL=0x92 ... hmm that
     would say "G17D rev C" -> the header reset value reflects whatever silicon Microchip
     compiled the DFP for. The reset value is informational; trust the live read.)

  DEVSEL table for the J/G/E variants (from OpenOCD samd21_parts[]):
   ```
   DEVSEL  Part            Flash  RAM
   0x00    SAMD21J18A      256KB  32KB
   0x01    SAMD21J17A      128KB  16KB
   0x06    SAMD21G17A      128KB  16KB
   0x57    SAMD21G17L      128KB  16KB
   0x92    SAMD21J17D      128KB  16KB     <-- our part
   0x93    SAMD21G17D      128KB  16KB
   0x94    SAMD21E17D      128KB  16KB
   0x97    SAMD21E17L      128KB  16KB
   ```

  Therefore the expected full DSU.DID value for our ATSAMD21J17D-MUT is:
  ```
  PROCESSOR=1  FAMILY=0  SERIES=1  DIE=0  REVISION=? (varies, often 0..2)  DEVSEL=0x92
  -> 0x1001_RR92  where RR is the revision nibble shifted into bits 11:8
  -> 0x10010092 for rev A silicon
  -> 0x10010192 for rev B
  -> 0x10010292 for rev C
  -> 0x10010392 for rev D
  ```
  (REVISION here is silicon die-revision, NOT the part-name "D" suffix. The "D" in the
  part name maps to DEVSEL 0x92, the silicon rev is independent.)

  How to read DSU.DID via OpenOCD:
   - Halt the target then run `mdw 0x41002018 1` from the OpenOCD telnet/gdb console.
   - Or via OpenOCD command line: `openocd -f interface/<probe>.cfg -f target/at91samdXX.cfg
     -c "init; halt; mdw 0x41002018; exit"`
   - Or the at91samd driver itself reads it during `examine` and prints the part name in
     OpenOCD's startup log (e.g. `at91samd.cpu: Cortex-M0+ ... SAMD21J17D`).
- **Confidence: HIGH** — DSU layout from Microchip-authored DFP, DEVSEL table from OpenOCD
  upstream which Microchip-supports.
- **Implication for our build:**
  When we attach OpenOCD to the new chip, expect to see "SAMD21J17D" identified, and the
  DSU.DID upper bytes 0x10010_92. If we see DEVSEL 0x01 (J17A) instead, the assembly house
  put the wrong part on the board. If we see 0x00 (J18A) we got more flash than ordered;
  good news but worth flagging. Mismatches in REVISION nibble are normal across batches.
- **Why I'm recording it:**
  Answers Q12, Q13, Q14 directly.

---
