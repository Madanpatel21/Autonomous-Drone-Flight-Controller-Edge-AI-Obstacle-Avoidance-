# Obstacle Avoidance — Phase 18 Evidence Record

Status: COMPLETE (SIM). All evidence is SIM-only — not hardware or flight
evidence. Policy per DEC-014; requirements AVD-001..003 + SAF-040; consumes
the Phase 17 perception picture (PERCEPTION_FUSION.md).

## 1. Inputs / assumptions

- Perception picture (`perception_get()`, DEC-013): bounded 16-entry obstacle
  list with body-FLU positions, velocities, confidence, source tags. The
  avoidance module treats it as advisory input and adds no trust of its own.
- SIM obstacle world: one obstacle ahead starting 12 m, closing 0.5 m/s
  (floored 1.5 m) — drives SLOW at ~8 s and STOP at ~20 s in a 30 s run.
- The vehicle has NO lateral mission dynamics in SIM yet: avoidance publishes
  a velocity setpoint for the position/velocity controller, it does not yet
  steer the trajectory. This is the honest boundary of the phase; a full
  local map + constrained trajectory generator is deferred (listed in §7).

## 2. Work performed

- New module `02_firmware/flight_controller/navigation/obstacle_avoidance/
  obstacle_avoidance.{c,h}`: 50 Hz collision-risk estimation + bounded
  command generation.
- Threat selection: nearest obstacle within ±45° forward cone; threats
  beyond the 4 m brake envelope are recorded but cause no intervention.
- Forward speed: linear brake curve from 2.0 m/s at 4 m down to 0 at 2 m;
  hard stop (vx = 0 exactly) inside the stop range.
- Lateral offset: steer away from the threat bearing, magnitude linear in
  proximity, clamped to 1.0 m/s; zero for a centered threat.
- Vertical command is identically zero (altitude stays mission-owned, DEC-011
  failsafe axis).
- SAF-040 enforcement: the clamp to FC-owned constants is the LAST step of
  the publish path — no upstream value (obstacle velocity, corrupted range,
  count) can produce an out-of-limit command.
- Degraded behavior: null/invalid/empty picture or cone-empty picture →
  AVOID_NONE with an all-zero command; avoidance never escalates a failsafe.
- App wiring: `avoidance_update()` in the 50 Hz block after perception tick +
  mode-change diagnostics lines (`avoidance: mode=... threat=... v=(...)`).

## 3. Files created / modified

- Created: `obstacle_avoidance.{c,h}`, this record.
- Modified: `02_firmware/flight_controller/app/app_main.c` (task + diagnostics),
  `02_firmware/tests/test_main.c` (+6 cases), DECISION_LOG (DEC-014),
  REQUIREMENTS_DOMAINS (AVD domain), TRACEABILITY_MATRIX, MASTER_VERIFICATION_PLAN,
  TEST_GENERATION, PROJECT_STATUS, REGRESSION.md.

## 4. Interfaces affected

- New: `avoidance_init/update/get` (Phase 21 tuner and the velocity-controller
  integration consume `avoidance_get()`; FC-owned limit constants exported).
- Perception interface unchanged (read-only consumer).
- No HAL, mission, or control-loop changes; control-path timing unaffected
  (deadline misses 0 in every regression scenario).

## 5. Verification performed

- Unit + app tests (test_main.c, 6 new cases, 639/639 total, exit 0):
  - TEST-AV-OFF: null picture / confidence-gated empty picture / threat
    beyond brake envelope → AVOID_NONE, zero command, threat still recorded.
  - TEST-AV-SLOW: 3 m threat → SLOW, vx = 1.0 m/s (curve midpoint), no
    lateral for a centered threat, vz = 0.
  - TEST-AV-STOP: 1.5 m threat → STOP with vx exactly 0.
  - TEST-AV-LATERAL: threat at +28.6° (left) → negative vy (steer right),
    bounded by 1.0 m/s.
  - TEST-AV-CLAMP (SAF-040): adversarial inputs — 0.05 m range, ±1.57 rad
    bearing, obstacle velocity 1000 m/s, 2-obstacle set — every published
    component within FC limits; nearest obstacle selected.
  - TEST-AV-APP: 30 s app run — avoidance progresses NONE → SLOW → STOP as
    the obstacle closes; failsafe NONE; mission never aborts; final command
    bounded (SAF-040 at end of flight).
- Regression runner: exit 0 — build, 639/639, determinism double-run diff = 0,
  7 scenario sequence checks with deadline_misses=0 (incl. ai_loss advisory
  degradation), 2 H-gated SKIPs.

## 6. Acceptance criteria

| Requirement | Criterion | Status |
|---|---|---|
| AVD-001 | Cone threat + brake curve + hard stop | **MET (SIM)** — TEST-AV-SLOW/STOP/LATERAL |
| AVD-002 | Commands clamped to FC limits as last step (SAF-040) | **MET (SIM)** — TEST-AV-CLAMP adversarial inputs |
| AVD-003 | Deterministic degraded behavior; no failsafe escalation | **MET (SIM)** — TEST-AV-OFF, TEST-AV-APP |
| Phase-18 prompt: "collision-risk estimation, local obstacle map, constrained trajectory generation" | risk estimation **MET**; local map + trajectory generator **DEFERRED** (§7) | PARTIAL |

## 7. Risks / TBDs

1. **No lateral vehicle dynamics in SIM**: the avoidance velocity setpoint is
   published and bounded but not yet flown by a position/velocity controller —
   behavioral avoidance (the vehicle actually steering around the obstacle)
   is unverified until the horizontal dynamics/controller phase. The mode
   progression and bounds are what is tested today.
   **RESOLVED Phase 21**: the horizontal dynamics and position controller landed
   (SIMULATION/SIM_TUNING_RECORD). The setpoint is now flown end-to-end —
   TEST-AVOID-FLOWN measures peak travel 6.06 m with a minimum obstacle
   clearance of 1.11 m against the advancing obstacle. Phase 21 also found that
   STOP alone was insufficient and added the bounded AVOID_RETREAT mode
   (DEC-018); see 07_simulation/SIMULATION_VALIDATION.md.
2. Single-threat policy (nearest in cone); no local occupancy map, no
   open-cone selection, no trajectory generator. Multi-obstacle scenes are
   reduced to the nearest cone threat — acceptable for the current sim world
   (one obstacle), insufficient for cluttered environments (Phase 21+).
3. Sensor-fusion quality bounds inherit the Phase 17 sim-class models; range
   accuracy vs real ToF/AI is H-gated.
4. Avoidance limits (2.0/1.0 m/s, 4.0/2.0 m) are engineering placeholders
   until Phase 21 tuning and are FC-owned constants, not parameters yet.

## 8. Next dependency

Phase 19 — Communication & Telemetry (`15_prompts/19_19_communication.prompt.md`):
UART framed link (DEC-003), heartbeat/timeout, `perception_feed_ai` gets its
real producer, companion health (0x03) classified into the perception state.
