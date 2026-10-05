# Test Generation Record — Phase 16

Response to `15_prompts/16_testing_verification/02_test_generation.prompt.md` (required
8-section format). Companion to [MASTER_VERIFICATION_PLAN.md](MASTER_VERIFICATION_PLAN.md).

Case format follows `08_testing/TEST_CASE_TEMPLATE.md` (ID, requirement, objective,
procedure, expected, pass/fail, evidence). All cases run on the SIM target (MinGW GCC)
inside `02_firmware/tests/test_main.c`; no framework, deterministic CHECK macros.

---

## 1. Inputs / assumptions

- Existing suite (pre-Phase-16): 24 test functions / 525 checks covering mixer,
  attitude, battery thresholds, disarm, determinism, CRC, parameters, sensor health,
  validation, calibration, hover/rate/alt closed loops, DShot framing, interlocks,
  mission sequence, RC-loss RTL, noise robustness, GNSS tracking/gating, IMU-dropout
  regression. New cases must not duplicate these scenarios.
- Variation axes required by the prompt: environmental (noise, seed), temporal
  (duration, timeouts, recovery order), fault (type, injection time, combination),
  boundary (thresholds, arrival radius, gate edges).
- Subsystems not yet implemented (companion link, Edge-AI, ground station) cannot have
  executable cases; they are listed as deferred with their phase, not faked.

## 2. Work performed

Generated 8 scenario cases + recorded 3 defects they exposed (all fixed at the cause):

| Case | Requirement | Variation axis | Procedure / expected | Result |
|---|---|---|---|---|
| TEST-SAF-PRIORITY | SAF-001, SAF-031 | fault combination + temporal recovery | 3.39 V/cell latches (critical stays NONE); RC timeout elapses → RC_LOSS wins over battery-critical; RC restored → battery remains, latched → mode RTL | PASS |
| TEST-NAV-FENCE | NAV-004 | boundary (alt 12>10 m, horizontal 50>20 m) | breach → RTL; LAND not preempted by the fence it is resolving; DONE terminal even while violated | PASS |
| TEST-NAV-RCREC | SAF-001 | temporal (restore after loss) | RC back mid-RTL → stays RTL (no re-takeoff/WP resume) → LAND → DONE | PASS |
| TEST-CTRL-YAW | CTRL-001/002, CTRL-005 | control axis not previously covered | identity est + yaw 0.5 rad setpoint → motor counter-torque pairs differ; disarmed → exactly 0 | PASS |
| TEST-EST-ATT-YAW | EST-001 | environmental (rotation vs tilt) | 1 rad/s gyro-z for 1 s → q = (cos 0.5, 0, 0, sin 0.5) ±0.02 | PASS |
| TEST-EST-ALT-DRIFT | EST-002 | temporal (60 s) + boundary | stationary under noise: MEAN offset over last 10 s < 0.5 m, |vz| < 0.2 m/s | PASS (0.071 m) |
| TEST-EST-ALT-DIVERGENCE | EST-055 class | fault (teleport to 100 m) | estimate >50 m from baro → re-anchors, vz reset | PASS |
| TEST-SIM-IMUFAIL (Phase 15) | SAF-003 | fault (in-flight dropout) | no uncommanded climb (peak +0 m), actuators commanded 0, FC_FAILSAFE_IMU, mission ABORT | PASS |

**Defects found by generation (root-caused, fixed, regression-locked):**

1. **Failsafe states never de-escalated** (`failsafe_sm.c`): one-way `escalate()` left
   RC_LOSS active after RC recovery, masking any later battery failsafe — priority
   order was untestable. Rewritten as level-based evaluation (RC/IMU clear on sensor
   recovery; battery-critical latched with 3.1→3.4 V/cell hysteresis; SAF-031 RTL
   latch only for packs seen crossing the RTL band) — DEC-012.
2. **Permanent altitude offset from ground-reference capture** (`est_alt.c`): a single
   noisy 5 Pa sample (≈0.4 m) captured as the ground reference produced a −0.56 m
   offset in the 60 s stationary test. Ground reference is now the mean of the first
   1 s of at-rest samples; estimator stays live during the window (takeoff unaffected).
3. **vz random-walk under baro noise** (`est_alt.c`): K_VZ applied to zero-mean noise
   integrated as a random walk. Correction now gated by a 0.30 m deadband (baro noise
   stays inside; real drift exceeds it).
4. **Position-loss latch survived app re-init (Phase 17)** (`app_main.c`):
   `pos_ever_healthy` was a function-static inside `app_tick_1khz`, so a second app
   session in one process inherited the first session's latch and false-triggered
   `mission_land_now()` during takeoff. Moved into the scheduler struct reset by
   `app_init()`. Regression-locked by the Phase 17 app-level test, which runs two
   app sessions back-to-back.

## 3. Files created / modified

Modified: `02_firmware/tests/test_main.c` (+8 cases → **556 checks**),
`02_firmware/flight_controller/failsafe/failsafe_sm.c`,
`02_firmware/flight_controller/estimation/est_alt.c`.
Created: this record. Deferred-domain case stubs intentionally NOT created (would be
unexecutable): companion link (Phase 19), Edge-AI (Phase 17/18), ground station
(Phase 20).

## 4. Interfaces affected

- `failsafe_sm.h` semantics per DEC-012 (level vs latch) — any consumer of
  `failsafe_active()`/`failsafe_mode_request()` is affected; consumers are in
  `app_main.c` and the test suite only.
- No HAL, control, or navigation interface changes.

## 5. Verification performed

- Full suite: **556/556 PASS**, exit 0 (was 525/525 before generation; +31 checks).
- Regression runner: `bash 08_testing/regression_tests/run_regression.sh` → exit 0,
  all six scenario sequences match expectations, determinism diff = 0 bytes.
- Non-duplication audit performed against the pre-existing 24 cases (§1) — every new
  case varies an axis no prior case covered.

## 6. Acceptance criteria

| Requirement | Criterion | Status |
|---|---|---|
| TEST-003 | Unit tests exist for math, filters, PID, fusion, mixer, protocol, state machines | **MET (SIM)** — every listed domain has ≥1 case (§2 + pre-existing) |
| TEST-002 | H-gated cases marked and skipped with a clear message | **MET** — runner prints SKIP + reason |
| SAF-001 | Hierarchy verifiable: RC > IMU > battery > geofence > companion | **MET (SIM)** for RC/IMU/battery; geofence L2-only (no cross-domain L0 case); companion deferred Phase 19 |
| SAF-030/031 | Threshold + latch behavior | **MET (SIM)** — boundary 3.39/3.05 cases |
| EST-002 | 60 s stationary drift/offset | **MET (SIM)** — mean 0.071 m; instantaneous noise documented as sensor, not estimator |
| Scenario variety | Environmental/temporal/fault/boundary axes varied, no duplicates | **MET** (§2 table axes column) |

## 7. Risks / TBDs

1. Geofence×failsafe interaction has no combined L0 case (fence breach while battery
   critical) — both sides are tested separately; a combined case belongs with the
   Phase 17 safety analysis.
2. `failsafe_monitor()` is now vestigial (levels are re-derived at query time); kept
   for API stability. Phase 17 review should either remove it or make it the single
   evaluation point.
3. Timing constants (RC 500 ms, IMU 100 ms) are tested as logic, not as metrology;
   real-link timing is H-gated.
4. Coverage is requirement/check-based, not structural (no MC/DC tooling).
5. Edge-AI/companion cases are deferred, not written; TEST-003 coverage claim excludes
   them until their phases land.

## 8. Next dependency

- Phase 18 (`18_18_avoidance.prompt.md`): SAF-040 clamp cases on the avoidance
  consumer of `perception_get()` (avoid/brake inside FC-owned limits).
- Phase 24 (`17_safety/` topic): FMEA/FTA should consume the failure classes these
  cases encode and add the missing cross-domain combinations (fence×battery, IMU×RC).
- Phase 19: real UART link cases (frame parse, heartbeat timeout) feeding
  `perception_feed_ai` — the FC-side sink contract is frozen as of Phase 17.
- The regression runner (03_regression) is the enforcement mechanism: new cases must
  be added to `test_main.c` and are picked up automatically.

## 9. Phase 17 additions (perception fusion, 9 cases, 556 → 607 checks)

| Case | Axis | Asserts |
|---|---|---|
| TEST-PERC-FUSED | fusion nominal | FUSED mode, count, min range, advisory flag; flow loss does not gate |
| TEST-PERC-ROTATION | vehicle-state fusion | body→stability rotation by 90° yaw quaternion |
| TEST-PERC-CONF-GATE | invalid input | confidence 0.40 dropped at link sink (SAF-041) |
| TEST-PERC-STALE | temporal | 300 ms-old AI set excluded; ToF carries (TOF_ONLY); stale ≠ dead |
| TEST-PERC-DEATH | failure | 1.2 s silence → NONE, ai_healthy false, failsafe level unchanged (DEC-007) |
| TEST-PERC-CONFLICT | SAF-041 | AI 10 m vs ToF 4 m → 4.0 m published, conf 0.45, source AI\|TOF |
| TEST-PERC-TOF-SYNTH | sensor complementarity | ToF-only obstacle published; 0.01 m glitch rejected |
| TEST-PERC-INVALID | robustness | null/zero-time/over-count/empty sets ignored |
| TEST-AI-LOSS | app-level consumer | ai_loss in flight → TOF_ONLY degradation, no failsafe, mission continues (Phase 16 deferred consumer) |

## 10. Phase 18 additions (obstacle avoidance, 6 cases, 607 → 639 checks)

| Case | Axis | Asserts |
|---|---|---|
| TEST-AV-OFF | degraded | null/empty/out-of-envelope picture → AVOID_NONE, zero command, threat recorded |
| TEST-AV-SLOW | brake curve | 3 m → SLOW, vx = 1.0 m/s, no lateral when centered, vz = 0 |
| TEST-AV-STOP | hard stop | 1.5 m → STOP, vx exactly 0 |
| TEST-AV-LATERAL | lateral offset | left threat → steer right, \|vy\| ≤ 1.0 m/s |
| TEST-AV-CLAMP | SAF-040 adversarial | 1000 m/s obstacle velocity + 0.05 m range + 2-obstacle set → all components bounded |
| TEST-AV-APP | app-level progression | 30 s flight: NONE → SLOW → STOP, failsafe NONE, mission never aborts, final command bounded |

## 11. Phase 19 additions (communication & telemetry, 9 cases, 639 → 766 checks)

| Case | Axis | Asserts |
|---|---|---|
| TEST-COM-CRSF | protocol parse | CRSF CHANNELS → normalized channels; CRC corruption/truncation/null rejected |
| TEST-COM-SBUS | protocol fallback | SBUS 25-byte parse, flags bit2 → failsafe_active, bad end byte rejected |
| TEST-COM-ICD02 | integrity | encode/parse round-trip; CRC reject with safe drop; garbage-prefix resync; undersized buffer reject |
| TEST-COM-OBSTACLE-SET | schema codec | 2-detection round-trip incl. i16 cm quantization; truncated/over-count/undersized payloads rejected |
| TEST-COM-LINK | heartbeat/sequence | heartbeat → healthy + ai_state + model_id; OBSTACLE_SET → perception; HEALTH decode; unknown type counted; seq gap counted; corrupted frame not applied; timeout → unhealthy, no failsafe change |
| TEST-COM-TELEMETRY | versioning | status record round-trip, seq advance, SCHEMA_VER present; bad magic/version/CRC/truncation rejected |
| TEST-COM-MAVLINK | GS protocol | HEARTBEAT/SYS_STATUS round-trip + sizes + sysid/compid; voltage decode; CRC corruption → resync consumed=1; unknown id/length mismatch rejected |
| TEST-COM-FCSTATE | FC→comp | 44-byte payload, monotonic TX seq, mode/failsafe/perception/avoidance fields present |
| TEST-COM-BYTEPATH | app-level integration | 8 s run: >100 frames, 0 CRC errors, 0 unknown types, 0 seq gaps, 0 overflow, companion healthy, perception fed |

**Phase 19 defects found by generation (root-caused, fixed, regression-locked):**

1. `icd02_encode_obstacle_set(uint8_t cap)` truncated `ICD02_MAX_PAYLOAD` (256)
   to 0, so every OBSTACLE_SET encode returned 0 and no AI geometry ever reached
   the link — silently, with zero test failures before this phase. Parameter is
   now `size_t` with a documented warning.
2. CRSF unpacking used one channel per 3 bytes instead of the spec's two
   channels per 3 bytes and treated `len` as excluding the type byte, so every
   parsed channel was garbage (mid stick decoded as 0). Parser and test encoder
   now follow the spec layout.
3. `MAVLINK_HEADER_LEN` was 10 instead of 8: two junk bytes followed every
   frame's CRC, so corruption of the final CRC byte was accepted. Verified by
   the round-trip + corruption cases.
4. HEALTH dispatch required ≥8 payload bytes while the field layout is 7 (every
   HEALTH frame was dropped), and `companion_healthy()` measured the heartbeat
   window against the last *frame* instead of the last poll time, so a silent
   companion never expired. Both fixed; the timeout case now asserts the
   unhealthy transition and the absence of failsafe escalation.

## 12. Phase 20 additions (ground station, 14 host-side tests + 2 end-to-end checks)

| Case | Axis | Asserts |
|---|---|---|
| GS status record | round-trip / integrity | decode of a reference-encoded record; CRC error counted; schema-version mismatch rejected |
| GS MAVLink | round-trip / integrity | frame decode; CRC corruption → resync |
| GS stream | degraded link | mixed MAVLink + status + garbage decodes; garbage and CRC counters; truncation at every cut never crashes |
| GS warnings | health classification | failsafe, battery-RTL and companion-down produce the expected codes |
| GS obstacle set | AI payload decode | OBSTACLE_SET v1 → positions/velocity/confidence/class |
| GS commands | COM-004 | six arming-class commands rejected; mode whitelist; telemetry-rate bounds; parameter whitelist + ranges; mission cap/radius/altitude |
| GS end-to-end | firmware interface | nominal log decodes clean (exit 0); battery_low log raises CRITICAL failsafe + BATT_RTL and `--strict` exits 1 |

**Phase 20 defects found (root-caused, fixed, regression-locked):**

1. The GS MAVLink CRC used a 256-entry byte table built from the non-reflected
   0x1021 iteration, which is NOT bit-identical to the protocol's reference
   nibble accumulate — every real MAVLink frame failed the GS CRC check (0 of
   240 decoded). The fixture-only self-test passed, which is exactly why the
   end-to-end check against firmware output was required.
2. One sequence counter served both MAVLink frames and status records, so a
   clean log reported 79 phantom gaps. Separate counters now (DEC-016).
3. `run_regression.sh` resolved the ground-station directory after its own `cd`
   into the firmware tree, so the GS step could never locate the tool.

## 13. Phase 21 additions (vehicle model + horizontal control, 5 cases, 766 → 795 checks)

| Case | Axis | Asserts |
|---|---|---|
| TEST-SIM-LATERAL | closed-loop tracking | 6 m waypoint: peak excursion 6.00 m, no lateral divergence, peak altitude 1.50 m, full WP→RTL→LAND→DONE sequence |
| TEST-SIM-DRAG | vehicle model | lateral velocity with no thrust decays, never reverses, vehicle still travels |
| TEST-SIM-WIND | disturbance rejection | 1.5 m/s crosswind during HOLD: x = 0.00 m (P-only was 4.39 m), altitude held, no failsafe |
| TEST-POS-LIMITS | limits | absurd position setpoint and velocity override stay inside AF velocity/accel/tilt limits |
| TEST-AVOID-FLOWN | end-to-end avoidance | flown toward the obstacle: peak 6.06 m, minimum clearance 1.11 m (contact floor 0.30 m), ends RETREAT, no abort/failsafe |

**Phase 21 defects found (root-caused, fixed, regression-locked):**

1. Thrust counted twice in the new horizontal model (full fz vertically AND
   fz·sin(tilt) laterally) — a sustained tilt accelerated the vehicle upward
   like a rocket (9.2 m climb).
2. The near-hover accelerometer convention broke under tilt: a vehicle
   *holding* altitude at 25° reported +0.4..0.7 m/s of climb, so est_alt ran to
   8.8 m while the vehicle stayed at 0.7 m. Fixed with an up_z-normalised force
   term and an exact ground-rest reading.
3. Position hold fought the ground: full horizontal authority while still on
   the ground dragged the vehicle back down (true altitude 0.79 → 0.12 m).
   Fixed with an airborne altitude gate and a 60°/s tilt slew limit.
4. `NAV_HOLD` re-latched its target every tick, making position hold follow the
   vehicle — the wind test drifted 4.39 m while "holding". Now latched once.
5. Acceleration limit was clamped per axis while the limit is a magnitude.
6. Integral-free velocity control could not reject steady wind → anti-windup
   integral added (clamped, frozen under avoidance override).
7. `AVOID_STOP` alone drove the vehicle into the advancing obstacle (clearance
   0.30 m = contact floor) → bounded `AVOID_RETREAT` added (DEC-018).

## 14. Phase 22 additions (HIL link, ring and DShot decode, 8 cases, 795 → 1600 checks)

| Case | Axis | Asserts |
|---|---|---|
| TEST-HIL-FRAME | link codec | every field survives encode→decode, little-endian byte order, over-long frames refused (never truncated) |
| TEST-HIL-CORRUPT | link robustness | **every single-bit flip anywhere in a frame is rejected on CRC**; garbage prefix + corrupt frame + 3 good frames → 1 CRC error, 3 frames recovered, resync counted |
| TEST-HIL-TRUNC | framing | a stream cut at **every** byte offset yields no partial frame; byte-at-a-time delivery still yields exactly one frame |
| TEST-HIL-WRAP | RX ring | ~1000 frames pushed in randomly sized slices so the 4096-byte ring wraps repeatedly: no frame lost, duplicated or reordered, no CRC error, no overrun, indices always in range, deliberate sequence loss counted |
| TEST-HIL-SENSOR | sensor payload | all 7 sensor classes + per-sample timestamps round-trip bit-exactly; short payload refused; frame fits the link budget |
| TEST-HIL-AISET | AI payload | ICD-02 obstacle set round-trip; truncated payload refused; the full 16-detection maximum still fits the link |
| TEST-HIL-FAULTS | scenario parity | the HIL fault names are exactly the SIM scenario names, so a scenario replays across targets |
| TEST-DSHOT-CAPTURE | actuator bit stream | motor unit→throttle→frame→throttle→unit round-trips within one LSB; telemetry bit preserved; **every single-bit flip in the 16-bit frame fails the CRC-4** |

**Phase 22 defects found (root-caused, fixed, regression-locked):**

1. **RX ring resync corrupted its own memory.** The pop path compacted the live
   window with a single `memmove` over the *modular* length; once the ring wrapped,
   that wrote up to 4095 bytes past `buf[]`, smashing `head`/`frames` — a SIGSEGV in
   `hil_ring_push` on the first loopback run. Rewritten to be wrap-safe (modular
   indexing, per-frame linear assembly) and locked by TEST-HIL-WRAP.
2. **A false SOF pair could wedge the stream.** The ring trusted the length field
   before validating the version byte, so a byte pair that looked like `0xA5 0x5A`
   made it wait forever for a bogus 600-byte frame. Now the version is validated
   first (found by TEST-HIL-CORRUPT).
3. **Per-message-type sequence counters made a healthy stream look lossy** (283
   phantom gaps in 4000 ticks). One counter per link direction, as the protocol
   header specifies.
4. **The rig never flushed control-path replies**, so the FC's parameter read timed
   out and the post-run world query was never answered.
5. **Link-corruption injection corrupted the payload before encoding**, producing a
   frame with a *valid* CRC: the peer accepted a wrong sensor value instead of
   rejecting the frame. Corruption is now injected into the encoded wire bytes.
6. **Request/response waits used `Sleep(1)`**, which quantises to ~15.6 ms on
   Windows and made the flash exchange time out. All link waits are now
   `select()`-based.
7. **The rig deadlocked at start**: it only advanced the world on a tick, while the
   FC only sent a tick from its first control cycle. The rig now emits one sensor
   frame at t=0 and has an idle timeout, so a missing FC fails fast instead of
   hanging.

Executed host-HIL steps (repeatable, in the runner — see HIL_DESIGN.md §5): link
integrity and sensor-rate fidelity (nominal), DShot capture analysis, fault
injection latency and nav response (`rc_loss`, `imu_dropout`), link robustness under
injected corruption, and the watchdog interlock negative test.

## 15. Phase 23 additions (verification audit: 5 cases, 1600 → 2667 checks; GS 15 → 16 tests)

Phase 23 did not add a feature; it added the evidence that closes requirement
rows and names the rows that are honestly not met. Five cases were generated
because `VERIFICATION_REPORT.md` found baseline requirements whose only evidence
was "none".

| Case | Axis | Asserts |
|---|---|---|
| TEST_PARAM-POWERCYCLE | FW-005/SYS-006 | a stored parameter image survives a simulated power cycle; an image whose magic is destroyed and one whose CRC fails both fall back to the documented safe defaults (SAF-030 default restored, no stale value used); a re-store after a failed write recovers — i.e. a corrupt flash image can never arm the vehicle with out-of-envelope parameters |
| TEST_LOOP-TIMING | FW-002/SYS-001/SEN-005 | the whole 1 kHz tick executes in bounded time: mean 0.5 µs, p50 0, p99 3, p99.9 5, max 23 µs over 20 000 ticks, so the 900 µs deadline is never at risk from software execution time (MCU interrupt latency stays H-gated) |
| TEST_EST-HEALTH | EST-055/NAV-002 | after a healthy GNSS fix the position estimator flips to unhealthy at 1018 ms (declared 1.0 s timeout plus one 10 Hz sample age) and the mission descends in place — the estimator's own health state, not a watchdog, ends horizontal flight |
| TEST_ARM-INTERLOCK | CTRL-004/CTRL-005/SAF-005 | with RC dead the mission never leaves IDLE and all four motors stay STOP; restoring RC and the arm switch starts the mission; a non-zero throttle with the arm switch low cannot arm |
| TEST_NO-ALLOC-API | FW-004/FW-001 | the exercised control-path API surface contains no dynamic allocation and no backend header type leakage (the repository-wide check is `audit_requirements.py`) |
| GS EMERGENCY_STOP test | GS-003 | `EMERGENCY_STOP` is accepted unconditionally and maps to the FC's bounded LAND action, while the arming class and failsafe-override remain rejected — an emergency request is the one command a GS must always be able to send |

**Phase 23 defects found (root-caused, fixed, regression-locked):**

1. **The requirement audit's first run reported a false FW-004 violation.** The
   allocator scan matched the word "allocation" inside a *comment* explaining that
   the module is allocation-free. The checker now strips comments before
   scanning, and reports the file and line of any real site so a future hit is
   actionable instead of a guess.
2. **The audit was initially run by hand.** A traceability requirement that only
   holds when someone remembers to run a script decays immediately, so it is now
   wired into `run_regression.sh` as step 7 (audit), with the H-gated hardware
   steps renumbered to step 8. A new requirement without a status row now fails
   the regression.
3. **`EMERGENCY_STOP` did not exist in the GS command whitelist** — the only
   safety-relevant command the operator could not issue from the ground station.
   Added unconditionally (it cannot arm and cannot clear a failsafe), with the
   bounded LAND mapping named so the GS cannot invent its own action.

Two requirements remain honestly PARTIAL after this phase and are **not**
reinterpreted to look complete: EST-001 (the implementation is a complementary
filter with gyro-bias estimation, not the specified quaternion EKF, and there is
no magnetometer) and EST-003 (optical flow is a NOT_READY stub, so horizontal
position is GNSS-only). Both need a requirements decision, not another test.

## 16. Phase 24 additions (safety analysis: 5 cases, 2667 → 2741 checks; GS 16 → 18 tests)

| Case | What it locks down | Checks |
|---|---|---|
| `test_safety_action_feasibility` | The action is chosen by what the vehicle can still do, not by which failure latched first. Reports `FC_FAILSAFE_RC_LOSS` while `failsafe_action()` is `FC_ACTION_MOTOR_STOP`. | 40 |
| `test_safety_companion_not_a_failsafe` | Companion loss stays **outside** the failsafe tier set (DEC-007): the flight continues with no failsafe, no action. | 21 |
| `test_safety_deadline_storm_stops` | Driving `deadline_misses` past 100 forces `FC_ACTION_MOTOR_STOP`; before the threshold a flyable action is retained. | 22 |
| `test_command_gate_arming_class_refused` | The FC-side gate refuses the whole arming class: ARM, DISARM_OK, FORCE_ARM, CLEAR_ARM_GATE, OVERRIDE_FAILSAFE in 10 frames, all refused. Also bad magic, bad version, bad length, bad CRC, short frame, unknown type, unknown/NaN parameter, SET_PARAM below and above each envelope (BATT_RTL 3.3–3.6, BATT_LAND 3.0–3.4, GEOFENCE 10–100, MAX_ALT 1–60), and waypoint cap/length/fence violations. `EMERGENCY_STOP` is accepted. | 111 |
| `test_telemetry_action_on_the_wire` | Telemetry schema v2 round trip (53 B payload, `action` at offset 45), a v1-length frame refused, and `HEARTBEAT` reporting `MAV_STATE_CRITICAL` whenever a failsafe **or** an action is present. | 31 |

Ground station (host): `test_safety_action_on_the_wire` (all five action codes decode,
unknown codes surfaced as `UNKNOWN(n)` rather than silently NONE, IMU-failure-with-MOTOR-
STOP is distinguishable from IMU-failure-with-RTL) and
`test_status_record_v1_length_is_refused` (a 52-byte v1 frame is rejected, not
mis-decoded). GS total **18/18**.

Regression runner additions:

- `scenario imu_rc_loss (safety action dominates)` — IMU fails at 6 s, RC at 6.2 s.
  Asserts nav `TAKEOFF → HOLD → ABORT`, `deadline_misses=0`, and the live log line
  `safety: failsafe=1 action=4`. This is the cross-domain case the schema bump exists
  for: the reported failsafe is RC loss, the action is motor stop.
- `ground station telemetry schema v2 (action on the wire)` — decodes a real
  firmware-generated GS log from the same scenario (240 status records), asserts
  `action=MOTOR_STOP`, a `SAFETY_ACTION` CRITICAL line, non-zero records and
  `--strict` exit 1.

Suite total: **2741/2741**. Runner: **23 executed steps PASS + 2 H-gated SKIPs, exit 0**.

**Phase 24 defects found (root-caused, fixed, regression-locked):**

1. **The command gate reported ACCEPT after refusing a frame.** Its early returns
   (bad magic/version/length/CRC, short frame) never touched the observable
   last-result state, so a caller — and the test — could conclude a hostile frame had
   been accepted. Every rejection path now goes through a `refuse()` helper.
2. **The waypoint length check used a 5-byte stride while 8 bytes were then read**, so
   a short frame was read past its own end. `CMD_WP_BYTES` is now 8, and `CMD_MAX_FRAME`
   was raised 128 → 160 so a full 16-waypoint mission fits in one frame.
3. **An in-loop waypoint rejection was overwritten** by an unconditional
   `d = CMD_ACCEPT` placed after the loop, so a rejected batch was silently accepted.
   Replaced with an explicit `bad` flag guarding the final assignment.
4. **The battery RTL band latched but produced no action** (SAF-030 / SAF-031). The
   requirement had only ever been exercised through `failsafe_mode_request()`, which
   the application never called — dead behaviour that passed its test. The action model
   now routes the latched band to RTL/LAND, `failsafe_mode_request()` derives from
   `failsafe_action()`, and `imu_rc_loss` plus `battery_low` assert it on the live log.

Defect 4 is the transferable lesson, recorded here deliberately: **a requirement
tested only through a helper the application never calls is not a requirement.** It
is the reason every failsafe tier is now asserted on the live `safety:` log line and
through a scenario, not only in a unit test.

## 17. Phase 25 additions (manufacturing package: firmware suite unchanged 2741; package audit + 16 negative tests)

Phase 25 added **no firmware test cases** — it touched no control-path code. What it
added is the evidence that the release package is what it says it is, plus the
negative tests that prove that evidence can fail.

### New executable checks (regression step 8, three report lines)

| Check | What it locks down | Result |
|---|---|---|
| `parameter default file regeneration` | `dump_param_defaults.c` links the firmware's own `parameters.c` + `crc16.c`, calls `params_defaults()` and writes the exact bytes `params_store()` writes. The default path must not touch the flash backend (any HAL flash call is a hard failure, not a silent default). Regenerating must reproduce the digest-pinned blob. | PASS — reproduced byte-for-byte |
| `manufacturing release manifest audit` | 7 check groups over `RELEASE_MANIFEST.json`: structure, present items, gated items, facility gates, version bindings, parameter blob, controlled BOM | PASS — 7 groups |
| `release manifest audit negative tests` | The audit detects each defect class it claims to | PASS — 16/16 cases |

### The 16 negative cases (`12_manufacturing/tools/selftest_audit.py`)

Each case builds a throwaway fixture tree, mutates exactly one thing, and runs the
**real audit as a subprocess** — no private helper is called, so the shipped command
line is what is under test.

| Case | Defect class injected | Expected detection |
|---|---|---|
| control | none | untouched package audits clean |
| 1 | a digest-pinned artifact edited after the freeze | `digest mismatch` |
| 2 | a pinned file degraded back to the placeholder stub sentence | `placeholder stub` |
| 3 | a `present` item with no digest recorded | `missing 'sha256'` |
| 4 | an item with an invented status (`done`) | `has status 'done'` |
| 5 | an unpinned BOM row with no gate | `names no gate` |
| 6 | a BOM gate that resolves to nothing (`ZZ-999`) | `not a declared facility gate nor a requirement` |
| 7 | BOM revision left behind after a package revision change | `BOM revision` |
| 8 | a gated directory that is declared empty but is not | `was declared empty` |
| 9 | a facility gate whose blocker has since resolved | `no longer holds` |
| 10 | GS decoder left on an older telemetry schema than the FC | `they must agree` |
| 11 | released parameter blob tampered with | `trailing CRC does not cover` |
| 12 | gate pointing at a requirement that is already MET | `is no longer open` |
| 13 | re-baseline without a revision bump | `must bump the revision` |
| 14 | re-baseline *with* a bump | succeeds, and re-checking still passes |

### Defect found by the negative tests (the point of them)

**The re-baseline path produced a package that contradicted itself.** `--update-digests`
recomputed every pinned digest and bumped `package.revision`, but the BOM's first line
(`# Revision: MFG-A`) still named the old revision. Every individual check passed in
isolation, so nothing but an end-to-end case would have caught it: the fixture went
`update → MFG-B` successfully and then failed the audit on the BOM revision check. The
fix is at cause — the update rewrites the embedded revision line in every `present`
artifact that declares one and re-digests *after* the rewrite — and the final case now
asserts the full round trip (update, then re-check clean).

Recorded here because it is the same shape as Phase 24's defect 4: both were green
because nothing exercised the path being claimed. Testing an audit's helper functions
would not have found it; running the shipped CLI as a subprocess did.

Suite total: **2741/2741** (unchanged). Runner: **26 executed steps PASS + 2 H-gated
SKIPs**, exit 0.

## 18. Phase 26 additions (documentation audit: firmware suite unchanged 2741; 282 scaffolds + 19 task stubs marked; runner step 10)

**What is tested.** The documentation layer itself is now executable evidence:

1. **Marking (`mark_scaffolds.py`)** — two deliberately conservative detectors. A
   *scaffold* is only a file whose entire body is an H1 plus one known placeholder
   sentence; an *instruction-only stub* is a short file (2–6 lines) whose every
   remaining line opens with an imperative verb, with no table, code fence or link.
   Anything richer is left alone, so a thin factual document is never relabelled.
   Applied: 282 scaffolds + 19 task stubs rewritten with banners that keep the
   original scope text and state plainly that it is not a record; the 20th previous
   stub (`DOCUMENTATION_INDEX.md`) was already rewritten as real content and was
   skipped as such. The tool is idempotent (re-run: 0 changes).
2. **Auditing (`docs_audit.py`, regression step 10)** — six check groups: index rows
   declared `content` must not carry a banner and rows declared `scaffold`/`task
   stub` must carry one (both directions, so neither a mis-declared content file nor
   an undeclared one survives); every relative link in the four entry documents
   resolves; no markdown file anywhere is an unmarked placeholder or stub, and a
   banner with content appended underneath it fails; every *derived* fact is re-read
   from the file that owns it (telemetry schema 2 / payload 53 B from the firmware
   headers, cmd-gate version 1, parameter blob version 1 / CRC 63177, package `MFG-A`
   16 items = 8 present + 8 gated) with an FC↔GS decoder-version cross-check; every
   *observed* count the run supplied (suite 2741, executed steps, GS tests 18,
   scenarios 8, HIL host steps 6, package audit 7 checks, 16 negative cases) must
   match `PROJECT_FACTS.json`; and every number the walkthrough quotes must match the
   facts file.
3. **The negative path is demonstrated, not asserted** — with the facts file still
   at 26 executed steps and the run measuring 27, step 10 failed and printed
   `--update-facts --observed executed_steps=27`; after the deliberate re-baseline,
   the quote check then failed on the walkthrough's stale "26 executed steps" until
   the prose was updated. Both stale-number paths (file vs run, prose vs file) are
   enforced, and the failure message names the exact re-baseline command instead of
   inviting a blind edit.

**Evidence.** `python 11_documentation/tools/docs_audit.py` → `DOCS: PASS` (44
index rows, 44 links, 497 files, 0 unmarked, 9 derived facts, 11 observed facts, 10
quoted values). Full regression: **27 executed steps PASS + 2 H-gated SKIPs**, exit
0, with the new line `documentation audit (282 scaffold(s), 19 task stub(s))`.

**Honest limits.** The quote check only covers patterns the walkthrough actually
uses; a number phrased some other way is not caught. The observed counts are omitted
when the host runs a reduced environment (no gcc/python), because the facts file
describes the documented environment rather than every host. And the scan proves a
file is *marked*, not that its content is correct — correctness is what the other
six check groups and the CLI readers are for.

## 19. Phase 27 additions (V&V review gate: firmware suite unchanged 2741; 83 requirements classified; runner step 11)

**What is tested.** The phase's acceptance criterion — *all critical requirements
verified or explicitly blocked* — is now a predicate over a mechanical
classification instead of a reading of the review:

1. **Classification rule (the important one).** The outcome of a requirement is
   the **last** status token in its verification row, because the document's
   compound rows put the caveat last: `MET (SIM); rates on hardware OPEN (H)` is a
   blocked completion element, not a verified requirement. Counting the first
   token would report ten hardware-blocked critical items as verified — the exact
   optimism the phase exists to remove. Result today: 49 verified / 11 partial /
   18 blocked / 5 open.
2. **Evidence resolution.** Every backticked reference that looks like a file path
   must resolve, as written or by basename anywhere in the tree; code tokens
   (`malloc/calloc/realloc/free`) and glob shorthand (`{bin,json}`) are excluded by
   construction, so the check reports missing artifacts rather than prose styles.
   27 references checked, 0 broken.
3. **Critical gate.** The 23 loss-of-control requirements (SAF-*, SYS-001..004,
   FW-002/003, EST-001, CTRL-001/002/003/005, NAV-004, COM-004, GS-003) must each
   be verified or carry a written reason in the status cell — a bare
   `**OPEN (H)**` with no explanation fails the run. Result: 13 verified + 10
   recorded blockers = 23/23.
4. **Residual register.** Non-critical PARTIAL/OPEN/blocked items are printed as a
   24-row register, not dropped; they do not fail the gate, but a review that
   omitted them would be the dishonest kind of green.
5. **Review-document agreement.** The gate fails if `VNV_REVIEW.md` stops quoting
   the counts it just computed, so the narrative cannot drift from the
   classification.
6. **Negative tests.** `--selftest` runs this same CLI as a subprocess against
   seven fixture trees: control, written-reason cases (partial/blocked), a missing
   reason, a missing verification row, a critical verified item whose evidence path
   resolves to nothing, and a non-critical residual. 7/7.

**Evidence.** `python 08_testing/vnv_gate.py` → `VNV: PASS`; `--selftest` → 7/7;
full regression **29 executed steps PASS + 2 H-gated SKIPs**, exit 0, with the new
lines `V&V requirement review gate (unverified 34 (partial 11, blocked 18, open 5))`
and `V&V gate negative tests (7/7 cases behaved as specified)`.

**Honest limits.** The classification follows `VERIFICATION_REPORT.md`, the
authoritative status source by DEC-020 — the gate cannot notice a *wrong* status,
only an inconsistent, unexplained or unsupported one. `blocked` is a review
classification, not a schedule: SAF-005/COM-004/GS-002/GS-003 are blocked on a
command transport no current phase builds, and FW-007 (bootloader) is a product gap
rather than a facility gate. The five UNDETECTED fault-tree branches remain outside
the evidence boundary, and no certification is claimed anywhere.

## 20. Phase 28 additions (release manifest audit: firmware suite unchanged 2741; RC-1 pinned; runner step 12)

**What is tested.** The release manifest is a claim that it matches the artifacts,
and step 12 re-derives the whole claim from the tree:

1. **Source revision.** A tree digest (sha256 over sorted `relpath\0sha256` lines,
   excluding build dirs, `__pycache__` and the manifest itself) plus a file count
   identify the exact revision — this workspace has no VCS, so the digest is the
   revision identifier. Today: 588 files, digest pinned in RC-1.
2. **Item pins.** All 10 distributed items exist inside the repository and their
   sha256 matches; any edit without `--update-digests --release <new id>` fails the
   run, which is what makes the pin a freeze.
3. **Gated absence.** The 4 artifacts that must not exist yet (STM32 image, PCB
   fabrication data, AI model, per-unit QC records) are verified absent — a
   directory declared empty may hold only a README, the same rule as the
   manufacturing audit — and each gate must be declared in the manifest.
4. **Build targets/reproduction.** SIM and host-HIL are recorded as built with the
   reproduction commands; the STM32 target is recorded as gated with its
   expected-absent path, so "we could not build it" cannot silently become "built".
5. **Requirement row.** `OPS-001` must not read `OPEN` while a release candidate
   exists, and a `PARTIAL` row must name its gap. **This check found a real stale
   row on its first run**: the manifest existed while OPS-001 still read OPEN; the
   row was corrected at cause before the release was claimed.
6. **Negative tests.** `--selftest` runs the same CLI as a subprocess against 7
   fixture releases: control, tree-digest drift, a pinned item modified without a
   re-baseline, a missing present item, a gated artifact that appeared, OPS-001
   still OPEN, and PARTIAL with no named gap. 7/7.

**Evidence.** `python 13_release/tools/audit_release.py` → `RELEASE: PASS` (10
present items digest-matched, 4 gated items verified absent); `--selftest` → 7/7;
full regression **31 executed steps PASS + 2 H-gated SKIPs**, exit 0, with the new
lines `release candidate manifest audit (10 present item(s) digest-matched, 4 gated
item(s) verified absent)` and `release candidate manifest audit negative tests (7/7
cases behaved as specified)`.

**Honest limits.** The tree digest excludes build outputs: SIM/HIL binaries are
rebuilt evidence, not distributed artifacts, and the release ships source and pins,
not executables (the STM32 image does not exist to ship). Absence probes stop the
wrong artifact from appearing, but their future presence still requires a new
release id and, for anything flyable, hardware evidence that does not exist in this
workspace. And the digest identifies a revision without preserving history — no
VCS means no ancestry, by construction.

## 21. Phase 29 additions (final A-Z audit: marker register two-way; RC-2; runner step 13)

**What is tested.** The final sweep turned the repository's unresolved-work
markers into a register that cannot go stale in either direction:

1. **Two-way marker scan.** Every `TODO`/`FIXME`/`XXX`/`HACK`/`TBD` in markdown,
   C/H, Python and shell files (excluding `15_prompts/` process material and the
   register itself) must appear in `FINAL_AUDIT.md`'s findings block with its
   occurrence count and a disposition. An unregistered marker fails; a register
   entry whose count changed fails; a `RESIDUAL` with no gate/owner named fails;
   a `FIXED-IN-PHASE-29` entry with a non-zero count fails.
2. **Findings this sweep produced.** 47 live occurrences in 12 files: 39
   prose (documents that define or report the check quote the marker
   vocabulary), 8 residual with named gates (draft BOM ×4, H-gated serial
   capture ×1, sim-class model correlation ×2, deferred STM32 wiring ×1), plus
   13 occurrences **fixed at cause**: 10 Phase 03 `TBD` cells in the
   task/priority table (replaced with the rates and 900 µs deadline the code
   actually runs) and 3 stale `TBD`/`Open` evidence cells in the traceability
   matrix (a duplicate conflicting SYS-003 row was removed).
3. **Negative tests.** `--selftest` runs the same CLI against 6 fixture trees:
   control, unregistered marker, count mismatch, RESIDUAL without a gate,
   recorded fix with zero occurrences, stale register entry. 6/6.

**Evidence.** `python 08_testing/final_audit.py` → `FINAL AUDIT: PASS`;
`--selftest` → 6/6; release re-baselined to **RC-2** (new id, same behaviour:
Phase 29 changed documents and the audit, no control-path code — suite still
2741/2741); full regression **33 executed steps PASS + 2 H-gated SKIPs**, exit 0,
with the new lines `final audit (marker register) (11 marker occurrence(s) in 7
file(s))` and `final audit negative tests (6/6 cases behaved as specified)`.

**Honest limits.** A marker register only sees the vocabulary it scans: an
abandoned sentence with no keyword is invisible to it, which is why the
requirement, documentation, V&V and release audits remain the nets for those. And
dispositioning a marker does not fix it — the 7 residual entries are the same
hardware/model gaps the V&V register lists, now with a second, independent place
that fails if they are quietly deleted.
