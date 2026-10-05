# Firmware Build & Test README

## Build (SIM target — hardware-free, DEC-001)

```bash
cd 02_firmware
cmake -S . -B build
cmake --build build -j
./build/tests/fc_tests.exe     # unit tests (TEST-001/003)
./build/fc_sim.exe             # 1 kHz deterministic SIM run
```

STM32 target (`-DFC_TARGET=stm32`) is hardware-gated: requires arm-none-eabi toolchain + STM32 HAL drivers; backend implemented at Phase 08+ (HARDWARE_TARGET_STRATEGY.md).

## Layout (FW-001)

- `common/` shared types (fc_types.h)
- `hal/hal_interfaces.h` HAL contract (ICD-01); `hal/sim/` backend; `hal/stm32/` production backend
- `flight_controller/app/` scheduler (task table per FIRMWARE_ARCHITECTURE.md)
- `flight_controller/estimation/` attitude (quaternion + accel correction + bias est), altitude (baro/accel complementary)
- `flight_controller/control/` PID cascade (rate 1 kHz, attitude 250 Hz), anti-windup, derivative-on-measurement
- `flight_controller/motor_control/` quad-X mixer with saturation prioritization
- `flight_controller/failsafe/` failsafe SM (RC loss 500 ms, battery thresholds SAF-030)
- `tests/` unit tests (no external framework)

## Test results (2026-10-03)

22/22 PASS — mixer hover/saturation, attitude stationary/tilt convergence, failsafe battery thresholds, disarm zero-output, SIM determinism.
