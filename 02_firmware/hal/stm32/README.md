# HAL STM32 Backend

Status: SKELETON, NOT COMPILED, NOT VERIFIED (no arm-none-eabi toolchain in this environment; no physical board). This is production-target structure only.

- Target MCU: STM32F765VIT6 (DEC-002) — 216 MHz Cortex-M7, 2 MB flash, 512 KB RAM.
- Pin map: `01_hardware/01_flight_controller/pcb/PCB_DESIGN_RECORD.md`.
- Build (requires: arm-none-eabi-gcc + STM32F7 HAL/CubeMX-generated project):
  `cmake -S . -B build-stm32 -DFC_TARGET=stm32 -DCMAKE_TOOLCHAIN_FILE=STM32F765.cmake`
  Toolchain file + startup + linker script are added at Phase 08 bring-up.
- All STM32 timing/peripheral validation is hardware-gated (HARDWARE_TARGET_STRATEGY.md). Nothing recorded from this directory counts as hardware evidence.
