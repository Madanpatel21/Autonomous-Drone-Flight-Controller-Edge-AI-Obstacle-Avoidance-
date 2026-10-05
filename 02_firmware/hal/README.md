# Hardware Abstraction Layer

The flight-control application is hardware-independent at the algorithm boundary.

Targets:
- `stm32/`: production MCU backend for the real STM32 flight controller.
- `sim/`: hardware-free backend for SIL, unit tests, regression, and development before the STM32 board is available.
- `interface/`: stable interfaces shared by both targets.

Rule: simulation replaces hardware dependencies; it does not replace the production STM32 target.
