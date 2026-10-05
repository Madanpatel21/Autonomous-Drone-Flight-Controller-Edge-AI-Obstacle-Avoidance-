# Release Documentation Checklist

What a release must be able to say about itself, and where the artifact that says
it lives. Every line carries a **status** and either **evidence** (an artifact
that exists, digest-pinned where the manufacturing package owns it) or a
**gate** (the requirement or facility blocker that stops it). No line may be
marked done by assertion; the final row is the mechanism that keeps the other
rows true.

This checklist describes the documentation side of a release. The manufacturing
side is [`12_manufacturing/MANUFACTURING_RELEASE_CHECKLIST.md`](../12_manufacturing/MANUFACTURING_RELEASE_CHECKLIST.md);
the operational release record is `OPS-001` (**PARTIAL** — the software/evidence
release record exists and is audited; an operational release stays gated).

| # | Item | Status | Evidence / gate |
|---|---|---|---|
| 1 | Architecture baseline | **DONE** | [`SYSTEM_ARCHITECTURE.md`](../00_project_control/03_architecture/SYSTEM_ARCHITECTURE.md); interfaces in [`INTERFACE_CONTROL_DOCUMENT.md`](../00_project_control/INTERFACE_CONTROL_DOCUMENT.md) |
| 2 | Requirements baseline | **DONE** | [`REQUIREMENTS_DOMAINS.md`](../00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md) — 83 baseline IDs, each with a status row and a traceability row, enforced by `08_testing/audit_requirements.py` (regression step 7) |
| 3 | Hardware revision | **PARTIAL** | selection and constraints approved ([`COMPONENT_SELECTION.md`](../01_hardware/00_system/COMPONENT_SELECTION.md), [`PCB_CONSTRAINTS.md`](../01_hardware/01_flight_controller/pcb/constraints/PCB_CONSTRAINTS.md)); no layout, so no revision can be released → gate `HW-010` / `EDA_TOOLCHAIN` |
| 4 | BOM | **DONE** | [`CONTROLLED_BOM.csv`](../12_manufacturing/bom/CONTROLLED_BOM.csv), 16 rows, revision matches the package; every unpinned row names a gate |
| 5 | Firmware source + build instructions | **PARTIAL** | SIM/host build reproducible ([`NEW_ENGINEER_WALKTHROUGH.md`](NEW_ENGINEER_WALKTHROUGH.md)); the STM32 image does not exist → gate `STM32_TOOLCHAIN` |
| 6 | Parameters | **DONE** | [`params_defaults.bin`](../12_manufacturing/parameters/params_defaults.bin) + field manifest, regenerated from the firmware's own defaults every run (step 8); version and CRC re-derived by the docs audit |
| 7 | AI model and model card | **GATED** | no model, no dataset, no latency measurement exists ([`07_edge_ai/README.md`](07_edge_ai/README.md)); the companion interface must degrade safely without one (`ai_loss` scenario) |
| 8 | Calibration records | **NONE (honest)** | no board and no sensors to calibrate; PT-3/PT-4 gate `BOARD` |
| 9 | Test evidence | **PARTIAL** | 2741/2741 unit/integration checks and 33 executed regression steps (2 H-gated SKIPs); the V&V review gate classifies all 83 requirements — 49 verified, 34 unverified with written blockers, 0 failed; host HIL byte-stream evidence; **no hardware evidence exists** |
| 10 | Known limitations | **DONE** | [`PROJECT_STATUS.md`](../00_project_control/PROJECT_STATUS.md), walkthrough §"What you still cannot do here", safety case non-claims, five UNDETECTED fault-tree branches carried openly |
| 11 | Safety/operational restrictions | **DONE** | [`SAFETY_CASE.md`](../00_project_control/04_safety/SAFETY_CASE.md), FMEA/PHA; [`13_user_manual/README.md`](13_user_manual/README.md) explains why there is deliberately no operator manual yet |
| 12 | Manufacturing files | **PARTIAL** | package `MFG-A`: 8 items present and digest-pinned, 8 gated by named blockers ([`RELEASE_MANIFEST.json`](../12_manufacturing/RELEASE_MANIFEST.json)) |
| 13 | Release notes | **PARTIAL** | [`13_release/RELEASE_NOTES.md`](../13_release/RELEASE_NOTES.md) + digest-pinned [`RELEASE_MANIFEST.json`](../13_release/RELEASE_MANIFEST.json) (RC-2), audited by runner step 12; an operational (flyable) release remains gated on `BOARD`/`STM32_TOOLCHAIN` |
| 14 | Documentation status is truthful | **DONE (enforced)** | every empty document carries a scaffold/task-stub banner, and `11_documentation/tools/docs_audit.py` (regression step 10) fails on an unmarked stub, a mis-declared index row, a broken entry-point link, or a number in prose that disagrees with [`PROJECT_FACTS.json`](PROJECT_FACTS.json) |

## How this checklist is verified

```
python 11_documentation/tools/docs_audit.py --observed <counts from this run>
```

Regression step 10. It fails on: an authoritative index row whose declared
status is false in either direction, a broken relative link in any entry
document, an unmarked placeholder or instruction-only stub anywhere in the
repository, a derived fact that disagrees with the file that owns it (telemetry
schema/payload, cmd-gate version, parameter blob version/CRC, package revision
and item counts), or an observed count — suite size, executed steps, GS tests,
scenarios, package audit results — that disagrees with the last run. When a
number legitimately moves, the failing step prints the exact `--update-facts
--observed ...` command to re-baseline it, deliberately.

**Not claimed:** a green line here means the named artifact exists and says what
it claims — not that a board can be built, an AI flown, or a release shipped.
Rows 3, 5, 7, 8, 9, 12 and 13 are open on hardware, model or operations, and
this repository is software-complete / hardware-gated by construction.
