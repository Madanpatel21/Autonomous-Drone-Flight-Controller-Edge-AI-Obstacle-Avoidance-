# Clock & Timing Budget

Status: APPROVED 2026-10-03 (DEC-004). Reference: FIRMWARE_ARCHITECTURE.md task table.

## CPU budget (per 1 ms control period, worst case, MCU-bound estimate for Phase 04 sizing)

| Component | Budget |
|---|---|
| IMU driver (SPI DMA) | 50 µs |
| Calibration+validation | 20 µs |
| Attitude propagation (quaternion EKF) | 60 µs |
| Rate PID + mixer | 40 µs |
| Motor output (DShot DMA) | 20 µs |
| Control-path sum | 190 µs of 1000 µs (19%) |
| All lower-rate tasks (amortized) | ≤400 µs |
| Headroom | ≥40% |

Acceptance: measured p99 per task ≤ deadline in FIRMWARE_ARCHITECTURE.md (verified in SIM timing harness; H later).

## Latency chain (SYS-002: ≤5 ms p99)

IMU sample ready (DMA ISR) → imu_read 0.2 ms → attitude propagate 0.1 ms → rate PID 0.05 ms → mixer+DShot issue 0.05 ms → ESC latch ≤2.0 ms (DShot600 frame+ESC update) → **total ≈2.4 ms typical, ≤5 ms p99 with jitter**.

## Sensor timing (DEC-004)

| Sensor | Rate | Interface | Staleness limit |
|---|---|---|---|
| IMU | 1 kHz (8 kHz capable) | SPI+DMA | 3 ms |
| Barometer | ≤50 Hz | SPI/I2C | 200 ms |
| GNSS | 10 Hz | UART | 500 ms |
| ToF (up+down) | ≥50 Hz | I2C/UART | 100 ms |
| Optical flow | ≥50 Hz | SPI | 100 ms |
| Battery ADC | 50 Hz | ADC+DMA | 200 ms |
| RC | 150–1000 Hz frame rate | UART (CRSF/SBUS) | 500 ms |
| Companion link | ≥100 Hz exchange | UART 1–3 Mbaud | 1 s heartbeat |

## Memory budget (Phase 04 verification targets)

| Item | Budget |
|---|---|
| Control path RAM (loops, state, mixers) | ≤32 KB |
| Estimator RAM | ≤24 KB |
| Buffers/logging (DMA ring, blackbox) | ≤64 KB |
| Total RAM floor (before stacks) | ≤128 KB → fits ≥192 KB requirement (HW-001) |
| Flash: app | ≤512 KB of ≥1 MB |
