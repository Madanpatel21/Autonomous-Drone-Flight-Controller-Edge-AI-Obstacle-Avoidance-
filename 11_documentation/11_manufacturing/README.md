# 11_manufacturing — manufacturing and production documentation

The package itself lives in [`12_manufacturing/`](../../12_manufacturing/README.md);
this page is the index into it.

| What | Where |
|---|---|
| Package manifest: every item, its digest, its gate | [`12_manufacturing/RELEASE_MANIFEST.json`](../../12_manufacturing/RELEASE_MANIFEST.json) |
| Release checklist: 21 lines, each with evidence or a gate | [`12_manufacturing/MANUFACTURING_RELEASE_CHECKLIST.md`](../../12_manufacturing/MANUFACTURING_RELEASE_CHECKLIST.md) |
| Controlled BOM (16 rows) and the rule for unpinned rows | [`bom/CONTROLLED_BOM.csv`](../../12_manufacturing/bom/CONTROLLED_BOM.csv), [`bom/README.md`](../../12_manufacturing/bom/README.md) |
| Parameter default file (**generated from the firmware's own code**) | [`parameters/params_defaults.bin`](../../12_manufacturing/parameters/params_defaults.bin) |
| Fabrication export contract (gerbers, drill, P&P, stencil) | [`fabrication/FABRICATION_DATA_SPEC.md`](../../12_manufacturing/fabrication/FABRICATION_DATA_SPEC.md) |
| Inspection criteria (fabrication F1–F10, assembly A1–A9) | [`pcb_inspection/INSPECTION_CRITERIA.md`](../../12_manufacturing/pcb_inspection/INSPECTION_CRITERIA.md) |
| Programming, serial numbers, production test stations PT-0..PT-7 | [`PROGRAMMING_AND_PRODUCTION_TEST.md`](../../12_manufacturing/PROGRAMMING_AND_PRODUCTION_TEST.md) |
| Why the package is built this way | `DEC-022`, `DEC-023` in [`00_project_control/DECISION_LOG.md`](../../00_project_control/DECISION_LOG.md) |

## How to check the package

```bash
python 12_manufacturing/tools/audit_release_manifest.py    # 7 check groups
python 12_manufacturing/tools/selftest_audit.py            # 16 negative cases
```

Both run inside the regression as step 8.

## Honest status — `MFG-001` is PARTIAL

Everything that can be produced on a host exists, is digest-pinned, and is
re-checked every run. Everything that requires fabricating something **does not
exist**: gerbers, drill, pick-and-place, stencil, assembly drawings, 3D model,
DRC evidence, the STM32 image and every per-unit QC record. Those items are gated
on `EDA_TOOLCHAIN`, `HW-010`, `STM32_TOOLCHAIN` and `BOARD`, and the gates carry
**executable probes**: the day a layout or a toolchain appears, the audit fails
until the corresponding artifact is produced. The gated directories are asserted
empty on every run, so "gated" cannot quietly become "present but unverified".

To change anything under `12_manufacturing/` you must re-baseline deliberately:

```bash
python 12_manufacturing/tools/audit_release_manifest.py --update-digests --revision MFG-B
```
