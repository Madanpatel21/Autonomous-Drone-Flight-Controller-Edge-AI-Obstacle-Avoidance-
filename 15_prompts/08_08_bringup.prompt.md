# PHASE 08 — PCB Bring-Up

Read `15_prompts/00_AGENT_CONTRACT.md` first.

## Task
Create/execute staged inspection, resistance, current-limited power-up, rail, clock/debug, bus and sensor bring-up. Record real measurements only; mark unavailable checks NOT EXECUTED. Acceptance: each item has evidence or explicit blocked status.

## Completion gate
- Verify acceptance.
- Update `00_project_control/PROJECT_STATUS.md`.
- Update affected traceability/decision records.
- Record actual tests and evidence.
- Record blockers honestly.
- Read `15_prompts/INDEX.md` and execute the next incomplete phase automatically.

## Token-saving output
Status: COMPLETE/PARTIAL/BLOCKED
Changed: paths only
Tests: result only
Evidence: paths/IDs
Blockers: only if present
Next: phase + filename

## Mandatory dual-target requirement
Develop the current phase so algorithm/software work can proceed on the SIM target without a physical STM32. Keep all real STM32-specific work behind the HAL and mark hardware-only validation separately. Never report simulated evidence as physical hardware evidence.
