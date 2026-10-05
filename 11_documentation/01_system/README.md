# 01_system — system-level documentation

Authoritative documents:

| What | Where |
|---|---|
| System architecture, decomposition, interfaces | [`00_project_control/03_architecture/SYSTEM_ARCHITECTURE.md`](../../00_project_control/03_architecture/SYSTEM_ARCHITECTURE.md) |
| Interface control (ICD-01..05), bus and protocol ownership | [`00_project_control/INTERFACE_CONTROL_DOCUMENT.md`](../../00_project_control/INTERFACE_CONTROL_DOCUMENT.md), [`MASTER_INTERFACE_DIAGRAM.md`](../../00_project_control/MASTER_INTERFACE_DIAGRAM.md) |
| Production-vs-development target strategy (**read this first**) | [`00_project_control/HARDWARE_TARGET_STRATEGY.md`](../../00_project_control/HARDWARE_TARGET_STRATEGY.md) |
| Requirement baseline (83 IDs, with acceptance + ver method) | [`00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md`](../../00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md) |
| Current phase status, evidence and open items | [`00_project_control/PROJECT_STATUS.md`](../../00_project_control/PROJECT_STATUS.md) |
| Why each architectural choice was made | [`00_project_control/DECISION_LOG.md`](../../00_project_control/DECISION_LOG.md) |

## What actually exists here

The system is a **STM32-class flight controller plus an advisory companion AI**,
developed against a hardware-free simulation target because no board is available
in this workspace ([DEC-001](../../00_project_control/DECISION_LOG.md)). Three
build targets share one application: `sim` (development), `hil` (host rig behind
a framed link), `stm32` (production skeleton).

## Read next

* Building and running anything: [`../NEW_ENGINEER_WALKTHROUGH.md`](../NEW_ENGINEER_WALKTHROUGH.md)
* Which parts are real vs. gated: [`../DOCUMENTATION_INDEX.md`](../DOCUMENTATION_INDEX.md)
* Requirements status per ID: [`08_testing/VERIFICATION_REPORT.md`](../../08_testing/VERIFICATION_REPORT.md)

## Gated

Nothing in this area is hardware-gated: the architecture and requirements are
documents. `SYS-001`/`SYS-002` (loop rate, latency) are **PARTIAL/OPEN (H)** —
their software side is bounded by measurement, but MCU timing needs a real board
(`HIL-3`).
