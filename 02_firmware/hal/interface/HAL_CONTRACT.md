# HAL Contract

All production control algorithms must consume abstract sensor/actuator interfaces.

Required interfaces:
- IMU
- Barometer
- GPS
- Optical flow
- ToF/LiDAR
- Battery voltage/current
- RC input
- Telemetry
- Motor output
- Time/clock
- Watchdog/fault status

`stm32/` implements these interfaces with real MCU peripherals.
`sim/` implements the same interfaces with deterministic simulated data.

No flight-control algorithm may directly depend on STM32 register APIs.
