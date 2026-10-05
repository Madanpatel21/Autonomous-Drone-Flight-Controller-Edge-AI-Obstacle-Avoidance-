# 06_navigation — mission and navigation documentation

| What | Where |
|---|---|
| Navigation validation: mission sequence, guards, geofence, RTL | [`04_navigation/NAVIGATION_VALIDATION.md`](../../04_navigation/NAVIGATION_VALIDATION.md) |
| Scenario expectations the regression asserts | [`08_testing/regression_tests/REGRESSION.md`](../../08_testing/regression_tests/REGRESSION.md) §2 step 5 |
| Avoidance policy that can override forward motion | [`../07_edge_ai/README.md`](../07_edge_ai/README.md) and `DEC-014` |
| Safety actions that can preempt the mission | `DEC-011`, `DEC-021` in [`00_project_control/DECISION_LOG.md`](../../00_project_control/DECISION_LOG.md) |

Code: `flight_controller/navigation/{mission_sm.c,waypoint.c,geofence.c}`.

## Mission semantics worth knowing

* The mission state machine owns the *sequence*; the failsafe owns *authority*.
  `mission_request_rtl()` and `mission_land_now()` are the two entry points the
  app calls when a safety action demands a behaviour change. `mission_request_rtl()`
  is idempotent and never preempts `LAND`/`DONE`/`ABORT` (a safety-driven RTL must
  not turn a committed landing back into flight); `mission_land_now()` does force a
  descent, which is why it is only reachable from a safety action.
* Requirements `NAV-001..004` are **MET (SIM)** with asserted scenario sequences
  (`imu_dropout` must reach `HOLD → ABORT`; `rc_loss` must reach
  `HOLD → LAND|RTL → DONE`; geofence breach must reach RTL with LAND excluded
  inside the fence).
* Altitude is a safety-owned quantity: avoidance is forbidden from commanding
  vertical motion (`DEC-014`).
