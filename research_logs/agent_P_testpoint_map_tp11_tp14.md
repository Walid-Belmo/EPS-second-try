# Research Log — Agent P: PCB test points TP11–TP14 → net map

Purpose: map physical test points TP11..TP14 on the CHESS EPS PCU testing
board V4.1 to their schematic nets, so a bench operator can probe them to
verify firmware-driven actuation (PWM, eFuse enables, rail voltages).

Ground rules:
- Prefer the actual PCB project files (.kicad_pcb, .kicad_sch, BOM .csv)
  over any prose doc or prior research log.
- Every source gets its own dated entry below, logged before moving on.
- If two sources disagree, record both and mark the current best guess.
- Today is 2026-05-16.

---

## Source 1: testingPCU.kicad_pcb — TP12 footprint + pad net

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\testingPCU.kicad_pcb` lines 14149–14251
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP12 is a `TestPoint:TestPoint_Pad_D1.5mm` SMD pad on layer `F.Cu` (top side),
  at board position `(at 81.25 124.5)`. Its single pad: `(net 16 "/BUCK/BUCK_IN")`.
  Footprint metadata: `(sheetname "/BUCK/")` `(sheetfile "BUCK.kicad_sch")`.
  Reference proof: `(property "Reference" "TP12"` at PCB line 14155.
  Net proof: `(net 16 "/BUCK/BUCK_IN")` at PCB line 14245.
- **Confidence: HIGH**
  Direct read of the authoritative board layout file; the pad net binding is
  the ground-truth copper connection, not silkscreen or prose.
- **Implication for our build / bench test:**
  TP12 = net `/BUCK/BUCK_IN` — the buck converter input rail (V_panel feeding
  the buck on the BUCK sheet). This CONFIRMS the prior claim "TP12 = BUCK_IN /
  V_panel". Probing TP12 measures the voltage presented to the buck input.
  Under current bench condition (only 3.3 V on J5, no PV/BAT power) this node
  is likely ~0 V unless fed from the PV/source path. Need BUCK.kicad_sch trace
  to confirm whether BUCK_IN is post-PV-eFuse or a separately injected source.
- **Why I'm recording it:**
  Establishes TP12 net identity; drives where operator probes to verify buck
  input rail before checking PWM-driven buck output.

## Source 2: testingPCU.kicad_pcb — TP13 footprint + pad net

- **URL / path:** `...\_pcb_files_agent_I\testingPCU.kicad_pcb` lines 18316–18412
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP13 = `TestPoint_Pad_D1.5mm`, layer `F.Cu` (top), position `(at 106.05 124.5)`.
  Footprint metadata `(sheetname "/BUCK/")` `(sheetfile "BUCK.kicad_sch")`.
  Reference proof: `(property "Reference" "TP13"` at PCB line 18322.
  Net proof: `(net 15 "/BUCK/BUCK_OUT")` at PCB line 18412.
- **Confidence: HIGH**
  Direct read of authoritative layout; pad-net binding is ground-truth copper.
- **Implication for our build / bench test:**
  TP13 = net `/BUCK/BUCK_OUT` — the buck converter OUTPUT rail. This refines
  the prior claim "TP13 = V_battery / buck output": it is specifically the buck
  *output* node on the BUCK sheet (not necessarily the raw battery terminal —
  battery is TP14 `/+BAT`, see Source 3). Probing TP13 with the buck input
  powered and firmware driving PA12/PA13 PWM lets the operator watch V_out
  track the commanded duty cycle. Under current bench condition (3.3 V only on
  J5, buck cold) expect ~0 V. NOTE: this is the regulated buck output node, not
  the inductor switching node, so it is a relatively safe DC probe point —
  but still verify against BUCK.kicad_sch where the switching node (SW) is.
- **Why I'm recording it:**
  Primary TP for verifying PWM→buck output actuation; defines the key
  firmware-driven-output probe point.

## Source 3: testingPCU.kicad_pcb — TP14 footprint + pad net

- **URL / path:** `...\_pcb_files_agent_I\testingPCU.kicad_pcb` lines 20977–21073
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP14 = `TestPoint_Pad_D1.5mm`, layer `F.Cu` (top), position `(at 118 98.5)`.
  Footprint metadata `(sheetname "/BAT/")` `(sheetfile "BAT.kicad_sch")`.
  Reference proof: `(property "Reference" "TP14"` at PCB line 20983.
  Net proof: `(net 17 "/+BAT")` at PCB line 21073.
- **Confidence: HIGH**
  Direct read of authoritative layout; pad-net binding is ground-truth copper.
- **Implication for our build / bench test:**
  TP14 = net `/+BAT` — the battery rail (top-level global net, used on the BAT
  sheet). This is the raw +BAT node, distinct from TP13's `/BUCK/BUCK_OUT`.
  Probing TP14 measures the battery-side rail. Relevant to BAT eFuse (IC7)
  enable via firmware PA17: if +BAT is the eFuse OUTPUT side, toggling the BAT
  eFuse enable should make TP14 go live/dead. Must trace BAT.kicad_sch to
  confirm whether /+BAT is the eFuse input (battery terminal) or eFuse output
  (post-eFuse distribution). Under current bench (no BAT power) expect ~0 V.
- **Why I'm recording it:**
  Defines battery-rail probe; needed to verify firmware BAT-eFuse enable
  (PA17) actuation depending on pre/post-eFuse position.

## Source 4: testingPCU.kicad_pcb — TP11 footprint + pad net

- **URL / path:** `...\_pcb_files_agent_I\testingPCU.kicad_pcb` lines 33578–33674
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  TP11 = `TestPoint_Pad_D1.5mm`, layer `F.Cu` (top), position `(at 64.25 129.25)`.
  Footprint metadata `(sheetname "/PV/")` `(sheetfile "PV.kicad_sch")`.
  Reference proof: `(property "Reference" "TP11"` at PCB line 33584.
  Net proof: `(net 60 "/-PV")` at PCB line 33674.
- **Confidence: HIGH**
  Direct read of authoritative layout; pad-net binding is ground-truth copper.
- **Implication for our build / bench test:**
  TP11 = net `/-PV` — the PV NEGATIVE / return node on the PV sheet. This
  CONTRADICTS the prior claim "TP11 = PV post-eFuse". `/-PV` is the negative
  side of the PV input, NOT the positive post-eFuse rail. The PV eFuse (IC4,
  PA16 enable) is in the positive PV path; `/-PV` is the low-side / return.
  Probing TP11 likely shows ~0 V (or PV return reference) and is generally
  NOT useful for verifying the PV eFuse enable (that needs the +PV post-eFuse
  net, on a different TP). MUST trace PV.kicad_sch to confirm whether `/-PV`
  is tied to GND or is an isolated/shunt-return node (could be the low side of
  a current-sense shunt — important for safety and for what it actually reads).
- **Why I'm recording it:**
  Corrects an inherited assumption; flags TP11 as PV-return, not PV-post-eFuse —
  changes which TP the operator must use to verify PA16 PV-eFuse enable.

## Source 5: BUCK.kicad_sch — BUCK_IN / BUCK_OUT / SW topology + inductor L2

- **URL / path:** `C:\Users\iceoc\Documents\EPS-second-try\research_logs\_pcb_files_agent_I\BUCK.kicad_sch` lines 1377–1431, 2209–2315
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  BUCK sheet has hierarchical labels: `BUCK_IN` (shape input, @78.74,109.22),
  `BUCK_HS_IN`, `BUCK_LS_IN` (separate input feeds), and `BUCK_OUT`
  (shape output, @233.68,99.06). There is a SEPARATE local label `SW`
  (@168.91,99.06) — the switching node. Inductor `(property "Reference" "L2"`
  is at `(at 182.88 93.345 90)`, sitting on the same y≈99 wire row physically
  between the `SW` label (x=168.91) and the `BUCK_OUT` label (x=233.68).
  So topology is: BUCK_IN → high/low-side switches → SW node → L2 → BUCK_OUT.
- **Confidence: HIGH**
  Direct schematic read; hierarchical-label shapes (input vs output) and L2
  placement between SW and BUCK_OUT confirm signal direction unambiguously.
- **Implication for our build / bench test:**
  - TP12 (`/BUCK/BUCK_IN`) = buck INPUT, before the switches/inductor. Safe DC
    node (input rail). Verifies the rail feeding the buck is present.
  - TP13 (`/BUCK/BUCK_OUT`) = buck OUTPUT, AFTER inductor L2 — it is the
    filtered/regulated DC output, NOT the SW switching node. SW is a distinct
    net with no TP in my range. So TP13 is a comparatively SAFE DC probe:
    operator powers BUCK_IN, firmware drives PA12/PA13 (PWM_H/PWM_L) PWM, and
    V(TP13) should rise and track the commanded duty — directly verifies the
    firmware PWM → buck output actuation. Use common bench GND OK.
  - HAZARD NOTE: the true switching node is net `SW`; it is high dv/dt. It is
    NOT on TP11–TP14, so none of my TPs is the dangerous switching node — but
    flag for whoever maps other TPs.
- **Why I'm recording it:**
  Confirms TP13 is post-inductor (safe, duty-tracking) — the cornerstone
  bench check for PWM-driven output; confirms TP12 is pre-switch input.

## Source 6: testingPCU.kicad_pcb netlist — /-PV connectivity (J6, IC10=LT6108, R17/R19/R32/R49)

- **URL / path:** `...\_pcb_files_agent_I\testingPCU.kicad_pcb` — net 60 pads
  at lines 6202, 6986, 13212, 17234, 18696, 26224; J6 pads ~26120–26260;
  IC10 footprint line 18424, `(property "Value" "LT6108IMS8-1#PBF")`
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  Net 60 `/-PV` pads belong to: R17, R19, R32, R49 (resistor/sense network),
  IC10, and connector J6. IC10 = `LT6108IMS8-1#PBF` (Linear Tech high-side
  current-sense amplifier + comparator); its pins include `/-PV`, `/+PV`,
  `GND`, `Net-(IC10-SENSEHI)`, `/OUTA1`, EN/~RST. Connector J6
  (`Connector:Screw_Terminal_01x03`, the PV-input screw terminal) has its
  three pins on nets `/-PV`, `GND`, and `/BUCK/BUCK_IN`.
- **Confidence: HIGH**
  Cross-referenced PCB pad-net ownership with component refs/values; multiple
  independent pads agree.
- **Implication for our build / bench test:**
  - DEFINITIVELY: TP11 `/-PV` = the PV INPUT NEGATIVE / sense-return rail at
    connector J6, feeding the LT6108 (IC10) current-sense network. It is NOT
    the "PV post-eFuse" positive rail (that inherited claim is WRONG for TP11).
  - J6 is the PV power-entry screw terminal: terminal pins = `-PV`, `GND`,
    and `/BUCK/BUCK_IN`. So PV power enters at J6 and `/BUCK/BUCK_IN` (=TP12)
    is the PV-derived buck input taken straight from J6 — TP12 voltage = the
    externally applied PV/source bench supply at J6.
  - Bench: with only 3.3 V on J5 and no PV applied at J6, TP11 ≈ 0 V / GND-ref.
    TP11 is essentially a return/sense node — probing it does NOT verify a
    firmware action (no firmware pin drives `/-PV`). Useful only as the PV
    current-sense reference / a clean PV-side GND-ish reference for differential
    measurements of PV input current via the LT6108.
  - Safe with common bench ground (low-side / return node). If PV bench
    supply later applied at J6, `/-PV` may sit a few mV–hundreds of mV off GND
    across the sense shunt — low voltage, safe.
- **Why I'm recording it:**
  Settles the corrected TP11 identity and tells the operator TP11 is not a
  firmware-actuation verification point — redirects PV-eFuse (PA16) checks to
  a +PV-side TP outside my range.

## Source 7: BAT.kicad_sch + testingPCU.kicad_sch + PCB netlist — /+BAT is BAT eFuse (IC7 TPS25940) OUTPUT

- **URL / path:**
  - PCB: `...\_pcb_files_agent_I\testingPCU.kicad_pcb` — net 17 pads (J7
    @line1787 pins BUCK_OUT/GND/+BAT; IC7 @line31920 = `TPS25940ARVCR`;
    IC8 @line3953 = `INA226AIDGSR`; IC11 @line15247 = `LT6108IMS8-1#PBF`);
    IC7 nets incl. net 17 `/+BAT` and net 19 `/VBAT`.
  - Root sch: `...\_pcb_files_agent_I\testingPCU.kicad_sch` — BAT sheet symbol
    pin `BAT_OUT2` `(at 307.34 203.2 0)` aligned in y with label `+BAT`
    `(at 320.04 203.2 180)`; pin `BAT_IN` `(at 276.86 196.85 180)`; pin
    `BAT_EN` `(at 276.86 204.47 180)`.
  - BAT sheet: `...\_pcb_files_agent_J\BAT.kicad_sch` — TPS25940 symbol pins
    `OUT_1/OUT_2/OUT_3` (pins 4/5/6); hierarchical outputs `BAT_OUT1/2/3`;
    hierarchical inputs `BAT_IN`, `BAT_EN`.
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  Net 17 `/+BAT` pads: J7 (battery screw terminal: pins = `/BUCK/BUCK_OUT`,
  `GND`, `/+BAT`), IC8 (INA226 power monitor), IC11 (LT6108 sense), plus
  R/C network. The BAT eFuse is IC7 = `TPS25940ARVCR`; it carries BOTH
  net 17 `/+BAT` and net 19 `/VBAT`. On the root sheet the `+BAT` label
  sits on the same wire row (y=203.2) as the BAT sheet's `BAT_OUT2` OUTPUT
  pin; `BAT_IN` is a separate pin (y=196.85) and `BAT_EN` another (y=204.47).
  BAT_OUT1/2/3 are TPS25940 OUT_x-derived hierarchical outputs.
- **Confidence: HIGH**
  Triangulated across PCB pad-net ownership, root-sheet pin/label coordinate
  alignment, and BAT sheet pin names. Consistent on all three.
- **Implication for our build / bench test:**
  - TP14 `/+BAT` = the BAT eFuse (IC7 TPS25940) OUTPUT distribution rail
    (`BAT_OUT2`), i.e. POST-eFuse — NOT the raw pre-eFuse battery terminal
    (raw side is `/VBAT`, the other IC7 power net). Also wired to J7 and to
    the INA226 (IC8) / LT6108 (IC11) monitors.
  - This makes TP14 a GOOD firmware-actuation probe: with battery (or a
    current-limited bench supply ≤ battery voltage) applied and IC7 enabled,
    firmware asserting the BAT eFuse enable (PA17 → BAT_EN) should make
    TP14 go LIVE (track input within eFuse drop); de-asserting PA17 should
    drop TP14 to ~0 V. Operator can directly verify PA17 → BAT eFuse on/off
    by watching TP14.
  - CAVEAT: J7 ties `/+BAT`, `GND`, and `/BUCK/BUCK_OUT` at one screw
    terminal — if a battery is wired to J7, TP14 may be back-fed by the
    battery regardless of eFuse state; for a clean PA17 actuation test the
    operator should feed the battery on the eFuse INPUT (`/VBAT`) side, not
    directly onto `/+BAT`/J7. Flag this to the bench operator.
  - Bench (only 3.3 V on J5, no battery): TP14 ≈ 0 V. Max safe probe if
    later powered: battery rail (≈ up to ~4.2 V / pack voltage) — low,
    safe with common bench ground. DC node, not switching.
- **Why I'm recording it:**
  Establishes TP14 as a post-eFuse rail usable to verify firmware PA17 BAT
  eFuse enable, with the J7 back-feed caveat that changes test wiring.

## Source 8: CONFLICT RECONCILIATION — PV.kicad_sch wire trace of TP11 vs PCB net; cross-check vs agents I/J/K/L

- **URL / path:**
  - `...\_pcb_files_agent_J\PV.kicad_sch`: TP11 symbol line 4599, UUID
    `922754e0-6d25-494d-a0a1-ec69dea26a8b`, pin1 @(260.35,50.8); wires
    `(xy 260.35 50.8)(xy 260.35 55.88)` line 2183, `(xy 260.35 55.88)
    (xy 266.7 55.88)` line 2653; junction @(260.35,55.88) line 1560;
    `(hierarchical_label "PV_OUT2" ... (at 266.7 55.88 0))` lines 2961-2963.
  - `...\_pcb_files_agent_I\testingPCU.kicad_pcb`: TP11 footprint
    `(path "/028ce7b3-.../922754e0-6d25-494d-a0a1-ec69dea26a8b")` line 33633
    (SAME UUID), pad1 `(net 60 "/-PV")` line 33674.
  - Cross-check (priority-3, re-verified, not copied): agent_I log src3
    (TP12=BUCK_IN, TP13=BUCK_OUT); agent_J log (TP11=PV_OUT2, TP14=BAT_IN,
    confirms PCB coords TP11 64.25,129.25 & TP14 118,98.5); agent_K
    (TP11=PV_OUT2 post-eFuse, TP14=pre-eFuse BAT_IN); agent_L
    (TP12=BUCK_IN, TP13=BUCK_OUT post-inductor — agrees with me).
- **Date accessed:** 2026-05-16
- **What this source gave me (plain English):**
  The TP11 schematic symbol and the TP11 PCB footprint share the SAME
  instance UUID (`922754e0-...`) — they are the same component, same project
  revision. In PV.kicad_sch TP11's pin wires up to a junction that runs to
  the `PV_OUT2` hierarchical label → schematic net = `PV_OUT2` (PV eFuse
  post-output). But the as-routed `.kicad_pcb` assigns that exact pad to
  `(net 60 "/-PV")`. Net 60 `/-PV` on the PCB ties J6, R17/R19/R32/R49,
  IC10(LT6108) — a PV-input/sense-return group, NOT the eFuse output group.
  => Genuine schematic-vs-PCB net-NAME conflict on TP11 (and by the same
  mechanism TP14: schematic trace says `BAT_IN` pre-eFuse per agents J/K,
  PCB pad says `/+BAT` which root sheet maps to BAT_OUT2 post-eFuse).
- **Confidence: HIGH (that a conflict exists); MEDIUM (which net is physically real)**
  Both files read directly with matching UUIDs. The conflict is real and
  reproducible. Which one matches the *physical* copper depends on whether
  the fab files were generated from this .kicad_pcb (most likely) or a
  re-netlisted schematic. KiCad PCB net names are imported from the
  schematic netlist; a stale "Update PCB from Schematic" can desync names.
- **Implication for our build / bench test:**
  - TP12 = `/BUCK/BUCK_IN`, TP13 = `/BUCK/BUCK_OUT`: NO conflict — PCB,
    schematic, and agents I/L all agree. HIGH confidence, use directly.
  - TP11 & TP14: DO NOT trust either name blindly on the bench. The
    operator MUST ring out TP11 and TP14 with a multimeter continuity
    test on the physical board BEFORE relying on them:
      * TP11: continuity to J6 `-PV` terminal & R17/IC10 ⇒ PCB `/-PV`
        (PV input/sense return, NOT a firmware-actuation point).
        Continuity instead to the PV eFuse (IC4) OUT side ⇒ schematic
        `PV_OUT2` (post-PV-eFuse; THEN it WOULD verify PA16 PV-eFuse en).
      * TP14: continuity to J7 & INA226(IC8)/IC11 on the BAT_OUT side ⇒
        post-eFuse `/+BAT` (verifies PA17 BAT-eFuse enable). Continuity to
        the raw battery/eFuse-IN side ⇒ schematic `BAT_IN` (pre-eFuse,
        NOT a clean PA17 verification point).
  - The .kicad_pcb (as-fabricated copper) is normally authoritative for a
    physical board, so my best-guess physical truth is TP11=`/-PV`,
    TP14=`/+BAT`(post-eFuse) — but the prior agents' schematic traces are
    self-consistent too, hence the mandatory bench continuity check.
- **Why I'm recording it:**
  This is the single most important caveat in the deliverable: TP11/TP14
  net identity is AMBIGUOUS between schematic and PCB; the operator must
  DMM-verify before using them for firmware-actuation proof.

---

# FINAL SUMMARY TABLE — TP11..TP14

| TP   | PCB net (as-fabricated, .kicad_pcb)            | PCB pos (F.Cu top) | Sheet | Subsystem / pre-post component | Bench voltage now (3V3 on J5 only) | Firmware action verifiable | Safety | Backing log src |
|------|------------------------------------------------|--------------------|-------|--------------------------------|-------------------------------------|----------------------------|--------|-----------------|
| TP11 | `/-PV` (net 60) — **CONFLICTS w/ schematic `PV_OUT2`** | (64.25, 129.25) | /PV/ | PCB: PV input neg / LT6108(IC10) sense-return @J6. Schem: post-PV-eFuse(IC4) PV_OUT2 | ~0 V (no PV applied) | If PCB(`/-PV`): NONE (return/sense, no fw pin drives it). If schem(`PV_OUT2`): PA16 PV-eFuse enable | Low-V return node, common GND OK | Src 4, 6, 8 |
| TP12 | `/BUCK/BUCK_IN` (net 16)                        | (81.25, 124.5)  | /BUCK/ | Buck INPUT, before switches/L2; fed from PV connector J6 pin | ~0 V (no PV/source on J6) | Indirect: confirms buck input rail present before PWM test | DC input rail, safe, common GND | Src 1, 5 |
| TP13 | `/BUCK/BUCK_OUT` (net 15)                       | (106.05, 124.5) | /BUCK/ | Buck OUTPUT, AFTER inductor L2 (NOT the SW switching node) | ~0 V (buck cold) | YES — power BUCK_IN, drive PA12/PA13 PWM, V(TP13) tracks duty | Safe DC (post-LC); SW node is separate, not here | Src 2, 5 |
| TP14 | `/+BAT` (net 17) — root sheet maps to BAT_OUT2; **CONFLICTS w/ schematic `BAT_IN`** | (118, 98.5) | /BAT/ | PCB+root: post-eFuse(IC7 TPS25940) BAT distribution, @J7 + INA226(IC8)/LT6108(IC11). Agents J/K: pre-eFuse BAT_IN | ~0 V (no battery) | If post-eFuse(`/+BAT`): PA17 BAT-eFuse enable on/off. If pre-eFuse(`BAT_IN`): NONE (raw input) | Battery-level (~≤4.2 V) DC, common GND OK; beware J7 back-feed | Src 3, 7, 8 |

Notes:
- TP12 & TP13: HIGH confidence, no conflict — use directly. TP13 is THE
  point to verify firmware PWM (PA12/PA13) → buck output actuation.
- TP11 & TP14: net identity AMBIGUOUS (schematic vs as-fabricated PCB).
  Operator MUST DMM-continuity-verify on the physical board before using
  them as firmware-actuation proof (procedure in Source 8).
- SW (buck switching node, high dv/dt hazard) is NOT on any of TP11–TP14.
- TP11–TP14 are all SMD pads on F.Cu (top/front silkscreen side).
