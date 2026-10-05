# Autonomous Drone Flight Controller + Edge-AI Obstacle Avoidance

## Development model

This project has two execution targets:

- **STM32 target:** the real production flight controller and final embedded target.
- **SIM target:** hardware-free development and Software-In-The-Loop testing when the STM32 board is not physically connected.

Both targets use the same flight-control application through a Hardware Abstraction Layer (HAL).

```text
                   Flight-control application
                              |
                 +------------+------------+
                 |                         |
              STM32                       SIM
          real peripherals          virtual peripherals
                 |                         |
                 +------------+------------+
                              |
                         same algorithms
```

## Validation

`Unit -> SIM/SIL -> HIL/bench -> STM32 bring-up -> motor-off -> controlled flight`

Simulation and hardware-free tests do not count as physical hardware validation.

## Agent prompts

All build prompts are included in `15_prompts/`.

Start the AI agent with:

`15_prompts/START_HERE.md`

The agent automatically finds and executes the next incomplete phase after each phase.
