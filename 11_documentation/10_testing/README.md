# 10_testing — verification and test documentation

| What | Where |
|---|---|
| Verification levels, requirement→evidence map, current run | [`08_testing/MASTER_VERIFICATION_PLAN.md`](../../08_testing/MASTER_VERIFICATION_PLAN.md) |
| **Requirement status, one row per baseline ID (83)** | [`08_testing/VERIFICATION_REPORT.md`](../../08_testing/VERIFICATION_REPORT.md) |
| How tests were generated, case by case, with the defects each found | [`08_testing/TEST_GENERATION.md`](../../08_testing/TEST_GENERATION.md) |
| The regression runner: every step, what it asserts, why | [`08_testing/regression_tests/REGRESSION.md`](../../08_testing/regression_tests/REGRESSION.md) |
| Traceability: requirement → design → implementation → test | [`00_project_control/01_governance/TRACEABILITY_MATRIX.md`](../../00_project_control/01_governance/TRACEABILITY_MATRIX.md) |
| Case template for a new test | [`TEST_CASE_TEMPLATE.md`](../../08_testing/TEST_CASE_TEMPLATE.md) |

Run it:

```bash
bash 08_testing/regression_tests/run_regression.sh    # exit 0 = every executed step passed
python 08_testing/audit_requirements.py               # requirement status + architecture audit
```

## How this area keeps itself honest

Three mechanisms, all executable, none relying on review:

1. `audit_requirements.py` fails the run if any of the 83 baseline IDs lacks a
   status row or a traceability row, and re-checks `FW-001` (no HAL backend
   include in algorithm code) and `FW-004` (no allocation in the control path).
2. The runner reports hardware-gated steps as `SKIP` with the missing
   prerequisite — they never count as passes (`TEST-002`).
3. Each audit ships **negative tests** proving it can fail
   (`12_manufacturing/tools/selftest_audit.py`; `DEC-023`). An audit that has never
   failed is a document.

`TEST-001..004` are **MET**; `HIL-1..6` and L5 flight are **OPEN (H)** — see
[`../02_hardware/README.md`](../02_hardware/README.md) for why.
