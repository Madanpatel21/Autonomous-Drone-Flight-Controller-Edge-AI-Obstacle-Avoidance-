# BOM (Phase 04 draft — hardware-gated values TBD)

| Ref | Function | Part | Qty | Source of spec | Status |
|---|---|---|---|---|---|
| U1 | MCU | STM32F765VIT6 LQFP100 | 1 | ST DS11532 | selected |
| U2 | IMU | TDK ICM-42688-P (LGA-14) | 1 | TDK DS-000347 | selected |
| U3 | Baro | Bosch BMP390 | 1 | Bosch DS002 | selected |
| U4 | GNSS | u-blox NEO-M9N module | 1 | UBX-19014285 | selected (module on FC carrier or external) |
| U5,U6 | ToF | Benewake TF-Luna | 2 | Benewake manual | selected |
| U7 | Flow | PixArt PMW3901 module | 1 | PixArt POT0189 | selected |
| U8 | Current monitor | INA226 (I2C) + shunt | 1 | TI DS (confirm at schematic) | provisional |
| U9 | Companion | RPi5 4GB + Hailo AI kit + Cam Module 3 | 1 | RPi docs | selected |
| U10 | 5V buck | ≥5 A buck reg (part locked Phase 06) | 1 | — | TBD |
| U11 | 3.3V | buck/LDO (locked Phase 06) | 1 | — | TBD |
| ESC | — | 4-in-1 DShot600 ≥45A BLHeli_32/AM32 | 1 | HW-006 | class selected |
| M1–4 | Motors | 2207 1750–2400 KV | 4 | measured thrust H-gated | class selected |
| P1–4 | Props | 5×4.3×3 | 2 sets | — | class selected |
| BT1 | Battery | 4S Li-ion 4500 / LiPo 1500–2200 | 1 | SYS-005/007 | class selected |
| J* | Connectors | JST-GH keyed, per ICD-05 | — | HW-009 | TBD Phase 06 |
