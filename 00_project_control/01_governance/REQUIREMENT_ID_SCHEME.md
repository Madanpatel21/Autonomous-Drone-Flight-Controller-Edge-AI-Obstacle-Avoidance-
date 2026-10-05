# Requirement ID Scheme

## Format
`<DOMAIN>-<nnn>` — sequential per domain, never reused after deletion (superseded only).

## Domains
| Prefix | Domain | File |
|---|---|---|
| SYS | System-level | 00_project_control/SYSTEM_REQUIREMENTS.md |
| HW | Hardware | 00_project_control/HARDWARE_REQUIREMENTS.md |
| FW | Firmware/architecture | 00_project_control/FIRMWARE_REQUIREMENTS.md |
| SEN | Sensors/calibration/validation | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| EST | Estimation/fusion | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| CTRL | Flight control | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| NAV | Navigation/mission | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| COM | Communication | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| GS | Ground station | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| SIM | Simulation/SIL | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| TEST | Verification/testing | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| SAF | Safety/failsafe | 00_project_control/SAFETY_REQUIREMENTS.md |
| AI | Edge-AI perception | 00_project_control/AI_REQUIREMENTS.md |
| MFG | Manufacturing | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |
| OPS | Operations/procedures | 00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md |

## Status vocabulary
DRAFT → APPROVED → IMPLEMENTED → VERIFIED. Deprecated requirements are struck through with a superseding ID.

## Rules
- Every requirement: single "shall", measurable acceptance criterion, explicit verification method (A=analysis, I=inspection, R=review, T=test on SIM/SIL, H=test on hardware).
- Hardware-only verification is marked `H` and is recorded only after execution on the real STM32.
- SIM evidence never satisfies an `H` requirement.
