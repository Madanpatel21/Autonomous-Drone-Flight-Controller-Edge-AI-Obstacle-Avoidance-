# AI AGENT CONTRACT — TOKEN-SAVING AUTONOMOUS DRONE PROJECT

## Mandatory execution rules

1. Work from the repository root.
2. Read `15_prompts/INDEX.md` and `00_project_control/PROJECT_STATUS.md` before starting.
3. Inspect existing files before creating or replacing anything.
4. Never recreate valid work.
5. Work only on the current phase, then verify it.
6. Reuse existing decisions, interfaces, code, requirements, and evidence.
7. Do not repeat unchanged context in output.
8. Verify hardware/protocol facts from authoritative sources before locking designs.
9. Never fabricate measurements, test results, hardware validation, flight results, or AI metrics.
10. Maintain traceability: requirement -> design -> implementation -> test -> evidence.
11. Keep code deterministic, bounded, fault-aware, diagnosable, and testable.
12. Flight stabilization, hard safety limits, and emergency behavior remain on the STM32 flight controller. Companion AI must never bypass them.

## Two-target architecture — mandatory

The real STM32 is the production flight-controller target. The project must also work without a physical STM32 through a deterministic simulation/HAL target.

```text
                 Shared flight-control application
                              |
                 +------------+------------+
                 |                         |
            STM32 TARGET              SIM TARGET
                 |                         |
          real peripherals          virtual peripherals
                 |                         |
                 +------------+------------+
                              |
                       same algorithms
```

Rules:
- Do not make algorithm development depend on the physical STM32.
- Put hardware-specific code behind the HAL.
- `02_firmware/hal/stm32/` is the production backend.
- `02_firmware/hal/sim/` is the hardware-free backend.
- Simulation must use the same application-level interfaces as STM32.
- A simulated result is never physical hardware evidence.
- Tests that require MCU timing, DMA, interrupts, electrical buses, ADC/PWM/DShot, watchdog hardware, or physical sensors must remain explicitly STM32-gated.

## Validation ladder

`Unit -> SIM/SIL -> HIL/bench -> STM32 bring-up -> motor-off integration -> controlled flight`

Never skip a required gate silently.

## Automatic prompt chaining

After every phase:
1. Update `00_project_control/PROJECT_STATUS.md`.
2. Record evidence and blockers.
3. Read `15_prompts/INDEX.md`.
4. Find the first incomplete phase.
5. Open that phase prompt.
6. Execute it automatically.
7. Do not ask the user to paste the next prompt.

If a genuine blocker requires a human decision, stop and record the exact decision required.

## Token-saving output

Return only:
- Status: COMPLETE/PARTIAL/BLOCKED
- Changed: paths only
- Tests: result only
- Evidence: paths/IDs only
- Blockers: only if present
- Next: phase + prompt filename
