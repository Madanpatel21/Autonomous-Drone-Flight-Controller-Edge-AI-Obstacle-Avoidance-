# Power Architecture

Status: APPROVED 2026-10-03 (Phase 05). Loads marked EST are engineering estimates for sizing, to be verified on hardware (H-gated) — not measured values.

## Power tree

```text
BT1 4S Li-ion/LiPo (12.0–16.8 V)
 |  +-- fuse (main) -- reverse-protection MOS -- TVS
 |        |
 |        +--> ESC 4-in-1 (DShot600) --> 4x motors      [raw pack]
 |        |
 |        +--> 5 V buck (≥6 A cont / 8 A pk, ≥92% eff class)
 |        |       +--> FC 5V (MCU VDD via 3.3 LDO/buck, radio, GNSS)
 |        |       +--> Companion RPi5+Hailo+camera (power-cyclable by FC GPIO MOSFET, HW-008)
 |        |       +--> telemetry radio
 |        +--> 3.3 V rail (MCU, ICM-42688-P, BMP390, PMW3901, TF-Luna via their regs)
 |        +--> battery monitor: per-cell divider -> ADC1 (HW-007), INA226 shunt (I2C)
```

## Budget (pack 4S; design targets)

| Load | Rail | Cont | Peak | Basis |
|---|---|---|---|---|
| Motors (full throttle) | raw | 30 A | 40 A | ESC class 45 A/ch (H: measure) |
| Companion (RPi5+Hailo+cam) | 5 V | 2.0 A | 3.0 A | EST per RPi5/Hailo class; H-verify |
| FC + sensors + radio | 5 V/3.3 V | 0.5 A | 0.8 A | EST; H-verify |
| Buck losses @93% class eff | — | ≈0.2 A equiv | — | from above |

5 V rail sizing: 6 A continuous / 8 A peak covers 2.8 A loads + 50% margin (HW/derating). 3.3 V rail 1 A.

## Thermal

Buck dissipation EST: P_out 15 W × (1/0.92−1) ≈ 1.3 W → needs exposed-pad copper area per selected reg (locked Phase 06). ESC and motors are airflow-cooled by props (H-verify).

## Transients / protection

- Motor surge: separate raw-pack path; 5 V rail isolated from pack spikes via buck + bulk caps; TVS on pack input (HW-009).
- Companion power cycle: MOSFET high-side switch, FC-controlled (HW-008); inrush limited by soft-start of buck.
- Brownout (SYS-005): UVLO behavior at 12.0 V pack (3.0 V/cell); FC sequences companion off before critical battery failsafe landing (SAF-030/031) to preserve control margin.

## Battery monitoring (HW-007)

- Per-cell voltage via 4× matched divider (≤1% resistors) → ADC1 + DMA @50 Hz.
- Pack current via INA226-class shunt monitor (I2C) @50 Hz; mAh integration in FC.
- Failsafe thresholds SAF-030: warn 3.5 V/cell, RTL 3.4 V/cell, land 3.1 V/cell — all on filtered values (sag compensation under load, H-calibrate).

## Unresolved rails

None — every load accounted (acceptance met). Regulator part numbers lock at Phase 06 schematic.
