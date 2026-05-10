# Research Log — Agent F: TCC0 complementary PWM with dead-time on PA12 (WO[6]) + PA13 (WO[7])

Purpose: Verify against the SAMD21 datasheet (and any reference
implementations) that the proposed scheme — drive TCC0 with CC[2] = CC[3]
identical, set DTIEN2 = DTIEN3 = 1, INVEN[7] = 1, mux PA12/PA13 to function
F — produces a clean complementary pair with hardware dead-time on PA12
and PA13 at 300 kHz, suitable for driving an EPC2152 GaN half-bridge.
This is a non-trivial scheme because the natural DTI pair on TCC0 is
WO[X] + WO[X+4] for the same channel; here we are using WO[6] from one
channel and WO[7] from another. Walid (the user) has explicitly asked
that this NOT be assumed without verification.

Ground rules:
- Primary source is the SAMD21 family datasheet, Chapter 31 (TCC), and
  the SAMD21J17D pinmux table (Chapter 7).
- Reference implementations: Microchip Harmony / ASF / Atmel Studio
  examples, plus any GitHub project that drives EPC2152 from a SAMD21.
- Every source gets its own dated entry below, logged before moving on.
- Today is 2026-04-26.

---

## Source 1: SAMD21 datasheet DS40001882H, Chapter 31.1 - 31.6.3.4 (pages 616-635)

- **URL / path:** c:\Users\iceoc\Documents\EPS-second-try\datasheets\samd21_datasheet.pdf, pages 616-635
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Confirmed structural facts about the TCC peripheral.
  - Section 31.1 Overview: "Waveform extensions are featured for motor control, ballast, LED, H-bridge, power converters, and other power control applications. They allow for low-side and high-side output with optional dead-time insertion."
  - Section 31.6.1 (page 620): "The Dead-Time Insertion (DTI) unit splits the four lower OTMX outputs into two non-overlapping signals: the non-inverted low side (LS) and inverted high side (HS) of the waveform output with optional dead-time insertion between LS and HS switching. The SWAP unit can swap the LS and HS HS pin outputs, and can be used for fast decay motor control."
    KEY FINDING: DTI "splits the four lower OTMX outputs". So the four DTI channels are channels 0..3. Each channel produces two outputs: a non-inverted LS on WO[N] and an inverted HS on WO[N+4].
  - Section 31.6.2.1 Initialization (p.620): "The following registers are enable-protected, meaning that they can only be written when the TCC is disabled (CTRLA.ENABLE=0): CTRLA (except RUNSTDBY/ENABLE/SWRST), FCTRLA, FCTRLB, **WEXCTRL, DRVCTRL**, EVCTRL." Confirms the proposed scheme must write WEXCTRL and DRVCTRL before setting ENABLE.
  - Initialization step 6 (p.621): "The waveform output can be inverted for the individual channels using the Waveform Output Invert Enable bit group in the Driver register (DRVCTRL.INVEN)." Inversion is per WO[x] line, which can be set independently of DTI.
  - Section 31.6.2.5.5 Single-Slope PWM Operation (p.625): "For single-slope PWM generation, the period time (T) is controlled by Top value, and CCx controls the duty cycle of the generated waveform output. When up-counting, the WO[x] is set at start or compare match between the COUNT and TOP values, and cleared on compare match between COUNT and CCx register values."
  - Frequency formula (p.626): f_PWM_SS = f_GCLK_TCC / (N x (TOP+1)). Confirms PER=159 at 48 MHz, prescale 1 -> 300.000 kHz exactly.
  - Table 31-2 (p.624) lists NPWM with TOP=PER, Update at TOP/ZERO. Output Polarity table 31-3 (p.627) lists single-slope PWM with POL=0: SET on TOP match, CLEAR on CCx match.
- **Confidence: HIGH** (primary source — Microchip datasheet)
- **Implication for our build:**
  - Confirms DTI architecture: 4 channels (0..3), each producing a paired LS/HS output where LS = WO[N] and HS = WO[N+4].
  - Confirms 300 kHz target with PER=159 from 48 MHz GCLK is exactly correct.
  - Confirms WEXCTRL and DRVCTRL must be written before CTRLA.ENABLE.
  - Confirms INVEN is a per-channel post-DTI inversion bit.
- **Why I'm recording it:** This is the canonical source. Everything else cross-checks against this.

---

## Source 2: SAMD21 datasheet DS40001882H, Sections 31.6.3.5 - 31.8.3 (pages 638-657) — Waveform Extension and Register Summary

- **URL / path:** c:\Users\iceoc\Documents\EPS-second-try\datasheets\samd21_datasheet.pdf, pages 638-657
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  This is the most important section for verifying the proposed scheme.

  **Section 31.6.3.7 Waveform Extension (p.642):**
  "Figure 31-33 shows a schematic diagram of actions of the four optional units that follow the recoverable fault stage on a port pin pair: Output Matrix (OTMX), Dead-Time Insertion (DTI), SWAP and Pattern Generation. The DTI and SWAP units can be seen as a four port pair slices:
   - Slice 0 DTI0 / SWAP0 acting on port pins (WO[0], WO[WO_NUM/2 +0])
   - Slice 1 DTI1 / SWAP1 acting on port pins (WO[1], WO[WO_NUM/2 +1])
   And more generally:
   - Slice n DTIx / SWAPx acting on port pins (WO[x], WO[WO_NUM/2 +x])"

  TCC0 has WO_NUM=8, so the four DTI slices are:
   - DTI0 -> (WO[0], WO[4])
   - DTI1 -> (WO[1], WO[5])
   - **DTI2 -> (WO[2], WO[6])** -- our channel for the natural pair on dev board
   - **DTI3 -> (WO[3], WO[7])** -- our channel for proposed scheme on PA13

  **Page 643:**
  "The dead-time insertion (DTI) unit generates OFF time with the non-inverted low side (LS) and inverted high side (HS) of the wave generator output forced at low level. This OFF time is called dead time. Dead-time insertion ensures that the LS and HS will never switch simultaneously."

  KEY: when DTI is enabled, the LS output is non-inverted and the HS output is inverted, BOTH derived from the SAME single compare channel input. The HS does NOT come from a different CC channel.

  **Page 644:**
  "The DTI stage consists of four equal dead-time insertion generators; one for each of the first four compare channels. The four channels have a common register which controls the dead time, which is independent of high side and low side setting."

  KEY: There is ONE single DTLS and ONE single DTHS register shared across all four DTI generators. You cannot set different dead-times per slice.

  "As shown in Figure 31-35, the 8-bit dead-time counter is decremented by one for each peripheral clock cycle until it reaches zero. A non-zero counter value will force both the low side and high side outputs into their OFF state. When the output matrix (OTMX) output changes, the dead-time counter is reloaded according to the edge of the input. When the output changes from low to high (positive edge) it initiates a counter reload of the DTLS register. When the output changes from high to low (negative edge) it reloads the DTHS register."

  KEY: DTLS and DTHS are 8-bit each, in GCLK_TCC cycles. At 48 MHz, one count = 1/48e6 = 20.833 ns. Range 0..255 -> 0 ns to 5.31 us. Granularity is exactly 20.833 ns per count, confirmed.

  **Figure 31-33 schematic (p.643):**
  Block diagram from left to right: OTMX -> DTI (with DTIxEN gate) -> SWAP -> PATTERN -> INV[x] -> P[x] (port pin).
  CRITICAL: the INV[x] block (DRVCTRL.INVEN[x]) is the LAST stage before the port pin. It is AFTER OTMX, AFTER DTI, AFTER SWAP, AFTER PATTERN. So INVEN inverts the FINAL processed waveform — including the dead-time gaps.
  CRITICAL: the DTI block has two outputs (LS to lower path WO[x], HS to upper path WO[x+WO_NUM/2]). When DTIEN=1, both outputs are derived from the SAME OTMX[x] input (the lower one), with HS being the inverse-with-dead-time of LS.

  **Section 31.6.3.7 OTMX (Table 31-4, p.643):**
  Default OTMX=0x0: CC0->WO[0]/WO[4], CC1->WO[1]/WO[5], CC2->WO[2]/WO[6], CC3->WO[3]/WO[7]. So with OTMX=0 (default), CC[2] feeds DTI2 which feeds WO[2]+WO[6], and CC[3] feeds DTI3 which feeds WO[3]+WO[7]. Two independent slices, two independent compare channels.

  **Section 31.7 Register Summary (p.650):**
  - WEXCTRL (offset 0x14): bits[1:0]=OTMX, bits[11:8]=DTIEN3..DTIEN0 (one bit per slice), bits[23:16]=DTLS[7:0], bits[31:24]=DTHS[7:0]
  - DRVCTRL (offset 0x18): bits[7:0]=NRE7..NRE0, bits[15:8]=NRV7..NRV0, bits[23:16]=INVEN7..INVEN0, bits[31:24]=FILTERVAL1[3:0],FILTERVAL0[3:0]

- **Confidence: HIGH** (primary datasheet)
- **Implication for our build:**
  - The proposed scheme's claim that `DTIEN[N]` produces complementary outputs on `WO[N]` and `WO[N+4]` from a single channel `CC[N]` is EXACTLY what the datasheet says.
  - DTIEN2 and DTIEN3 are independent slices, so two channels (CC[2] and CC[3]) can drive two independent dead-time-protected pairs:
      - DTIEN2: CC[2] -> WO[2] (LS, non-inverted) + WO[6] (HS, inverted, with dead-time)
      - DTIEN3: CC[3] -> WO[3] (LS, non-inverted) + WO[7] (HS, inverted, with dead-time)
  - INVEN[x] is the LAST stage. Inverting WO[7] via INVEN7=1 takes the HS-inverted output (which is the inverse of the LS) and inverts it AGAIN, producing back the LS shape (non-inverted) at PA13.
  - Net result on the proposed scheme:
      - PA12 = WO[6] = inverse of CC[2]'s LS output, with dead-time on rising edges
      - PA13 = WO[7] (with INVEN7=1) = double-inverse of CC[3]'s LS output = positive copy of LS, with dead-time on falling edges
    Wait — this is subtle. Let me re-check: when DTIEN3=1, WO[7] is the HS output which is "inverted with dead-time" of CC[3]'s match. INVEN7 inverts again to give back the LS shape... BUT with the dead-time gaps still present. So PA13 ends up looking like the LS output of slice 3, but with the dead-time gap on FALLING edges (because the dead-time was inserted by DTI on the HS rising edge, which after inversion is the LS falling edge).
  - This is NOT the natural complementary pair. The two pins both get a "modified" version of the same waveform but the dead-time gaps land in different places.
  - DTLS and DTHS are SHARED across all four DTI slices, so both slice 2 and slice 3 get the same dead-time settings.
- **Why I'm recording it:** Direct evidence that the proposed scheme's logic is correct in concept BUT requires careful analysis of where the dead-time gaps land. Need to draw out the timing diagram explicitly to confirm.

---

## Source 3: SAMD21 datasheet DS40001882H, Section 31.8.6 - 31.8.7 (pages 664-665) — WEXCTRL and DRVCTRL register descriptions

- **URL / path:** c:\Users\iceoc\Documents\EPS-second-try\datasheets\samd21_datasheet.pdf, pages 664-665
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Definitive register-bit-level description.

  **Section 31.8.6 WEXCTRL (offset 0x14, Enable-Protected, p.664):**

  Bits 31:24 - DTHS[7:0] Dead-Time High Side Outputs Value
  "This register holds the number of GCLK_TCC clock cycles for the dead-time high side."

  Bits 23:16 - DTLS[7:0] Dead-time Low Side Outputs Value
  "This register holds the number of GCLK_TCC clock cycles for the dead-time low side."

  -> 8-bit each. Range 0..255 in GCLK_TCC cycles. At 48 MHz: 0 to ~5.31 us, granularity 20.833 ns. CONFIRMED.

  Bits 11..8 - DTIEN3..DTIEN0 Dead-time Insertion Generator x Enable
  "Setting any of these bits enables the dead-time insertion generator for the corresponding output matrix. This will override the output matrix [x] and [x+WO_NUM/2], with the low side and high side waveform respectively."
  Value 0: "No dead-time insertion override."
  Value 1: "Dead time insertion override on signal outputs[x] and [x+WO_NUM/2], **from matrix outputs[x] signal**."

  THIS IS THE GOLD-STANDARD QUOTE: when DTIEN[x]=1, the DTI generator takes ONLY OTMX[x] (the lower output, which is CC[x] under default OTMX=0) and produces both WO[x] (low side, non-inverted) and WO[x+WO_NUM/2] (high side, inverted, with dead-time). The OTMX[x+WO_NUM/2] input (which under default OTMX=0 would be CC[x+4] but TCC0 has only 4 CC, so for x in 0..3 OTMX[x+4] would be the second copy of CC[x] in default mapping anyway) is OVERRIDDEN.

  Bits 1..0 - OTMX[1:0] Output Matrix
  "These bits define the matrix routing of the TCC waveform generation outputs to the port pins, according to 31.6.3.7 Waveform Extension."
  Default OTMX=0: CC0->WO[0]/WO[4], CC1->WO[1]/WO[5], CC2->WO[2]/WO[6], CC3->WO[3]/WO[7] (per Table 31-4 source 2).

  **Section 31.8.7 DRVCTRL (offset 0x18, Enable-Protected, p.665):**

  Bits 23..16 - INVEN7..INVEN0 Waveform Output x Inversion
  "These bits are used to select inversion on the output of channel x."
  Value 1: "Writing a '1' to INVENx inverts output from WO[x]."
  Value 0: "Writing a '0' to INVENx disables inversion of output from WO[x]."

  Bits 15..8 - NRV7..NRV0 Non-Recoverable State x Output Value
  Bits 7..0 - NRE7..NRE0 Non-Recoverable State x Output Enable

  CRITICAL: INVENx is a separate per-WO bit. From Figure 31-33 (source 2), it sits AFTER OTMX, AFTER DTI, AFTER SWAP, AFTER PATTERN, just before the port pin. So INVEN7=1 inverts the post-DTI HS output of slice 3 (which is the inverted version of CC[3]'s LS, with dead-time on what would be the HS rising edge).

  After applying INVEN7=1: the output at PA13 = NOT(HS_with_dead_time(CC[3])) = NOT(NOT(LS(CC[3])) with dead-time gaps). In effect, this gives the LS shape back, but the dead-time gaps that were inserted by the DTI on the HS path now appear as... let me draw it explicitly in the deliverable summary.

  **Section 31.8.4 SYNCBUSY (p.659-660):**
  WEXCTRL and DRVCTRL are NOT in the SYNCBUSY list. Per p.620, they are "enable-protected" but not "write-synchronized". So no need to wait on SYNCBUSY after writing them.

- **Confidence: HIGH** (primary datasheet, direct register description)
- **Implication for our build:**
  - Confirms DTIEN[x] takes only OTMX[x] as the source and forces both WO[x] and WO[x+WO_NUM/2] from it.
  - Confirms DTIEN2 and DTIEN3 produce two INDEPENDENT slices: slice 2 drives (WO[2], WO[6]) from CC[2]'s match, slice 3 drives (WO[3], WO[7]) from CC[3]'s match.
  - The proposed scheme writes:
      WEXCTRL = DTIEN3 | DTIEN2 | DTLS(N) | DTHS(N) | OTMX(0)
      DRVCTRL = INVEN7 | NRE7 | NRE6 | NRV6=NRV7=0
    Then sets CC[2] = CC[3] = X (same compare value).
  - The dead-time-low-side and dead-time-high-side counts are SHARED across slices 2 and 3 — they cannot be set independently. That's fine since we want symmetric dead-time on both gates.
  - INVEN7=1 inverts the slice-3 HS output (which is CC[3]'s match inverted-with-dead-time) to give back CC[3]'s match shape with dead-time gaps.
- **Why I'm recording it:** This is the quote-level evidence that backs the YES verdict on the proposed scheme — modulo verifying where the dead-time gap lands at PA13 vs PA12.

---

## Source 4: SAMD21 datasheet DS40001882H, Chapter 7 I/O Multiplexing (pages 29-34)

- **URL / path:** c:\Users\iceoc\Documents\EPS-second-try\datasheets\samd21_datasheet.pdf, pages 29-34
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Definitive pinmux table for the SAMD21J variant.

  **Table 7-1 PORT Function Multiplexing (page 30) for SAMD21J:**
  - PA12 (SAMD21J pin 21): A=EXTINT[12], C=SERCOM2/PAD[0], D=SERCOM4/PAD[0], E=TCC2/WO[0], **F=TCC0/WO[6]**, H=AC/CMP[0]
  - PA13 (SAMD21J pin 22): A=EXTINT[13], C=SERCOM2/PAD[1], D=SERCOM4/PAD[1], E=TCC2/WO[1], **F=TCC0/WO[7]**, H=AC/CMP[1]

  CONFIRMS: PA12 mux F = TCC0/WO[6], PA13 mux F = TCC0/WO[7]. Mux value F corresponds to PMUX value 0x5 (per DFP header lib/samd21-dfp/pio/samd21g17d.h lines 1111-1129: MUX_PA12F_TCC0_WO6 = 5, MUX_PA13F_TCC0_WO7 = 5).

  **Table 7-7 TCC Configuration Summary (page 34):**
  | TCC# | Channels (CC_NUM) | Waveform Output (WO_NUM) | Counter Size | Fault | Dithering | Output Matrix | DTI | SWAP | Pattern Generation |
  | 0    | 4                 | 8                        | 24-bit       | Yes   | Yes       | Yes           | Yes | Yes  | Yes                |
  | 1    | 2                 | 4                        | 24-bit       | Yes   | -         | -             | -   | -    | Yes                |
  | 2    | 2                 | 2                        | 16-bit       | Yes   | -         | -             | -   | -    | -                  |
  | 3    | 4                 | 8                        | 24-bit       | Yes   | Yes       | Yes           | Yes | Yes  | Yes                |
  Note: TCC3 is only supported in SAMD21x17D devices (per footnote 8 on p.32). CHESS uses SAMD21G17D so TCC3 is available BUT TCC3's outputs do not include PA12/PA13 — TCC3 outputs are mapped to other pins per the table.

  **Implication for "no closer pin pairing" question:**
  - On TCC0, the natural-DTI pairs are (WO[0], WO[4]), (WO[1], WO[5]), (WO[2], WO[6]), (WO[3], WO[7]).
    - PA12 = WO[6], so its natural slice-2 partner is WO[2], which is on PA10 mux F or PA18 mux F (per Table 7-1).
    - PA13 = WO[7], so its natural slice-3 partner is WO[3], which is on PA11 mux F or PA19 mux F.
  - The mainboard schematic apparently wires PA12 and PA13 to the EPC2152 (per the user's preamble, which says "the mainboard wires PA12+PA13 to the EPC2152, full stop"). Those pins are NOT on a natural single-DTI pair.
  - On TCC2, PA12=WO[0] and PA13=WO[1]. But TCC2 has DTI = "-" (none). So TCC2 cannot insert hardware dead-time anywhere. Plus TCC2 has CC_NUM=2 and WO_NUM=2, so there is no WO[N+CC_NUM/2] = WO[N+1] pair structure for DTI even if the unit existed.
  - On TCC3 (also CC_NUM=4, WO_NUM=8, has DTI), but PA12/PA13 are not in TCC3's pinmux F or any TCC3 mux per Table 7-1.
  - So the proposed dual-channel-on-TCC0 scheme is the ONLY way to get hardware-dead-time complementary pair on PA12+PA13 on this chip.
- **Confidence: HIGH** (primary datasheet pinmux table, cross-checked with DFP header)
- **Implication for our build:**
  - Pinmux F (value 0x5) on PA12 and PA13 selects TCC0/WO[6] and TCC0/WO[7] respectively. Confirmed.
  - There is no simpler scheme for this pin pair. The dual-channel-with-INVEN approach is forced by the mainboard wiring.
- **Why I'm recording it:** Closes question A.4 and question B.8 (no closer pin pairing exists on TCC0 for PA12+PA13).

---

## Source 5: SAMD21 DFP header file lib/samd21-dfp/pio/samd21g17d.h (lines 1111-1129)

- **URL / path:** c:\Users\iceoc\Documents\EPS-second-try\lib\samd21-dfp\pio\samd21g17d.h
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  ```c
  #define PIN_PA12F_TCC0_WO6                         _UINT32_(12)
  #define MUX_PA12F_TCC0_WO6                         _UINT32_(5)
  #define PINMUX_PA12F_TCC0_WO6                      ((PIN_PA12F_TCC0_WO6 << 16) | MUX_PA12F_TCC0_WO6)
  #define PORT_PA12F_TCC0_WO6                        (_UINT32_(1) << 12)
  ...
  #define PIN_PA13F_TCC0_WO7                         _UINT32_(13)
  #define MUX_PA13F_TCC0_WO7                         _UINT32_(5)
  #define PINMUX_PA13F_TCC0_WO7                      ((PIN_PA13F_TCC0_WO7 << 16) | MUX_PA13F_TCC0_WO7)
  ```
- **Confidence: HIGH** (vendor-supplied silicon mapping)
- **Implication for our build:** Use mux value 5 (i.e. PORT_PMUX_PMUXE_F_Val for the lower nibble of PMUX[6], and PORT_PMUX_PMUXO_F_Val for the upper nibble of PMUX[6]). PA12 is even pin -> PMUX[6] PMUXE field, PA13 is odd pin -> PMUX[6] PMUXO field.
- **Why I'm recording it:** Cross-check that the DFP and the datasheet agree on the pinmux, per Principle 1.

---

## Source 6: Project's existing phase5-pwm driver code

- **URL / path:** c:\Users\iceoc\Documents\EPS-phase5-pwm\src\drivers\driver_For_Generating_PWM_for_Buck_Converter.c
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  A known-good register sequence for TCC0 single-channel DTI on the dev board (PA18=WO[2], PA20=WO[6]). Configuration order:
  1. PM_APBCMASK |= TCC0_Msk (enable bus clock)
  2. GCLK_CLKCTRL = CLKEN | GEN_GCLK0 | ID_TCC0_TCC1; wait SYNCBUSY
  3. PORT_PINCFG[18]=PMUXEN|INEN; PMUX[9]=F (lower nibble); same for pin 20
  4. CTRLA = SWRST; wait SYNCBUSY.SWRST
  5. WAVE = WAVEGEN_NPWM; wait SYNCBUSY.WAVE
  6. WEXCTRL = DTIEN2 | OTMX(0) | DTLS(2) | DTHS(2)   <- single slice DTI2
  7. DRVCTRL = NRE2 | NRE6   <- non-recoverable fault forces both LOW
  8. PER = 159; wait SYNCBUSY.PER
  9. CC[2] = 0; wait SYNCBUSY.CC2
  10. CTRLA |= ENABLE; wait SYNCBUSY.ENABLE

  CRITICAL: The dev-board code uses DTIEN2 only, with ONE compare channel CC[2]. WO[2] is the LS (high-side gate? actually the comments in the code say "WO[2] (PA18, high-side drive)" and "WO[6] (PA20, low-side drive)" — but the datasheet says LS is non-inverted (WO[N]) and HS is inverted (WO[N+4]). This is a labelling choice depending on which gate of the EPC2152 corresponds to "LS" vs "HS" in the datasheet's naming).

  Comment in the code (line 49): "DTLS = delay on low-side (WO[2]) rising edge."
  This matches the datasheet: when OTMX[2] goes LOW->HIGH (positive edge), DTLS counter is loaded and WO[2] (LS) goes HIGH only after DTLS expires. WO[6] (HS) is the inverse, so it goes LOW immediately on the rising edge, then dead-time, then both stay LOW for DTLS counts, then WO[2] rises.

  Comment in the code (line 50): "DTHS = delay on high-side (WO[6]) rising edge."
  When OTMX[2] goes HIGH->LOW, DTHS counter is loaded. WO[2] (LS) goes LOW immediately. WO[6] (HS = inverted of OTMX[2]) wants to go HIGH, but it waits DTHS counts before doing so.

- **Confidence: HIGH** (working code, on the bench)
- **Implication for our build:**
  For the proposed scheme on the mainboard, we adapt this recipe. The differences are:
  1. Pinmux: PA12 mux F (TCC0/WO[6]) and PA13 mux F (TCC0/WO[7]).
     PA12 is PMUX[6] PMUXE (even pin), PA13 is PMUX[6] PMUXO (odd pin).
  2. WEXCTRL: enable DTIEN2 AND DTIEN3 (slice 2 drives WO[6]; slice 3 drives WO[7]).
  3. DRVCTRL: NRE6 | NRE7 to force PA12 and PA13 LOW on fault, plus INVEN7=1 to invert WO[7].
  4. CC[2] AND CC[3] both written with the same compare value.
  5. CCB[2] AND CCB[3] both updated when changing duty cycle.
- **Why I'm recording it:** This gives the exact register-write sequence to adapt for the mainboard. Reuses validated code structure.

---
