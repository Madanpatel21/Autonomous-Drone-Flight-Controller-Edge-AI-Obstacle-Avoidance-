# Documentation Index

Phase 26 entry point for this repository. Read the task you are trying to do,
then follow one link.

## I want to…

| Task | Go to |
|---|---|
| Understand what this project is and what is real | [`../README.md`](../README.md), then [`01_system/README.md`](01_system/README.md) |
| **Build and run it for the first time** | [`NEW_ENGINEER_WALKTHROUGH.md`](NEW_ENGINEER_WALKTHROUGH.md) |
| See which requirements are met, partial or open | [`08_testing/VERIFICATION_REPORT.md`](../08_testing/VERIFICATION_REPORT.md) (one row per baseline ID) |
| Fix a failure I am staring at | [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md) |
| Change firmware behaviour safely | [`03_firmware/README.md`](03_firmware/README.md) → [`12_safety/README.md`](12_safety/README.md) before touching the failsafe path |
| Build or inspect the manufacturing package | [`11_manufacturing/README.md`](11_manufacturing/README.md) |
| Know what is hardware-gated and why | [`02_hardware/README.md`](02_hardware/README.md), [`PROJECT_STATUS.md`](../00_project_control/PROJECT_STATUS.md) |
| See why a design decision was made | [`00_project_control/DECISION_LOG.md`](../00_project_control/DECISION_LOG.md) |
| Know what a doc is allowed to claim | this file, section "Scaffolds and stubs" |

## Domain indexes

[01_system](01_system/README.md) · [02_hardware](02_hardware/README.md) ·
[03_firmware](03_firmware/README.md) · [04_sensor_fusion](04_sensor_fusion/README.md) ·
[05_flight_control](05_flight_control/README.md) · [06_navigation](06_navigation/README.md) ·
[07_edge_ai](07_edge_ai/README.md) · [08_communication](08_communication/README.md) ·
[09_simulation](09_simulation/README.md) · [10_testing](10_testing/README.md) ·
[11_manufacturing](11_manufacturing/README.md) · [12_safety](12_safety/README.md) ·
[13_user_manual](13_user_manual/README.md)

## Authoritative documents

Machine-checked by `11_documentation/tools/docs_audit.py` (regression step 10):
each row below is verified to exist, and its declared status is verified in both
directions — a document marked `content` here must not be a scaffold or a task
stub, and one marked `scaffold`/`task stub` must actually be marked on disk.

<!-- docs-audit:authoritative -->
| Document | Area | Status |
|---|---|---|
| `00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md` | requirements baseline | content |
| `00_project_control/PROJECT_STATUS.md` | phase status | content |
| `00_project_control/DECISION_LOG.md` | decisions | content |
| `00_project_control/01_governance/TRACEABILITY_MATRIX.md` | traceability | content |
| `00_project_control/03_architecture/SYSTEM_ARCHITECTURE.md` | system | content |
| `00_project_control/INTERFACE_CONTROL_DOCUMENT.md` | interfaces | content |
| `00_project_control/HARDWARE_TARGET_STRATEGY.md` | targets | content |
| `00_project_control/04_safety/SAFETY_CASE.md` | safety | content |
| `00_project_control/04_safety/FAILURE_MODE_AND_EFFECTS_ANALYSIS.md` | safety | content |
| `00_project_control/04_safety/FAULT_TREE_ANALYSIS.md` | safety | content |
| `00_project_control/04_safety/PRELIMINARY_HAZARD_ANALYSIS.md` | safety | content |
| `01_hardware/00_system/COMPONENT_SELECTION.md` | hardware | content |
| `01_hardware/02_power/POWER_ARCHITECTURE.md` | hardware | content |
| `01_hardware/01_flight_controller/pcb/constraints/PCB_CONSTRAINTS.md` | hardware | content |
| `01_hardware/01_flight_controller/pcb/PCB_DESIGN_RECORD.md` | hardware | content |
| `02_firmware/00_architecture/FIRMWARE_ARCHITECTURE.md` | firmware | content |
| `02_firmware/hal/interface/HAL_CONTRACT.md` | firmware | content |
| `02_firmware/02_estimation/STATE_ESTIMATION_VALIDATION.md` | estimation | content |
| `02_firmware/03_control/CONTROL_VALIDATION.md` | control | content |
| `04_navigation/NAVIGATION_VALIDATION.md` | navigation | content |
| `06_communication/COMMUNICATION_DESIGN.md` | communication | content |
| `06_communication/ground_station/GROUND_STATION.md` | ground station | content |
| `03_edge_ai/perception/PERCEPTION_FUSION.md` | edge AI | content |
| `03_edge_ai/avoidance/OBSTACLE_AVOIDANCE.md` | edge AI | content |
| `07_simulation/SIMULATION_VALIDATION.md` | simulation | content |
| `07_simulation/SIM_TUNING_RECORD.md` | simulation | content |
| `07_simulation/hardware_in_the_loop/HIL_DESIGN.md` | HIL | content |
| `08_testing/VERIFICATION_REPORT.md` | testing | content |
| `08_testing/MASTER_VERIFICATION_PLAN.md` | testing | content |
| `08_testing/TEST_GENERATION.md` | testing | content |
| `08_testing/regression_tests/REGRESSION.md` | testing | content |
| `08_testing/VNV_REVIEW.md` | verification | content |
| `08_testing/FINAL_AUDIT.md` | final audit | content |
| `12_manufacturing/RELEASE_MANIFEST.json` | manufacturing | content |
| `12_manufacturing/MANUFACTURING_RELEASE_CHECKLIST.md` | manufacturing | content |
| `12_manufacturing/PROGRAMMING_AND_PRODUCTION_TEST.md` | manufacturing | content |
| `13_release/RELEASE_MANIFEST.json` | release | content |
| `13_release/RELEASE_NOTES.md` | release | content |
| `11_documentation/PROJECT_FACTS.json` | documentation | content |
| `11_documentation/NEW_ENGINEER_WALKTHROUGH.md` | documentation | content |
| `11_documentation/TROUBLESHOOTING.md` | documentation | content |
| `15_prompts/00_AGENT_CONTRACT.md` | process | content |
| `00_project_control/PROJECT_CHARTER.md` | project control | task stub |
| `08_testing/FLIGHT_TEST_READINESS_REVIEW.md` | readiness | task stub |
| `02_firmware/02_estimation/STATE_ESTIMATION_SPEC.md` | estimation | task stub |
| `05_ground_station/GROUND_STATION_SPEC.md` | ground station | task stub |
| `00_project_control/ACCEPTANCE_CRITERIA.md` | project control | task stub |
| `03_edge_ai/03_inference/AI_INFERENCE_BENCHMARK.md` | edge AI | task stub |
<!-- /docs-audit:authoritative -->

## Scaffolds and stubs — read this before citing any document

This repository was scaffolded with one README per topic directory before any
content existed. Most of those directories are still empty, and their READMEs
originally contained a single sentence that read like documentation. Two classes
are now **marked and machine-checked**, so "there is a document" can never be
mistaken for "there is content":

| Marker | Meaning | How many |
|---|---|---|
| `**Scaffold — no content yet.**` | The directory exists for layout stability; no artifact has been produced there. | see `docs_audit.py` output |
| `**Task stub — not a record.**` | The file states what a document *must cover*; the covering document does not exist. **Never cite as evidence.** | see `docs_audit.py` output |

`docs_audit.py` fails the regression if any unmarked placeholder or unmarked
imperative-only stub appears anywhere in the repository, so a new empty file
cannot masquerade as content later.

## What this index does not claim

It does not claim the repository is fully documented. It claims that every
document's status is now knowable without reading it, and that the real content
for each area is one link away — which is what "workflow reproducible by a new
engineer" can actually mean for a project with a hardware-gated half.
