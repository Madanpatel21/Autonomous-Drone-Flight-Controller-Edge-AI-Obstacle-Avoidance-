# 12_safety — safety analysis and the safety case

| What | Where |
|---|---|
| **Safety case** (6 claims, 6 explicit non-claims) — start here | [`00_project_control/04_safety/SAFETY_CASE.md`](../../00_project_control/04_safety/SAFETY_CASE.md) |
| FMEA — 22 analysed failure modes, project-local risk method | [`FAILURE_MODE_AND_EFFECTS_ANALYSIS.md`](../../00_project_control/04_safety/FAILURE_MODE_AND_EFFECTS_ANALYSIS.md) |
| Fault tree — top event, gates T1–T7, derived requirements SR-01..SR-10 | [`FAULT_TREE_ANALYSIS.md`](../../00_project_control/04_safety/FAULT_TREE_ANALYSIS.md) |
| Hazard log — 13 hazards + forward hazards | [`PRELIMINARY_HAZARD_ANALYSIS.md`](../../00_project_control/04_safety/PRELIMINARY_HAZARD_ANALYSIS.md) |
| The safety model that came out of it | `DEC-021` in [`00_project_control/DECISION_LOG.md`](../../00_project_control/DECISION_LOG.md) |
| Safety requirements as written | `SAF-*` in [`00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md`](../../00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md) |

Code: `flight_controller/failsafe/failsafe_sm.c` (tier set + `failsafe_action()`),
`app/app_main.c` (the action switch), `communication/command/cmd_gate.c`
(inbound authority gate).

## The two questions the code now answers separately

1. **Why** was control taken over? → the reported `fc_failsafe_t` (RC loss, IMU
   failure, battery, geofence, estimator).
2. **What can the vehicle still do?** → `fc_safety_action_t`, ranked by
   flyability, dominating tiers: `MOTOR_STOP` on IMU timeout or a deadline storm,
   RC loss → `RTL`, battery critical or the latched RTL band → `RTL`/`LAND`,
   geofence → `RTL`, estimator loss → `LAND`.

Collapsing those two into one field is the defect Phase 24 fixed: an operator was
shown `RC_LOSS` while the vehicle was cutting its motors.

## Honest limits — carried, not closed

Five fault-tree branches are **UNDETECTED** by design and recorded as such: IMU
bias (no second attitude reference), motor/ESC failure, power collapse, GNSS
wrong-fix, and real MCU timing. The safety case's non-claims section lists them
by name. Anything that claims flight readiness must consume that list first:
[`08_testing/FLIGHT_TEST_READINESS_REVIEW.md`](../../08_testing/FLIGHT_TEST_READINESS_REVIEW.md)
is currently a **task stub** — no review has been performed.
