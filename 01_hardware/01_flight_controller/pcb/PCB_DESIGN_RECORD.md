# PCB Design Record & Pin Assignment

Status: APPROVED 2026-10-03 (Phase 07). LQFP100 STM32F765VIT6 pin map v1 (frozen with ICD-05; changes require decision-log entry). Verify against DS11532 at schematic capture (H-gated).

## Pin map v1

| Function | Peripheral | Pins (LQFP100, tentative) | Notes |
|---|---|---|---|
| IMU SPI1 | SPI1 master | PA5 SCK, PA6 MISO, PA7 MOSI, PA4 CS, PD0 INT | 10 MHz max, DMA2 |
| Baro I2C1 | I2C1 | PB8 SCL, PB9 SDA | BMP390 addr 0x77 |
| ToF x2 I2C2 | I2C2 | PB10 SCL, PB11 SDA | TF-Luna 0x10/0x11 |
| Flow SPI2 | SPI2 | PB13 SCK, PB14 MISO, PB15 MOSI, PB12 CS | PMW3901 ≤2 MHz |
| GNSS UART1 | USART1 | PA9 TX, PA10 RX | 38.4 kbaud default → 460800 configured |
| RC UART3 | USART3 | PB11 alt/PC10 TX, PC11 RX | CRSF 420 kbaud |
| Telemetry UART2 | USART2 | PD5 TX, PD6 RX | MAVLink 57600 |
| Companion UART6 | USART6 | PC6 TX, PC7 RX | 1–3 Mbaud, ICD-02 |
| DShot M1–M4 | TIM1 CH1..CH4 / TIM8 CH1..CH4 | PE9,PE11,PE13,PE14 (TIM1) | 33R series, DMA2 |
| Battery ADC | ADC1 IN1..IN5 | PA0–PA3 cells, PA4 spare | 1k/100n RC |
| Companion power | GPIO | PE0 gate (MOSFET), PE1 fault sense | HW-008 |
| Buzzer/LED | GPIO/TIM | PB0 buzzer, PB1 LED1, PB2 LED2 | |
| SWD | debug | PA13 SWDIO, PA14 SWCLK | 4-pin header + NRST |
| CAN (optional) | CAN1 | PB5/PB6 (alternate) | reserved per ICD-05 |

## Design record

- Layer stack & class rules: PCB_CONSTRAINTS.md
- Reviews: schematic/PCB checklists execute at CAD completion (hardware-gated)
- DRC/ERC: not executable here (no EDA) — recorded BLOCKED/NOT EXECUTED, not failed

## Status

- Electrical design constraints: COMPLETE
- CAD (schematic/layout/gerbers): BLOCKED — requires EDA tool (KiCad) run by user/hardware phase; all specs frozen for capture
