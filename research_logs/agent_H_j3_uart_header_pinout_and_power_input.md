# Research Log — Agent H: EPS PCU testing board V4.1 — J3 (UART header) pin-by-pin map and power-input safety

Purpose: Walid is about to power the EPS PCU testing board for the first
time and is asking whether he can apply 3.3 V to the 4-pin UART header
`J3` (the only 0.1″ header on the board, on the MCU sub-sheet) instead of
the screw-terminal `J5` (top-level PDU bench-supply input). The downstream
decision is binary: is it safe and electrically equivalent to power the
board through J3 instead of J5?

Specifically the agent must confirm:
- For each of the 4 pins of J3, which schematic net it connects to.
- Whether one of those nets is a 3.3 V power input AND another is GND.
- Whether the 3.3 V pin on J3 is the SAME net as J5 pin 1 (`PDU_3V3`
  feeding into the AUX_SUPPLY regulator), or a different net such as
  `AUX_3V3` (post-regulator) or `VDDIO` (only the MCU's I/O domain).
- The implication for "feed 3.3 V into J3 instead of J5": will the MCU
  be powered the same way? Will the AUX_SUPPLY regulator be bypassed?
  Will the rest of the board be powered or not?

Ground rules:
- Read the schematic files in the GitHub repo CHESS-mission/eps_pcu_eng
  branch `main` via `gh api repos/.../contents/<file> -H "Accept: application/vnd.github.raw"`.
- The relevant sub-sheets are MCU.kicad_sch (J3 lives there) and
  AUX_SUPPLY.kicad_sch (the rail-generator that consumes PDU_3V3 and
  produces AUX_3V3). Cross-check against testingPCU.kicad_sch (top level)
  if needed.
- Quote exact `(symbol …)`, `(label …)`, `(wire …)` S-expressions as proof.
- Every source gets its own dated entry below, logged before moving on.
- Today is 2026-04-26.

---

## Source 1: J3 symbol instance and library definition in MCU.kicad_sch

- **URL / path:** `gh api repos/CHESS-mission/eps_pcu_eng/contents/MCU.kicad_sch` — instance at lines 6462–6533; embedded library symbol `Connector:Conn_01x04_Pin` at lines 171–414 (pins defined at lines 341–412).
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  J3 is placed via `(symbol (lib_id "Connector:Conn_01x04_Pin") (at 69.85 40.64 0) ...)` (line 6463–6464), with `(property "Reference" "J3" ...)` at line 6471 and `(property "Footprint" "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical" ...)` at line 6487. The library symbol places its 4 pins at local coords `(at 5.08 2.54 180)`, `(at 5.08 0 180)`, `(at 5.08 -2.54 180)`, `(at 5.08 -5.08 180)` for pins 1, 2, 3, 4 respectively (lines 341–412). Anchor rotation is 0 and unmirrored.
- **Confidence: HIGH**
  Direct quote from the schematic source-of-truth. KiCad's S-expression format is well-defined; for an unrotated, unmirrored symbol the absolute pin position is `(anchor_x + pin_local_x, anchor_y − pin_local_y)`.
- **Implication for our build:**
  Computed absolute page coordinates of the four pin endpoints:
    - Pin 1 → (74.93, 38.10)
    - Pin 2 → (74.93, 40.64)
    - Pin 3 → (74.93, 43.18)
    - Pin 4 → (74.93, 45.72)
  These are the (x, y) values I will grep for in `(wire ...)` and `(label ...)` S-expressions to identify each pin's net.
- **Why I'm recording it:**
  Establishes the geometric anchor for tracing each J3 pin to its net.

## Source 2: Wires and labels at J3's four pin endpoints in MCU.kicad_sch

- **URL / path:** `gh api repos/CHESS-mission/eps_pcu_eng/contents/MCU.kicad_sch` — wires at lines 2982–2988, 3122–3128, 3282–3288, 3382–3388, 3422–3428, 3452–3458; labels at lines 3822–3831 ("AUX_3V3"), 3872–3880 ("UART_RX"), 3982–3990 ("UART_TX"); GND power symbol at lines 4550–4615.
- **Date accessed:** 2026-04-26
- **What this source gave me (plain English):**
  Tracing the wires from each computed J3 pin endpoint:
    - Pin 1 endpoint (74.93, 38.10): wire `(xy 78.74 38.1) (xy 74.93 38.1)` then `(xy 78.74 35.56) (xy 78.74 38.1)`, terminated by `(label "AUX_3V3" (at 78.74 35.56 0) ...)`.
    - Pin 2 endpoint (74.93, 40.64): wire `(xy 74.93 40.64) (xy 87.63 40.64)`, terminated by `(label "UART_TX" (at 87.63 40.64 180) ...)`.
    - Pin 3 endpoint (74.93, 43.18): wire `(xy 74.93 43.18) (xy 87.63 43.18)`, terminated by `(label "UART_RX" (at 87.63 43.18 180) ...)`.
    - Pin 4 endpoint (74.93, 45.72): wire `(xy 74.93 45.72) (xy 78.74 45.72)` then `(xy 78.74 46.99) (xy 78.74 45.72)`, terminated by `(symbol (lib_id "power:GND") (at 78.74 46.99 0) ...)` `(property "Value" "GND" ...)` (#PWR036).
- **Confidence: HIGH**
  Each pin endpoint is matched to a label or power symbol whose coordinates exactly coincide with the wire end. Direct quotes from the schematic.
- **Implication for our build:**
  J3 pin-to-net table is now established:
    - **Pin 1 → AUX_3V3** (3.3 V output rail, the post-regulator rail that powers the MCU)
    - **Pin 2 → UART_TX**
    - **Pin 3 → UART_RX**
    - **Pin 4 → GND**
  Critical: pin 1 is on the AUX_3V3 net, NOT PDU_3V3. Driving 3.3 V into J3 pin 1 BYPASSES the AUX_SUPPLY regulator and directly energises every node tied to AUX_3V3 (which includes the SAMD21 supply pins — see Source 3 below).
- **Why I'm recording it:**
  Direct, primary answer to question A and most of question B/C.
