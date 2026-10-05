# Component Selection

Status: APPROVED 2026-10-03 (Phase 04). All specs below verified from vendor datasheets (sources listed); no invented electrical characteristics. Hardware parts still SIMULATED until Phase 08+ bring-up.

## MCU (HW-001, DEC-002)

| | Selection | Verified facts (datasheet) |
|---|---|---|
| Primary | **STM32F765VIT6** (LQFP100) | Cortex-M7 + FPU (single+double), 216 MHz, 2 MB flash, 512 KB RAM, 15 timers @216 MHz, 2 watchdogs, ART accelerator. Source: ST datasheet DS11532 (stm32f765bi.pdf), SparkFun-hosted STM32F765VI DS. |
| Alternate | STM32F405RGT6 (proven FC-class, 168 MHz M4F, 1 MB/192 KB) if F765 procurement constrained — meets HW-001 minimums. | ST DS8626 family DS. |

## Sensors

| Function | Selection | Verified facts | Source |
|---|---|---|---|
| IMU (HW-002) | **TDK ICM-42688-P** | 6-axis; gyro noise 2.8 mdps/√Hz, accel 70 µg/√Hz; 2 KB FIFO, up to 20-bit data; configurable ODR (≤32 kHz); SPI host interface | TDK DS-000347 |
| IMU alternate | ICM-40609 / BMI088 class | — | — |
| Barometer (HW-003) | **Bosch BMP390** | 24-bit; RMS pressure noise 0.02 Pa (lowest BW); relative accuracy ±0.03 hPa (≈±0.25 m); I2C/SPI | Bosch bst-bmp390-ds002 |
| GNSS (HW-004) | **u-blox NEO-M9N (M9N-00B)** | 4 concurrent GNSS (GPS, Galileo, GLONASS, BeiDou); update rate up to 25 Hz; UART/I2C | u-blox UBX-19014285 |
| ToF range (HW-005) | **Benewake TF-Luna ×2** (forward, downward) | Range 0.2–8 m; ±6 cm @0.2–3 m; 1 cm resolution; UART/I2C; configurable frame rate 1–250 Hz (default 100 Hz). NOTE: stable max rate to be bench-verified (product manual states stable max 10 Hz in some configurations) — recorded as hardware-gated verification item. | Benewake product page/manual |
| Optical flow (HW-005) | **PixArt PMW3901MB** module | Flow ASIC; working range 80 mm–infinity; SPI ≤2 MHz; supported by PX4 ecosystem | PixArt DS via Bitcraze POT0189; PX4 docs |
| Battery (HW-007) | Per-cell ADC divider (4S) + INA226-class current/shunt monitor | INA226: I2C, 36 V max bus, ±0.1% gain err (to be confirmed at schematic phase) | TI DS (confirm at schematic) |

## Companion computer (HW-008)

| | Selection | Notes |
|---|---|---|
| Primary | **Raspberry Pi 5 (4 GB) + Hailo AI Kit (Hailo-8L, 13 TOPS) + Camera Module 3** | Meets AI-003 (≤10 ms/frame) with acceleration; UART ≥1 Mbaud to FC per ICD-02; power-cyclable via FC GPIO relay/MOSFET. |
| Alternate | NVIDIA Jetson Orin Nano (if Hailo supply constrained) | Same ICD-02 interface; FC independent. |

Exact companion procurement is hardware-gated; interface frozen in ICD-02 so FC software does not depend on companion model.

## Motors/ESC/props/battery (SYS-007)

| Item | Selection class | Rationale |
|---|---|---|
| ESC | 4-in-1 BLHeli_32/AM32, DShot600, ≥45 A/ch | HW-006; AM32 open firmware configurable |
| Motors | 2207-class brushless, 1750–2400 KV | 5" prop, 4S: ~1.2–1.8 kg thrust/motor static (to be measured, H) |
| Props | 5×4.3×3 | SYS-007 |
| Battery | 4S 4500 mAh Li-ion (21700) or 4S 1500–2200 mAh LiPo | SYS-005/007; hover current to be measured |

## Regulators/connectors (hardware-gated, to be locked at Phase 06 schematic)

- 5 V ≥5 A buck from 4S (companion + servos of GS radio), 3.3 V buck/LDO for MCU/sensors, per ICD-05. TVS + fuse + reverse MOS per HW-009.

## Verification status

| HW req | Covered by | Verified? |
|---|---|---|
| HW-001 | STM32F765VI datasheet facts | YES (datasheet) |
| HW-002 | ICM-42688-P DS | YES (datasheet) |
| HW-003 | BMP390 DS | YES (datasheet) |
| HW-004 | NEO-M9N DS | YES (datasheet) |
| HW-005 | TF-Luna/PMW3901 DS | YES (datasheet, with bench-rate caveat above) |
| HW-006..010 | ESC/motor/PCB classes | Design-level; H-gated |
