# Navigation & Mission Validation — Phase 14 (SIM)

Status: SM/altitude COMPLETE (SIM); horizontal localization PARTIAL (needs virtual GNSS — Phase 15). Evidence: `fc_tests` 102/102, `fc_sim` end-to-end flight log.

## Implemented

- `mission_sm.c`: states IDLE → TAKEOFF → WAYPOINT(×n) → RTL → LAND → DONE; waypoint list (≤16) with arrival radius; geofence (radius + ceiling) → RTL (NAV-004); RC-loss → RTL (SAF-001); deterministic 50 Hz tick with injected position (testable).
- `ctrl_cascade.c` altitude hold: alt → vz PID chain → collective offset around hover; 100 Hz; NaN-guarded setpoint (Phase 14).
- `est_alt.c` rewritten: accel predict (gravity-compensated) + baro complementary correction with bias state (EST-002).
- SIM: vertical dynamics (thrust/gravity → vz/z), ISA-consistent virtual barometer; sim hooks `hal_sim_get_vertical/set_vertical`.
- App wiring: RC read/keepalive, mission tick @50 Hz, altitude SP application, auto-start on arm switch, state-change log lines.

## Measured results (deterministic SIM, `fc_tests`)

| Test | Scenario | Result |
|---|---|---|
| TEST-NAV-SEQ | 2 waypoints → RTL → LAND → DONE | all transitions in order, wp index correct — PASS |
| TEST-NAV-RCLOSS | RC loss at 15 m from home | immediate RTL; RTL completes → LAND — PASS |
| TEST-CTRL-ALT | altitude hold 1.5 m, 12 s, full loop (physics+estimator+control) | true 1.50 m, est 1.50 m, vz ≈ 0, motors bounded — PASS |

End-to-end sim run (8 s): `nav: TAKEOFF alt_sp=1.50` → `nav: HOLD alt_sp=1.50 alt=1.26` (climbing to setpoint; sim-class hover base).

## Honest limitations

- Horizontal position/velocity estimator (GNSS+flow) NOT implemented — `nav_xy_t` is injected in tests and stubbed (0,0) in the app pending virtual GNSS (Phase 15). Waypoint navigation is therefore validated at state-machine/logic level, not as closed-loop flight.
- Altitude hold gains are sim-tuned placeholders; hover base 0.5 is sim-class (real vehicle requires thrust calibration, H-gated).
- Baro model is ideal ISA (no noise/bias/drift) — Phase 15 adds sensor error models.
