# Release Notes — RC-6

**Release:** RC-6, software + evidence release candidate, 2026-10-05.
Supersedes RC-5: the README safety-notice lead sentence was removed at the owner's
request; every factual claim it carried still lives in the project status table
(`Certification | None claimed`), the requirement rows and the gate list, so nothing
about the project's verified state changed. No firmware behaviour changed. (RC-5
superseded RC-4: the README identity changed (project name now uses `&`), the
hardware-status badge was removed and an owner-supplied hero image is tracked at
`assets/autonomous-drone-hero.png`; the tree is 591 files. No firmware behaviour
changed. (RC-4 superseded RC-3: the project license was set to MIT and the placeholder `LICENSE`
file was replaced with the canonical text (README license section updated); no
firmware behaviour changed. (RC-3 superseded RC-2: the repository README — a
digest-pinned item — was rebuilt as the project entry point with GitHub-native
Mermaid architecture diagrams, an evidence-based status table and executed
commands as Quick Start.)
(RC-2 superseded RC-1: the final audit in Phase 29 fixed two stale documents and
added its own executable gate.)
**Requirement:** OPS-001.
**Manifest (authoritative):** [`RELEASE_MANIFEST.json`](RELEASE_MANIFEST.json) —
digest-pinned, audited every run by [`tools/audit_release.py`](tools/audit_release.py)
(regression step 12). These notes are the human summary; the manifest is the pin.

## What this release is

A reproducible, audited **software release candidate** built entirely on this
host: the shared flight-control application (SIM and host-HIL targets), the
ground-station tools, the test suites and audits, and the documentation set.
Every claim below was produced by a command in this repository, and the tree
digest in the manifest identifies the exact revision those commands ran against
(this workspace has no version control; the digest is the revision identifier).

## What this release is not

- **Not a flight release.** No STM32 firmware image exists — nothing has ever
  been compiled for the production target (`STM32_TOOLCHAIN` gate).
- **Not a hardware release.** No PCB layout, fabrication data or BOM pin set
  (`EDA_TOOLCHAIN`), no board, no calibration, no per-unit record (`BOARD`).
- **Not an AI release.** No dataset, model or latency measurement exists; the
  companion interface is validated only through the `ai_loss` degradation path
  (`AI_MODEL` gate). No latency number is fabricated.
- **No certification** is claimed, implied or sought by this document. SIM and
  host-HIL evidence is never hardware evidence.

## Contents (10 pinned items + 4 gated)

| Item | Path |
|---|---|
| Project README | [`README.md`](../README.md) |
| Documentation index | [`11_documentation/DOCUMENTATION_INDEX.md`](../11_documentation/DOCUMENTATION_INDEX.md) |
| New-engineer walkthrough | [`11_documentation/NEW_ENGINEER_WALKTHROUGH.md`](../11_documentation/NEW_ENGINEER_WALKTHROUGH.md) |
| Project facts | [`11_documentation/PROJECT_FACTS.json`](../11_documentation/PROJECT_FACTS.json) |
| Verification report | [`08_testing/VERIFICATION_REPORT.md`](../08_testing/VERIFICATION_REPORT.md) |
| V&V review | [`08_testing/VNV_REVIEW.md`](../08_testing/VNV_REVIEW.md) |
| Regression entry point | [`08_testing/regression_tests/run_regression.sh`](../08_testing/regression_tests/run_regression.sh) |
| Manufacturing package manifest (MFG-A) | [`12_manufacturing/RELEASE_MANIFEST.json`](../12_manufacturing/RELEASE_MANIFEST.json) |
| Parameter default file | [`12_manufacturing/parameters/params_defaults.bin`](../12_manufacturing/parameters/params_defaults.bin) |
| Release documentation checklist | [`11_documentation/RELEASE_DOCUMENTATION_CHECKLIST.md`](../11_documentation/RELEASE_DOCUMENTATION_CHECKLIST.md) |

Gated (verified absent by the audit): STM32 firmware image, PCB/fabrication
release, AI model and model card, per-unit QC records.

## Evidence

- Firmware unit + integration suite: **2741/2741 passed**.
- Regression: **33 executed steps PASS + 2 H-gated SKIPs**, exit 0 (script
  status captured directly).
- V&V review gate: 83 requirements classified, 23 critical all verified or
  explicitly blocked with written blockers, 0 failed review items; 7/7 negative
  cases.
- Documentation audit: 0 unmarked placeholders; derived and observed counts
  agree with `PROJECT_FACTS.json`.
- Manufacturing package: `MFG-A`, 16 items (8 present digest-pinned, 8 gated),
  audit 7 check groups + 16/16 negative cases.
- Release manifest: 10/10 present items digest-matched, 4/4 gated artifacts
  verified absent, 7/7 negative cases.
- Final A-Z audit: 47 marker occurrences in 12 files, every one dispositioned
  (39 prose, 8 residual with named gates); 6/6 negative cases.

## Reproduce

```bash
cd 02_firmware && cmake -S . -B build && cmake --build build -j 4
cmake -S . -B build-hil -DFC_TARGET=hil && cmake --build build-hil -j 4
bash 08_testing/regression_tests/run_regression.sh
python 13_release/tools/audit_release.py          # manifest matches artifacts?
```

The last command fails the moment a pinned artifact changes without a release
re-baseline (`--update-digests --release <new id>` is the deliberate act), so a
stale release is a red build rather than a stale document.

## Requirement status

`OPS-001` is **PARTIAL**: the software/evidence release record exists and is
audited. An *operational* release — a vehicle that can be flown — remains gated
on the board and toolchain, and the residual-risk register in the V&V review
lists every remaining item.
