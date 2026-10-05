# Hardware Target Strategy

## Production Target

The real flight controller is an STM32-based custom board. The production firmware target must run on the selected STM32 and interface with real sensors, ESCs, power monitoring, communication hardware, and safety mechanisms.

## Development Target

The project must remain buildable and testable without a physical STM32 board. A simulation/HAL backend supplies deterministic virtual sensors, actuators, timing, and faults.

## Two-Target Rule

```text
                 Shared flight-control application
                              |
                 +------------+------------+
                 |                         |
            STM32 target              SIM target
                 |                         |
        real peripherals            virtual peripherals
                 |                         |
                 +------------+------------+
                              |
                     same algorithms
```

## What can be validated without STM32

- algorithms
- sensor-fusion math
- PID/control logic
- motor mixing
- navigation
- mission logic
- obstacle-avoidance logic
- AI perception/inference
- communications protocol logic
- logging
- fault handling logic
- regression tests
- SIL

## What requires STM32

- MCU peripheral configuration
- interrupt/DMA behavior
- real timer/PWM/DShot timing
- ADC electrical behavior
- actual SPI/I2C/UART/CAN buses
- watchdog hardware behavior
- MCU resource/timing validation
- real sensor electrical integration
- ESC electrical/signaling integration
- final real-time validation

## Validation progression

`Hardware-free unit tests -> SIL -> HIL/bench -> STM32 peripheral bring-up -> motor-off integration -> controlled flight test`

No simulation result may be recorded as physical hardware evidence.
