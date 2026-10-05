# Perception Fusion — Phase 17 Evidence Record

Status: COMPLETE (SIM). All evidence below is SIM-only; nothing here is
hardware or flight evidence. Advisory boundary per DEC-007; fusion model per
DEC-013; requirements PERC-001..004 in REQUIREMENTS_DOMAINS.md.

## 1. Inputs / assumptions

- Companion AI reaches the FC as `fc_ai_obstacle_set_t` (ICD-02 OBSTACLE_SET
  payload class, ≤16 detections, FLU body frame, FC-domain timestamps). The
  Phase 19 UART link will parse frames into the same sink
  (`perception_feed_ai`); the FC-side contract does not change.
- SIM companion (hal_sim.c): deterministic virtual companion publishing one
  detection at 25 Hz for an obstacle starting 12 m ahead and closing at
  0.5 m/s (floored at 1.5 m so long batch runs stay bounded); ±0.05 m
  position noise. `ai_loss` stops the stream (companion dead), never the
  obstacle itself — the forward ToF keeps measuring it.
- SIM forward ToF (hal_tof_read id 1, previously a stub): obstacle range
  ±0.02 m, TF-Luna class 12 m max range.
- Optical flow remains a NOT_READY stub on SIM: it is an advisory egomotion
  source only and never gates the fusion mode (honest gap below).

## 2. Work performed

- New module `02_firmware/flight_controller/perception/perception_fusion.c`
  (+ header): 50 Hz fusion of AI sets, FC ToF, flow and vehicle state into a
  bounded 16-entry obstacle picture; entries carry confidence, source tag,
  body FLU position and stability-frame position (rotated by the estimated
  attitude quaternion, DEC-008 convention).
- Link-layer sink rules: zero timestamp, zero or oversized count → rejected;
  confidence < 0.5 dropped (ICD-02 / SAF-041 / AI-001).
- Validity windows: AI set stale >200 ms (ICD-02), companion dead >1 s
  (SYS-004), ToF stale >100 ms or outside [0.05, 12] m. Stale geometry is
  excluded, never kept "valid" (SAF-003 class rule).
- SAF-041 conflict rule (DEC-013): AI geometry in the ±20° forward cone that
  claims >1 m beyond the FC ToF range is clamped to the ToF range and its
  confidence is downgraded 50% (source becomes AI|TOF, visible to consumers).
- FC-only synthesis: ToF return ≤8 m in the forward cone publishes a
  TOF-sourced obstacle when AI covers nothing in the cone.
- Degraded modes: FUSED / AI_ONLY / TOF_ONLY / NONE. Companion death (>1 s)
  with no FC ranging forces NONE. Perception loss never escalates a failsafe
  (DEC-006/007) — asserted by test.
- App wiring (app_main.c, 50 Hz task): tof id 1 feed, companion AI read,
  flow feed, vehicle state, fusion tick, mode-change diagnostics line
  (`perception: mode=.. count=.. min_range=.. ai_ok=.. tof_ok=..`).
- HAL contract: `hal_companion_ai_read()` added to hal_interfaces.h (SIM
  implements; STM32 replaces the call site with the UART link in Phase 19).
- Virtual obstacle world in hal_sim.c shared by ToF id 1 and the AI stream.

## 3. Files created / modified

- Created: `02_firmware/flight_controller/perception/perception_fusion.{c,h}`,
  `03_edge_ai/perception/PERCEPTION_FUSION.md` (this record).
- Modified: `02_firmware/common/fc_types.h` (fc_ai_detection_t /
  fc_ai_obstacle_set_t), `02_firmware/hal/hal_interfaces.h`
  (hal_companion_ai_read), `02_firmware/hal/sim/hal_sim.c` (obstacle world,
  forward ToF, AI stream), `02_firmware/flight_controller/app/app_main.c`
  (perception task + pos-loss latch fix), `02_firmware/tests/test_main.c`
  (+9 cases), docs/regression files.

## 4. Interfaces affected

- `perception_fusion.h`: feed/tick/get API (Phase 18 avoidance consumes
  `perception_get()`; Phase 19 link consumes `perception_feed_ai`).
- `hal_interfaces.h`: +`hal_companion_ai_read` (SIM-only behavior described
  in ICD-01 terms; STM32 backend returns HAL_NOT_READY until the link lands).
- `fc_types.h`: +AI obstacle-set payload classes (ICD-02 wire mapping).
- No control-path timing change: fusion runs in the 50 Hz block; deadline
  misses remain 0 in every regression scenario.

## 5. Verification performed

- Unit + app tests (test_main.c, 9 new, 607/607 total, exit 0):
  - TEST-PERC-FUSED: fresh AI + fresh ToF agree → FUSED, count 1, min range
    6.0 m, advisory flag true; flow-unhealthy does not change the mode.
  - TEST-PERC-ROTATION: body detection rotated into the stability frame by a
    90° yaw quaternion (body x 6 m → world y 6 m).
  - TEST-PERC-CONF-GATE: confidence 0.40 detection dropped at the sink.
  - TEST-PERC-STALE: AI set 300 ms old excluded (mode TOF_ONLY, ToF carries);
    companion still "healthy" (<1 s) — stale ≠ dead, both tested.
  - TEST-PERC-DEATH: stream silent 1.2 s → NONE, ai_healthy false, failsafe
    level unchanged (no escalation from an advisory source).
  - TEST-PERC-CONFLICT (SAF-041): AI 10 m vs ToF 4 m → published 4.0 m,
    confidence 0.45, source AI|TOF.
  - TEST-PERC-TOF-SYNTH: ToF-only 5 m publishes (TOF_ONLY); a 0.01 m
    out-of-band glitch is rejected, previous picture retained.
  - TEST-PERC-INVALID: null / zero-time / count>16 / count=0 sets ignored.
  - TEST-AI-LOSS (app-level, the Phase 16 deferred consumer): 8 s flight →
    FUSED; `ai_loss` injected → ai_healthy false, mode TOF_ONLY (FC ToF
    carries), failsafe NONE, mission NOT aborted (HOLD/TAKEOFF continues).
- Regression runner (`run_regression.sh`): exit 0 — cmake configure, build,
  607/607, determinism double-run (seed 1234, 8000 ticks, diff = 0), 7
  scenario sequence checks with 0 deadline misses including the new
  `ai_loss` advisory-degradation check (nav TAKEOFF→HOLD AND perception
  degraded to mode=1).
- Defect found and fixed at cause during this phase (regression-locked):
  `pos_ever_healthy` in app_main.c was a function-static, so it survived
  `app_init()`; a second app run in one process inherited the previous run's
  latch and false-triggered `mission_land_now()` during takeoff. Moved into
  the scheduler struct (reset by app_init). Exercised by the new app-level
  test which runs two app sessions in one process.

## 6. Acceptance criteria

| Requirement | Criterion | Status |
|---|---|---|
| PERC-001 | Bounded picture, confidence/source/timestamp per entry | **MET (SIM)** — TEST-PERC-FUSED/ROTATION |
| PERC-002 | Stale/invalid exclusion, out-of-band rejection | **MET (SIM)** — TEST-PERC-STALE/CONF-GATE/TOF-SYNTH/INVALID |
| PERC-003 | Degraded modes; perception loss never failsafes | **MET (SIM)** — TEST-PERC-DEATH; TEST-AI-LOSS; ai_loss scenario |
| PERC-004 | FC sensor wins on conflict (SAF-041); ToF synthesis | **MET (SIM)** — TEST-PERC-CONFLICT/TOF-SYNTH |
| AI-004 | Failure modes classified and published as health state | **PARTIAL (SIM)** — stale/confidence/death classified in perception state; companion-side health message (0x03) is Phase 19 |
| SAF-040 | AI clamped to FC-owned limits | **PENDING Phase 18** — clamp applies to avoidance velocity commands; perception only downgrades confidence today |
| SYS-004 | Companion loss fallback ≤1 s | **PARTIAL (SIM)** — link-layer health flips ≤1 s (TEST-PERC-DEATH); UART heartbeat/timeout is Phase 19 |

## 7. Risks / TBDs

1. Optical flow is not modeled on SIM (NOT_READY) and is advisory-only in the
   fusion; flow-driven egomotion consistency checks are untested until the
   flow driver phase. Do not cite flow as covered.
2. Sim-class obstacle world: one obstacle, constant closing rate, no lateral
   dynamics — adequate for fusion validity testing, NOT for avoidance
   performance claims (Phase 18 must state the same limitation).
3. Companion timestamps assumed FC-clock-domain (documented in fc_types.h).
   The Phase 19 link must define the clock-sync/staleness handling for real
   UART frames; a real companion's capture latency is not simulated.
4. SAF-040 avoidance clamps are Phase 18 scope; DEC-007 boundary is enforced
   today only as "no consumer commands anything".

## 8. Next dependency

Phase 18 — Obstacle Avoidance (`15_prompts/18_18_avoidance.prompt.md`):
consume `perception_get()` (min range/bearing, velocities) inside
FC-owned bounded velocity/position limits (SAF-040), with avoid/brake
behavior tests on the same virtual obstacle world.
