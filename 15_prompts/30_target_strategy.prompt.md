# 30 — Target Strategy Override / Cross-Phase Rule

Read `15_prompts/00_AGENT_CONTRACT.md` first.

This is a cross-phase rule, not a replacement for the numbered phase sequence.

## Objective
Ensure every implementation can progress without a physical STM32 while preserving the real STM32 as the production target.

## Required structure
- Shared application/control logic
- HAL interfaces
- STM32 backend
- Simulation backend
- Hardware-free tests
- STM32-gated tests

## Acceptance
- No control algorithm directly depends on STM32 registers.
- SIM and STM32 expose equivalent application-level interfaces.
- CI/local test path can execute without STM32.
- Hardware-only claims are clearly marked as pending until physical evidence exists.

## Execution
Apply this rule whenever any phase introduces hardware dependencies. Do not wait until the hardware phase.
