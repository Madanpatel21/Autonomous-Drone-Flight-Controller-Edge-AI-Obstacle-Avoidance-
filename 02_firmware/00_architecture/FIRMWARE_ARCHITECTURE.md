# Firmware Architecture

Status: APPROVED 2026-10-03 (Phase 03). Decisions DEC-001..005 apply.

## Layering (enforced, see FW-001)

```text
        application (mission, telemetry, ground/companion link logic)
                    |
        flight-control core (sensors→estimation→control→mixer, failsafe SM)
                    |
              HAL interfaces (pure C headers, no backend includes)
              +------------------+------------------+
              |                  |                  |
        hal/sim backend    hal/stm32 backend   (future hil backend)
        virtual sensors    SPI/I2C/UART/ADC
        virtual actuators  timers/PWM/DShot
        fault injection    DMA/ISR, watchdog, flash
```

- `02_firmware/common/` — math, filters, geometry, protocols, data types, utilities. Hardware-independent.
- `02_firmware/flight_controller/` — application + core algorithms. Hardware-independent.
- `02_firmware/hal/` — interface headers only; `hal/sim/` and `hal/stm32/` implement them.
- Rule: nothing under `common/` or `flight_controller/` may include `hal/sim/*` or `hal/stm32/*` (FW-001, include-audit enforced).

## Scheduler task table (Phase 03 freeze; DEC-004)

| Task | Rate | Priority | Deadline | Notes |
|---|---|---|---|---|
| imu_read | 1 kHz | 0 (highest) | 200 µs | SPI DMA complete callback driven |
| attitude_update | 1 kHz | 1 | 400 µs | EKF propagate (every IMU sample) |
| rate_control | 1 kHz | 1 | 500 µs | + mixer + motor output in same slot |
| attitude_control | 250 Hz | 2 | 800 µs | outer loop |
| sensor_lowrate (baro, battery) | 50 Hz | 3 | 1 ms | |
| position_control | 50 Hz | 3 | 1 ms | GNSS/flow fusion consumers |
| navigation/mission | 50 Hz | 4 | 2 ms | |
| companion_link | event/100 Hz | 4 | 1 ms | RX parse + TX queue |
| telemetry/GS | 10 Hz | 5 | 2 ms | MAVLink |
| failsafe_monitor | 100 Hz | 2 | 500 µs | link/battery/est health |
| logging | 250 Hz | 6 (lowest) | best effort | drops first under load (FW-006) |

Deadline miss on rate_control → failsafe escalation (SAF-004).

## Execution targets

- SIM: POSIX/Linux build; scheduler driven by deterministic virtual clock; sim physics feeds HAL inputs (SIM-002).
- STM32: FreeRTOS-style fixed-priority or bare-metal tick scheduler; watchdog feeds on rate_control deadline (SAF-004); same task table.

## Data flow

Raw IMU → calibration (SEN-003) → validation (SEN-002) → EKF propagate/update (EST-001) → attitude/rate cascade (CTRL-001) → quad-X mixer (CTRL-003) → DShot/PWM (HW-006). Low-rate: baro/battery/ToF/GNSS/flow feed estimator secondary updates and failsafe monitors.

## Motor/ESC interface

Mixer outputs [0..1] × 4 → DShot600 frames (primary) or 1–2 ms PWM (fallback) behind one HAL actuator interface.
