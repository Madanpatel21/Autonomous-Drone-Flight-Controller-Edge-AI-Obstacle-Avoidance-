# PCB Constraints & Stackup

Status: APPROVED 2026-10-03 (Phase 07). Board: FC30x30, 4-layer, HW-010. No EDA toolchain available in this environment — CAD execution is hardware-gated/external (see BLOCKERS in PROJECT_STATUS).

## Stackup (JLCPCB-class 4-layer, 1.6 mm)

| Layer | Purpose |
|---|---|
| L1 top | signals + components; IMU zone keep-out for airflow below |
| L2 GND | unbroken ground plane under ALL high-speed/driver circuits |
| L3 PWR | 5V/3V3 pours, battery feed on top layer segments |
| L4 bottom | signals, ESC connectors, mounting |

## Class rules

- Trace: signal 0.2 mm; power ≥1.0 mm (5 V ≥3 A), battery path ≥2.5 mm + 2 oz copper class
- Vias: 0.3/0.6 mm; power stitching 0.5/0.9 mm
- Clearance: battery/ESC nets ≥0.8 mm; logic ≥0.2 mm

## Critical constraints

1. **IMU isolation (HW-010)**: ICM-42688-P at geometric center, in plane of prop rotation; isolated mount (soft silicone posts, 3 mm standoff); keep-out radius 8 mm for other components; ground via fence around IMU zone; no traces under chip.
2. **DShot600**: TIM1/TIM8 outputs length-matched ±5 mm, series 33R at driver, referenced to unbroken L2.
3. **Battery feed**: TVS (SMBJ ≥17 V standoff) at entry; fuse; reverse MOS; star ground at pack entry to L2.
4. **ADC battery divider**: kelvin-routed to ADC pins, RC filter 1k/100n, away from DShot/ESC area.
5. **SPI 1-10 MHz**: ≤80 mm length, series termination on SCK, ground guard.
6. **Decoupling**: 100 nF + 1 µF per VDD pin of U1/U2/U3 <2 mm from pin; bulk 22 µF at 3.3 rail entry.
7. **Companion power switch**: high-side MOSFET + gate driver near connector, soft-start RC ≥10 ms.
8. **Mechanical**: 30.5 mm mounting pattern (M3), corner fillets, board edge ≥3 mm to first component; IMU center; USB on edge.
9. **Thermal**: 5 V buck exposed pad → 2×2 cm copper pour both outer layers + thermal vias (per POWER_ARCHITECTURE.md 1.3 W EST).

## Review gates

SCHEMATIC_REVIEW_CHECKLIST.md + PCB_REVIEW_CHECKLIST.md to be executed at CAD completion (H-gated); DRC/ERC runs in EDA tool at that time.
