# PCB & Assembly Inspection Criteria

Status: criteria **defined** (Phase 25), records **none** — no board exists. Every
number below is traceable to an approved document, not to a judgement call:

* class rules, stackup, IMU zone, DShot/SPI rules →
  [`PCB_CONSTRAINTS.md`](../../01_hardware/01_flight_controller/pcb/constraints/PCB_CONSTRAINTS.md) (APPROVED 2026-10-03)
* MCU/IMU/baro/GNSS/ToF/flow part numbers → [`../bom/CONTROLLED_BOM.csv`](../bom/CONTROLLED_BOM.csv)
* pin map / net intent → `PCB_DESIGN_RECORD.md` v1

Any measurement that fails a criterion is a reject. "Looks fine" is not an
acceptance basis, and a criterion that cannot be measured with the tools on hand
is an inspection gap that must be named (column *Basis* below), not waived.

## 1. Fabrication (incoming PCB, before assembly)

| # | Check | Accept | Reject | Basis |
|---|---|---|---|---|
| F1 | Layer count / stackup | 4 layers, 1.6 mm, L2 unbroken ground | any other | visual + vendor cert |
| F2 | Signal trace width | 0.2 mm ±10 % | <0.18 mm | microsection or vendor cert |
| F3 | Power trace width | ≥1.0 mm (5 V ≥3 A); battery path ≥2.5 mm | under width | microsection |
| F4 | Signal clearance | ≥0.2 mm | <0.2 mm | microsection / AOI |
| F5 | Battery/ESC net clearance | ≥0.8 mm | <0.8 mm | microsection |
| F6 | Board outline | 30.5 mm M3 pattern, ≥3 mm edge-to-component, corner fillets | out-of-tolerance | calipers |
| F7 | Via classes | 0.3/0.6 mm signal, 0.5/0.9 mm power stitching | wrong class in a power net | drill map + X-ray |
| F8 | Plating quality | no voids, no barrel cracks | any | X-ray / microsection |
| F9 | Solder mask / silk | layer order correct, ref-des legible | misregistration | visual |
| F10 | Warp | ≤0.5 % of diagonal | above | flat plate |

F3/F4/F5 exceed "typical" fab tolerance checks; they are the ones the constraint
doc calls out, so they are checked explicitly rather than assumed from a DRC run
made in a different tool version.

## 2. Assembly (per unit)

| # | Check | Accept | Reject | Basis |
|---|---|---|---|---|
| A1 | Ref-des placement vs BOM | every BOM row populated, no substitutions | any deviation | visual + BOM diff |
| A2 | Polarity / keying | all polarized parts oriented per drawing; connectors keyed per ICD-05 | any reversal | visual |
| A3 | QFN/LGA fillets | continuous fillet, no bridging; X-ray on IMU and MCU | void >25 % pad area, bridge | AOI + X-ray |
| A4 | IMU zone | chip at board centre, in prop plane; **8 mm keep-out respected**; soft silicone posts; no traces under the chip; via fence present | any encroachment | optical + X-ray |
| A5 | Decoupling | 100 nF + 1 µF within 2 mm of each U1/U2/U3 VDD pin; bulk 22 µF at 3V3 entry | missing or displaced | optical, measured |
| A6 | DShot stubs | series 33R at the driver; TIM1/TIM8 pairs length-matched ±5 mm; reference to L2 unbroken | over mismatch | TDR or length check |
| A7 | ADC divider | kelvin-routed, 1k/100n filter, away from DShot/ESC area | routing violation | optical |
| A8 | Thermal | 5 V buck pad → 2×2 cm pour both outer layers + thermal vias | missing pour/vias | optical + thermal image at PT-4 |
| A9 | Cleaning | no flux residue bridging; conformal coat (if applied) covers board only | any bridging | visual |

## 3. What this document cannot do

No board exists in this workspace, so **zero records exist** for F1–F10 and
A1–A9. These criteria are a specification for the first build, and the manifest
audit keeps the `BOARD` facility gate honest: the moment a record appears while
the gate says "no board", the audit fails rather than quietly counting the
record as evidence.
