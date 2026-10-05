# 13_release

The release record. Phase 28 created the first software release candidate; Phase 29 re-baselined it as RC-2 after the final audit, and the repository README
rebuild re-baselined it as RC-3 (no firmware behaviour change):

| Artifact | What it is |
|---|---|
| [`RELEASE_MANIFEST.json`](RELEASE_MANIFEST.json) | Versioned, digest-pinned release candidate (RC-3; supersedes RC-2 and RC-1): source tree digest, 10 present items, 4 gated artifacts, build targets and reproduction commands. Authoritative. |
| [`RELEASE_NOTES.md`](RELEASE_NOTES.md) | Human summary: what the release is and is not, evidence, reproduction, requirement status. |
| [`tools/audit_release.py`](tools/audit_release.py) | Re-derives every manifest claim from the tree (regression step 12); `--update-digests --release <id>` re-baselines deliberately; 7 fixture cases prove it can fail. |
| [`RELEASE_MANIFEST_TEMPLATE.md`](RELEASE_MANIFEST_TEMPLATE.md) | Pre-Phase-28 template, kept as the historical layout sketch. |
| [`firmware/`](firmware/README.md) · [`pcb/`](pcb/README.md) · [`ground_station/`](ground_station/README.md) · [`edge_ai/`](edge_ai/README.md) · [`documentation/`](documentation/README.md) | Per-domain release folders. **Gated**: no STM32 image, no PCB release, no AI model. The manifest is where their absence is asserted and probed. |
| [`release_notes/`](release_notes/README.md) | Historical notes location; the current notes are [`RELEASE_NOTES.md`](RELEASE_NOTES.md). |

The release is a **software + evidence release candidate**: SIM/host build,
tests, audits and documentation, all reproducible on a host. It is not a flight
release and claims no certification — the gates above are probed on every run,
so an artifact appearing without its fabrication/board evidence fails step 12
instead of shipping quietly.

Status of the requirement it serves: `OPS-001` **PARTIAL** (record exists and is
audited; operational release gated) — see
[`08_testing/VERIFICATION_REPORT.md`](../08_testing/VERIFICATION_REPORT.md).
