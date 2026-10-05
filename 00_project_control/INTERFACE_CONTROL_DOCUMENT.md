# Interface Control Document

Status: APPROVED 2026-10-03 (Phase 03). Interfaces frozen here; changes require decision-log entry.

## ICD-01 HAL interfaces (C headers, `02_firmware/hal/`)

All interfaces return `hal_status_t { OK, BUSY, ERROR, TIMEOUT, INVALID }` and never block in control path (FW-003).

| Interface | Key functions (conceptual) | SIM backend | STM32 backend |
|---|---|---|---|
| hal_time | `time_us()`, `delay_us()`, `sleep_until(deadline)` | virtual clock | DWT/timer |
| hal_imu | `imu_start(cb)`, `imu_read(&sample)` | virtual IMU w/ faults | SPI+DMA |
| hal_baro / hal_gnss / hal_tof / hal_flow | `*_read(&data)`, `*_healthy()` | virtual w/ fault inject | I2C/SPI/UART |
| hal_rc | `rc_read(&frame)` | scripted frames | UART CRSF/SBUS |
| hal_actuator | `actuator_write(motor[4])` | records to sim physics | DShot600 DMA / PWM |
| hal_battery | `battery_read(&v,i,cell_v[])` | virtual battery model | ADC+DMA |
| hal_flash | `flash_read/write/erase` | file-backed mock | internal flash |
| hal_wdg | `wdg_feed(ms)`, `wdg_setup(ms)` | counters/log | IWDG |
| hal_uart | `uart_open(cfg)`, `uart_write/read_async` | loopback/pipes | USART+DMA |

Versioning: `HAL_INTERFACE_VERSION` incremented on any signature change; both backends must match.

## ICD-02 Companion link (FC ↔ companion, UART, DEC-003)

Frame: `SYNC(2) | LEN(1) | TYPE(1) | SEQ(1) | PAYLOAD(0..N) | CRC16-CCITT(2)`, max payload 256 B, ≥1 Mbaud 8N1.

| Type | Dir | Rate | Payload |
|---|---|---|---|
| 0x01 HEARTBEAT | comp→FC | 1 Hz | uptime, ai_state, model_id |
| 0x02 OBSTACLE_SET | comp→FC | 10–50 Hz | count(≤16) × {position xyz i16 cm, velocity xyz i16 cm/s, radius u8 cm, confidence u8, class u8} |
| 0x03 HEALTH | comp→FC | 1 Hz | cpu, mem, temp, camera_ok, inference_ms |
| 0x10 FC_STATE | FC→comp | 50 Hz | attitude quat, velocity, mode, failsafe flags |
| 0x11 FC_CONFIG_ACK | FC→comp | on change | params echo |

Rules: FC ignores stale (timestamped) sets >200 ms; confidence <0.5 detections dropped at link layer (AI-001/SAF-041). Bounded velocity command from avoidance: FC clamps to SAF-040 limits.

## ICD-03 Ground station link (MAVLink v2 over telemetry UART)

Standard MAVLink v2 framing; messages: HEARTBEAT, ATTITUDE, LOCAL_POSITION_NED, SYS_STATUS, RC_CHANNELS, COMMAND_LONG (modes/RTL), PARAM_REQUEST/SET. GS cannot arm (COM-004): COMMAND_LONG ARM/DISARM requires RC-level gate; GS emergency-stop = motor-stop command processed only when disarmed-safe state confirmed.

## ICD-04 RC protocol

CRSF primary (420 kbaud, 11-bit channels, failsafe on ≥500 ms silence per SYS-003), SBUS fallback.

## ICD-05 Electrical (frozen at schematic phase 06)

UART1 GNSS, UART3 RC, UART6 companion, UART2 telemetry; SPI1 IMU, SPI2 flow; I2C1 ToF/baro; TIM1/TIM8 DShot; ADC1 battery. Pin map frozen in Phase 06 schematic docs.
