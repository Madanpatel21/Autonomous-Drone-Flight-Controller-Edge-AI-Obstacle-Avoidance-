# Master Verification Plan — Phase 16

Response to `15_prompts/16_testing_verification/01_test_strategy.prompt.md` (required
8-section format). Supersedes the placeholder text. The companion execution artifact is
[regression_tests/run_regression.sh](regression_tests/run_regression.sh); the generated
test inventory is in [TEST_GENERATION.md](TEST_GENERATION.md).

**Evidence status: everything marked SIM/U is executed and passing today (2741/2741,
regression runner exit 0 including the ground-station, host-HIL and requirement-audit
steps). Everything marked H is designed and NOT executed — no board, no rig. H items are
never counted as coverage.** (Counts updated Phase 26: 556 → 607 → 639 → 766 → 795 →
1600 → 2667 → 2741 firmware checks, 6 → 8 asserted scenarios, 15 → 18 host-side GS tests,
3 end-to-end decodes, 6 executed host-HIL steps, 1 requirement/architecture audit
step, 3 manufacturing steps and 1 documentation audit step; 20 → 23 → 26 → 27
executed runner steps. Per-requirement status for all 83 baseline IDs is in
[VERIFICATION_REPORT.md](VERIFICATION_REPORT.md); two requirements are recorded PARTIAL
because the implementation deviates from the requirement wording — EST-001 (no EKF/mag)
and EST-003 (no optical flow).)

---

## 1. Inputs / assumptions

- Requirement baseline: `00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md`
  (SYS/CTRL/EST/SEN/NAV/SAF/SIM/TEST/HW/AI/FW/COM domains).
- Verification-method legend: U = unit/integration test (SIM), S = simulation scenario,
  A = analysis, R = review, H = hardware (bench/HIL/flight) — H evidence requires real
  execution on real hardware and does not exist yet anywhere in this project.
- Test levels (entry/exit criteria below): L0 unit, L1 integration/app, L2 scenario
  (deterministic sim), L3 HIL, L4 bench/ground, L5 controlled flight.
- Single firmware application, dual HAL backends (DEC-001): the same app runs on sim
  and STM32, so L0–L2 run host-side now and L3–L5 re-run the same suites on hardware.

## 2. Work performed

1. Defined the five verification levels with entry/exit criteria and mapped every
   requirement domain to a level + method + current evidence status (table in §6).
2. Implemented the safety-critical regression runner
   (`regression_tests/run_regression.sh`): configure → build → unit suite →
   determinism double-run → six expected-sequence fault scenarios → H-gated skips
   with explicit reasons. Exit 0 only when every executed step passes.
3. Generated 8 new scenario test cases (TEST-GEN, see TEST_GENERATION.md) closing the
   previous coverage gaps: failsafe priority/latch, geofence boundary, RC recovery,
   yaw chain, yaw integration, 60 s altitude drift, divergence re-anchor, plus the
   Phase 15 SAF-003 regression.
4. Root-caused and fixed three defects the new tests exposed (see TEST_GENERATION.md
   §2): failsafe states never de-escalated; ground-reference capture noise became a
   permanent −0.56 m altitude offset; vz state random-walked under baro noise.

## 3. Files created / modified

Created: this plan, `08_testing/TEST_GENERATION.md`,
`08_testing/regression_tests/run_regression.sh`.
Modified: `02_firmware/tests/test_main.c` (+8 test cases in Phase 16; the suite now
holds 2741 checks after Phases 17–24),
`flight_controller/failsafe/failsafe_sm.c` (level/latch semantics),
`flight_controller/estimation/est_alt.c` (ground-reference averaging, vz deadband).

## 4. Interfaces affected

- `failsafe_sm.h`: semantic contract change (documented in DEC-012): RC/IMU failsafe
  states are level-triggered (clear on sensor recovery); battery-critical is latched
  with 3.1→3.4 V/cell hysteresis; SAF-031 RTL latch applies only to packs seen
  crossing the RTL band. `failsafe_monitor()` is retained for API stability; state is
  re-derived at query time from levels.
- `est_alt.h`: no API change (ground reference now averaged over the first second at
  rest; internal only).
- No HAL/app interface changes.

## 5. Verification performed

- `bash 08_testing/regression_tests/run_regression.sh` → **exit 0** (script status read
  directly, never through a pipe). At Phase 16 this was configure PASS, build PASS,
  556/556 unit+integration, determinism diff = 0 bytes, six scenario sequences matching
  with `deadline_misses=0`, and two H-gated SKIPs.
- Phase 24 run (2026-10-04): **23 executed steps PASS** — configure, build,
  **2741/2741 unit+integration**, determinism diff = 0 bytes, 8 scenario sequences
  (nominal, rc_loss, imu_dropout, battery_low, gnss_loss, noiseless, ai_loss,
  imu_rc_loss), GS self-test (18 tests) + 2 end-to-end decodes + a telemetry
  schema-v2 decode step (240 records, action=MOTOR_STOP, --strict exit 1), HIL target
  build, 6 host-HIL steps (loopback, DShot capture, two fault-injection runs, link
  robustness, watchdog interlock) and the requirement/architecture audit (83 baseline
  IDs, TEST-004/FW-001/FW-004) — plus 2 H-gated SKIPs naming the missing toolchain/
  board (TEST-002).
- Phase 25 run (2026-10-04): **26 executed steps PASS** + 2 H-gated SKIPs,
  exit 0, with the Phase 24 evidence (2741/2741, 8 scenarios, GS 18 tests + 2 end-to-end
  decodes + the schema-v2 decode, HIL build and 6 host-HIL steps, requirement audit)
  unchanged and three new manufacturing steps added: parameter-default regeneration
  (reproduced the pinned blob byte-for-byte), the release manifest audit (7 check
  groups) and the audit's negative tests (16/16 cases).
- Phase 26 run (2026-10-04): **27 executed steps PASS** + 2 H-gated SKIPs,
  exit 0. The Phase 25 evidence is unchanged; the new step 10 is the documentation
  audit (`11_documentation/tools/docs_audit.py`), which re-checks the document
  index in both directions, entry-document links, placeholder/stub markers, the
  derived facts against the files that own them (telemetry schema/payload, cmd-gate
  version, parameter blob version/CRC, package revision and item counts) and the
  observed counts against the run that just measured them — and fails if a number
  quoted in the walkthrough disagrees with `PROJECT_FACTS.json`. Phase 26 changed
  no control-path code, so the timing and scenario evidence above still applies to
  the same binaries.
- Phase 27 run (2026-10-04): **29 executed steps PASS** + 2 H-gated SKIPs,
  exit 0. New step 11 is the V&V review gate (`08_testing/vnv_gate.py`): every
  baseline requirement is classified from its verification row (the *last* status
  token wins, so a compound row's caveat counts), evidence references must
  resolve, a critical requirement that is not verified must carry a written
  blocker, and the review document's quoted counts must equal the gate's. Result:
  83 requirements — 49 verified, 34 unverified with written blockers, 0 failed —
  and 23/23 critical verified or explicitly blocked; selftest 7/7. Phase 27
  changed no control-path code, so the evidence above still applies to the same
  binaries.
- Phase 28 run (2026-10-04): **31 executed steps PASS** + 2 H-gated SKIPs,
  exit 0. New step 12 is the release manifest audit (`13_release/tools/audit_release.py`):
  the RC-1 manifest pins the 588-file source tree digest and 10 release items,
  requires the 4 gated artifacts to be genuinely absent, checks the build targets
  and reproduction commands, and fails while OPS-001 reads OPEN (it did, on its
  first run — corrected at cause). Selftest 7/7.
- **Current run (Phase 29, 2026-10-04): 33 executed steps PASS** + 2 H-gated SKIPs,
  exit 0. New step 13 is the final A-Z audit (`08_testing/final_audit.py`): every
  `TODO`/`TBD` marker in the repository must be dispositioned in
  `08_testing/FINAL_AUDIT.md`, two-way (an unregistered marker or a changed count
  fails), and the audit fixed two stale documents at cause (the Phase 03 task
  table's 10 `TBD` cells and three stale traceability evidence cells). Selftest
  6/6. No control-path code changed in Phases 26-29, so the timing and scenario
  evidence above still applies to the same binaries.
- Determinism re-verified after every phase's code changes (seed 1234, 8000 ticks).

## 6. Acceptance criteria — requirement → level → evidence map

| Domain | Requirement | Level / method | Evidence today | Status |
|---|---|---|---|---|
| CTRL-001/002 | Cascade PID, anti-windup, rate/att loops | L0+L2 | TEST-CTRL-RATE/HOV/YAW | SIM (H pending) |
| CTRL-005 | Disarm → zero output | L0+L1 | TEST-CTRL-DISARM, TEST-CTRL-YAW disarmed path | SIM |
| EST-001 | Quaternion attitude | L0+L2 | TEST-EST-001/002 + yaw integration | SIM |
| EST-002 | Altitude/vertical fusion | L0+L2 | TEST-CTRL-ALT, 60 s drift (mean 0.071 m), divergence re-anchor | SIM |
| EST-003/004 | Position tracking + outlier gate | L0 | TEST-EST-003/004 | PARTIAL (no lateral dynamics) |
| SEN-002/003/004 | Validation, calibration, health | L0 | TEST-VAL/CAL/HEALTH | SIM |
| NAV-001/003 | Mission sequence, guards | L0+L2 | TEST-NAV-SEQ, WP boundary | SIM |
| NAV-004 | Geofence → RTL, LAND exclusion | L0+L2 | TEST-NAV-FENCE | SIM |
| SAF-001 | Failsafe hierarchy RC>IMU>batt>geofence>companion | L0+L2 | TEST-SAF-PRIORITY (+L2 scenarios) | SIM |
| SAF-003 | No sensor failure → uncommanded surge | L1+L2 | TEST-SIM-IMUFAIL, imu_dropout→ABORT | SIM |
| SAF-030/031 | Battery thresholds + RTL latch | L0+L2 | TEST-SAF-BATT, latch paths, battery_low→LAND | SIM |
| SYS-003 | RC loss → RTL ≤500 ms | L0+L2 | TEST-SAF-RC, TEST-NAV-RCLOSS, rc_loss→RTL; **timing measured in sim clocks only** | SIM (H pending) |
| SYS-004 | Companion loss fallback | L0+L1 | **Phase 17**: link-layer health flips ≤1 s (TEST-PERC-DEATH); ai_healthy in perception state; UART heartbeat still Phase 19 | PARTIAL (SIM) |
| PERC-001..004 | Obstacle picture: bounds, staleness, degraded modes, FC-sensor priority (SAF-041) | L0+L1+L2 | TEST-PERC-FUSED/ROTATION/CONF-GATE/STALE/DEATH/CONFLICT/TOF-SYNTH/INVALID; TEST-AI-LOSS; ai_loss scenario asserted in runner (PERCEPTION_FUSION.md §5) | SIM |
| AVD-001..003 + SAF-040 | Avoidance: cone threat, brake curve, hard stop, clamp-last bounds, deterministic degraded behavior | L0+L1 | TEST-AV-OFF/SLOW/STOP/LATERAL/CLAMP/APP (OBSTACLE_AVOIDANCE.md §5); behavioral steering deferred until lateral dynamics exist | SIM (behavioral L2 pending) |
| COM-001 | RC: CRSF/SBUS parse, normalization, protocol failsafe | L0 | TEST-COM-CRSF/SBUS | SIM (radio H-gated) |
| COM-002 | MAVLink v2 telemetry, degraded link tolerated | L0 | TEST-COM-MAVLINK/TELEMETRY | SIM |
| COM-003 | Companion link: CRC, seq, heartbeat, versioned schema | L0+L1 | TEST-COM-ICD02/OBSTACLE-SET/LINK/BYTEPATH | SIM |
| COM-004 | FC master of safety data; GS cannot arm | L0 + end-to-end | TEST-COM-FCSTATE/TELEMETRY; GS self-test + firmware-log decode (GROUND_STATION.md §5) | SIM-VERIFIED (command transport still open) |
| SIM-001..004 | Virtual sensors, determinism, physics, replay | L0+L2 | SIMULATION_VALIDATION.md §5; TEST-SIM-LATERAL/DRAG/WIND; TEST-AVOID-FLOWN (SIM_TUNING_RECORD.md) | SIM (model not HW-identified) |
| TEST-001 | Automated suite, regression on change | runner | run_regression.sh exit 0 (33 executed steps as of Phase 29) | MET (SIM + host HIL) |
| TEST-002 | H tests marked + explicit skip message | runner | 2 SKIP lines with reasons (STM32 build, HIL hardware) | MET |
| TEST-003 | Coverage: math/filters/PID/fusion/mixer/protocol/SM | L0 | all domains have ≥1 L0 test (§6) | MET (SIM) |
| TEST-004 | Traceability: every requirement ≥1 row + a status row | matrix audit | TRACEABILITY_MATRIX.md + VERIFICATION_REPORT.md (83 IDs), enforced by audit_requirements.py as runner step 7 | MET |
| MFG-001 | Release package version-matched: gerbers, BOM, pick&place, firmware binary, parameter default file, docs | manifest audit | runner step 8 — `audit_release_manifest.py` (16 items: 8 digest-pinned and reproducing, 8 gated and probed) + `selftest_audit.py` 16/16 | **PARTIAL** — BOM, parameter default file, procedures and inspection criteria exist and are audited every run; gerbers, drill, P&P, stencil, drawings, firmware image and per-unit records are gated on `EDA_TOOLCHAIN`/`HW-010`/`STM32_TOOLCHAIN`/`BOARD` |
| SYS-001 | Loop rate ≥500 Hz | L1+L3 | fixed 1 kHz scheduler; TEST_LOOP-TIMING host distribution (p99 3 µs, p99.9 5 µs, max 23 µs) bounds software execution; **MCU rate needs HIL timer capture (HIL-3)** | PARTIAL (SIM); OPEN (H) |
| SYS-002 | Latency ≤5 ms p99 | L3+ | end-to-end latency is not measurable without a capture — no claim made; **HIL-3** | Open (H) |
| FW-001 / FW-004 | No backend headers in algorithm code; no dynamic allocation in the control path | audit | audit_requirements.py: 51 files scanned, 0 backend includes, 0 allocation sites; run as regression step 7 | MET (audit) |
| HW-006 | DShot600 on real ESC | L3/L4 | **Phase 22**: encoder + decoder round-trip over the HIL link, independent capture analysis (HIL-0c); wire timing on a real ESC still H | SIM-VERIFIED (H pending) |
| HIL-0a..0f | Critical-interface evidence: link integrity, sensor-rate fidelity, DShot bit stream, fault-injection latency, CRC robustness, watchdog interlock | L3 (host rig) | 6 executed regression steps (HIL_DESIGN.md §5/§6) | MET (host) — **not** hardware evidence |
| HIL-1..6 | Rig on the real board: 60 s run, nav equivalence, loop rate/latency capture, sensor correlation, ESC analyser comparison, bus fault injection | L3/L4 | design only | Open (H) |
| L5 flight | Controlled flight test | L5 | not started | Open (H) |

Level entry/exit criteria: L0 exits when the suite is green and deterministic;
L1/L2 exit when the expected app-level sequences occur with zero deadline misses;
L3–L5 exit criteria are HIL-1..6 (HIL_DESIGN.md) and FLIGHT_TEST_READINESS_REVIEW.md.
Since Phase 22 a host-side subset of L3 exists (HIL-0a..0f): the real application runs
behind a HIL HAL with the world on the far side of a framed link, and the runner asserts
link integrity, sensor-rate fidelity, the DShot bit stream, fault-injection latency, CRC
robustness and the watchdog interlock. That is **repeatable critical-interface evidence,
not hardware evidence** — HIL-1..6 remain open until the board exists.

## 7. Risks / TBDs

1. SYS-001/002 (real loop rate/latency) have **no hardware evidence path yet**; sim
   timing cannot substantiate them. They stay Open until HIL-3. The Phase 22 host rig
   measures link and injection latency in the rig clock, which bounds the software side
   of the budget but says nothing about the MCU's own timing.
2. Companion/Edge-AI verification (SYS-004, SAF-040, AI-001): **Phase 17 landed the
   FC-side perception consumer** (PERC-001..004, ai_loss scenario now asserted).
   Remaining: real UART link + companion health messages (Phase 19), SAF-040
   avoidance clamps (Phase 18).
3. Failsafe timing constants (RC 500 ms, IMU 100 ms) are unit-tested but not
   metrologically verified against a real RC link.
4. Test-coverage metric is check-count, not MC/DC; structural coverage tooling is
   out of scope until a toolchain decision (H-gated).
5. L3–L5 criteria exist only as design; executing them requires the Phase 08 board.

## 8. Next dependency

- **Current (Phase 25 complete):** Phase 26 (`26_documentation`) runs the
  new-engineer reproducibility pass over these documents. Note the phase-25 contract:
  anything under `12_manufacturing/` is digest-pinned, so a documentation edit that
  touches the package must be followed by a deliberate re-baseline
  (`audit_release_manifest.py --update-digests --revision <new>`), not a silent one.
- Manufacturers' package acceptance (MFG-001) completes only when `EDA_TOOLCHAIN` and
  `STM32_TOOLCHAIN` close; the audit probes both, so those items cannot be forgotten.
- Phase 17 (`17_safety/`) consumes this map: safety analysis (FMEA) should reuse the
  TEST-SAF-* evidence and the failsafe level/latch contract (DEC-012).
- Phase 21 tuning will add parameter-sweep regression cases on top of L2.
- Hardware path: Phase 08 bring-up → HIL-1..3 → re-run this runner with the STM32
  backend in place of the SKIP lines.
