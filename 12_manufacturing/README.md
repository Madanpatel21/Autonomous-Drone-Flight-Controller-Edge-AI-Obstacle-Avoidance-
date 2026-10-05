# 12_manufacturing — Manufacturing Release Package (revision MFG-A)

Phase 25 (see `15_prompts/25_25_manufacturing.prompt.md`). The requirement is
`MFG-001`: *"Release package: gerbers, BOM, pick&place, firmware binary,
parameter default file, docs; version-matched — manifest audit."*

## The honest shape of this package

There is no PCB layout in this workspace and no EDA tool to make one, so the
fabrication half of the package **cannot exist yet**. What Phase 25 delivers is
the half that does not depend on fabricating anything, plus a mechanism that keeps
the missing half honest:

* everything that *can* be produced on this host — controlled BOM, parameter
  default file, assembly sequence, inspection criteria, programming and
  production-test procedure, fabrication data contract — **exists and is
  digest-pinned**;
* everything that cannot — gerbers, drill, pick-and-place, stencil, assembly
  drawings, firmware image, release record — is an explicit **gated** item with a
  named requirement or facility blocker;
* every one of those claims is re-checked by a **machine audit** on every
  regression run, and the audit's probes fail if a declared blocker no longer
  holds. The gate list cannot rot into a permanent excuse.

## Contents

| Path | What it is | Status |
|---|---|---|
| [`RELEASE_MANIFEST.json`](RELEASE_MANIFEST.json) | the controlled package manifest: items, digests, gates, facility-gate probes | audited |
| [`MANUFACTURING_RELEASE_CHECKLIST.md`](MANUFACTURING_RELEASE_CHECKLIST.md) | 21-line release checklist with status + evidence/gate each | audited |
| [`bom/CONTROLLED_BOM.csv`](bom/CONTROLLED_BOM.csv) + [policy](bom/README.md) | controlled BOM, alternates policy | done |
| [`parameters/params_defaults.bin`](parameters/params_defaults.bin) + [`.json`](parameters/params_defaults.json) | parameter default file, generated from the firmware's own code | done |
| [`fabrication/FABRICATION_DATA_SPEC.md`](fabrication/FABRICATION_DATA_SPEC.md) | export contract for gerbers/drill/P&P/stencil/3D | spec |
| [`pcb_inspection/INSPECTION_CRITERIA.md`](pcb_inspection/INSPECTION_CRITERIA.md) | fabrication + assembly accept/reject criteria | spec |
| [`assembly/ASSEMBLY_SEQUENCE.md`](assembly/ASSEMBLY_SEQUENCE.md) | assembly order, IMU-zone rules, pre-power gate | spec |
| [`PROGRAMMING_AND_PRODUCTION_TEST.md`](PROGRAMMING_AND_PRODUCTION_TEST.md) | programming, serial scheme, PT-0..PT-7 | spec, PT-1 live |
| [`qc/`](qc/), [`gerbers/`](gerbers/), [`drill_files/`](drill_files/), [`release/`](release/) | record/artifact locations | empty on purpose |

## Reproduce

```bash
# 1. regenerate the parameter default file from the firmware's own defaults
gcc -std=c11 -I02_firmware/common -I02_firmware/common/utilities -I02_firmware/hal \
    -I02_firmware/flight_controller/configuration \
    -o /tmp/dump_params 12_manufacturing/tools/dump_param_defaults.c \
    02_firmware/flight_controller/configuration/parameters.c \
    02_firmware/common/utilities/crc16.c
/tmp/dump_params 12_manufacturing/parameters     # writes .bin + .json, must reproduce the pinned digests

# 2. audit the package (this is the MFG-001 acceptance evidence)
python 12_manufacturing/tools/audit_release_manifest.py
```

The regression runner executes both as its manufacturing step, so the package
cannot drift away from the firmware it claims to contain.

## Changing the package

Any edit to a pinned artifact fails the audit by design. To re-baseline:

```bash
python 12_manufacturing/tools/audit_release_manifest.py --update-digests --revision MFG-B
```

which recomputes digests, refuses a no-op or a missing revision bump, and prints
what changed. `--update-digests` is a release-engineering action: run it when you
mean to cut a revision, not to silence a failure.

## What "version-matched" means here (executable, not prose)

1. Every package item is digest-pinned to the manifest revision.
2. The BOM's in-file revision equals the package revision.
3. The released parameter blob's `version` equals the firmware's current
   `params_defaults()` version — checked against the source, live.
4. Telemetry schema version and payload length agree between the flight
   controller (`telemetry.h/.c`) and the ground station (`gs_protocol.py`).
5. The command-gate protocol version is recorded and re-read from `cmd_gate.h`.

A change in any of 2–5 without a package revision bump is a failed audit.
