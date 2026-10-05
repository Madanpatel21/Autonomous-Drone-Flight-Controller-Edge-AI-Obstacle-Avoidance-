# Verification Report — Phase 29

Response to `15_prompts/29_29_final_audit.prompt.md` (8-section format). Phase
23–28 content is preserved below and marked; this revision adds the final A-Z
audit and closes the phase chain.

Phase 29 touched **no control-path code** (the firmware suite is unchanged at
2741/2741): `08_testing/final_audit.py` (runner step 13) requires every
TODO/FIXME/XXX/HACK/TBD marker in the repository to be dispositioned in
`08_testing/FINAL_AUDIT.md`, two-way. The audit fixed two stale documents at
cause and issued release **RC-2** (superseding RC-1: same behaviour, corrected
documents, new audit). Runner: **33 executed steps PASS + 2 H-gated SKIPs**,
exit 0. **No certification is claimed.**

Phase 27 touched **no control-path code** (the firmware suite is unchanged at
2741/2741): `08_testing/vnv_gate.py` (regression step 11) classifies all 83
baseline requirements from their verification rows, resolves the evidence
references they name, and fails if a critical requirement is unverified without a
written blocker. Runner: **29 executed steps PASS + 2 H-gated SKIPs**, exit 0.
The narrative review, the critical-requirement table and the residual-risk
register are in `08_testing/VNV_REVIEW.md`; **no certification is claimed**.

Phase 26 (previous revision) made document status machine-checkable; that
mechanism still runs as regression step 10.

**Every one of the 83 baseline requirements in
`00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md` has a row below.**
`08_testing/audit_requirements.py` parses this file and the traceability matrix,
and fails if any baseline ID is missing a status or a traceability row — so this
table cannot silently drift away from the requirements.

**Status vocabulary (deliberately narrow):**

| Status | Meaning |
|---|---|
| **MET (SIM)** | Acceptance verified by an executed test/scenario on the SIM or host target |
| **MET (host HIL)** | Verified through the host HIL rig (Phase 22) — **not** hardware evidence |
| **PARTIAL** | Some acceptance elements verified, others not; the gap is named in the row |
| **OPEN (H)** | Requires real hardware/toolchain: no board, no rig in this workspace |
| **OPEN** | Not implemented or not evidenced yet, and deliberately not faked |

A SIM result is never counted as hardware evidence (agent contract). Requirements
whose literal wording is not met by the implementation are marked PARTIAL and the
deviation is named — see EST-001 and EST-003.

---

## 1. Inputs / assumptions

- Baseline: `00_project_control/02_requirements/REQUIREMENTS_DOMAINS.md` (83 IDs, APPROVED 2026-10-03).
- Evidence sources: the regression runner (`08_testing/regression_tests/run_regression.sh`),
  the firmware suite (`02_firmware/tests/test_main.c`, **2741/2741 checks**), the host
  ground-station tool (**18 tests** + 3 end-to-end decodes), the host HIL rig (6 executed
  steps), the safety analysis in `00_project_control/04_safety/`, and the per-phase
  evidence records under `03_edge_ai/`, `06_communication/`, `07_simulation/`, `08_testing/`.
- Phase 24 re-run (2026-10-04): **23 executed steps PASS + 2 H-gated SKIPs, script
  exit 0**, including the new `imu_rc_loss` scenario (`safety: failsafe=1 action=4`,
  nav `TAKEOFF HOLD ABORT`, `deadline_misses=0`) and the new ground-station
  telemetry-schema-v2 decode step (240 status records, `action=MOTOR_STOP`,
  `--strict` exit 1).
- Assumptions carried forward: STM32F765VIT6 (DEC-002) is the production target but
  **no board and no `arm-none-eabi-gcc` exist here**, so every hardware row is OPEN(H);
  control gains are sim-class and not bench-identified (DEC-009/017); no AI model has
  been trained or benchmarked on this host, so no AI performance number is claimed.

## 2. Work performed

1. **Enumerated the baseline** (83 IDs) and audited coverage with a new executable
   check instead of an assertion: `08_testing/audit_requirements.py` verifies
   TEST-004 (every requirement has a status row and a traceability row), FW-001 (no
   algorithm file includes a HAL backend header — 51 files scanned, 0 violations) and
   FW-004 (no dynamic allocation in the control path — 0 sites after comment stripping;
   the first run flagged a false positive from the word "allocation-free" in a comment,
   which is why comments are stripped before scanning).
2. **Closed six requirement gaps with new executed tests** (`test_param_power_cycle`,
   `test_loop_timing_distribution`, `test_estimator_health_escalation`,
   `test_arming_interlock_rc_gate`, `test_no_dynamic_allocation_api`, plus the GS
   `EMERGENCY_STOP` command test): SYS-006, FW-002, FW-004, FW-005 evidence, EST-055,
   CTRL-004/SAF-005 and GS-003 moved from "no evidence" to executed evidence.
3. **Gave every remaining requirement an explicit status** — including the ones that
   are honestly not met, with the specific missing element named per row.
4. **Extended the traceability matrix** so TEST-004 is satisfiable for all 83 IDs.

## 3. Requirement → evidence map

### System (SYS)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| SYS-001 | L1/L2 + host timing | fixed 1 kHz scheduler (TICK_HZ 1000, `app_tick_1khz`), deadline monitor; host tick execution p99 = 3 µs, p99.9 = 5 µs (TEST_LOOP-TIMING). "IMU ≤2 ms old" and the 500 Hz rate on silicon | **PARTIAL** — software dispatch verified; MCU rate/latency needs HIL-3 (H) |
| SYS-002 | L1 host timing | end-to-end latency is not measurable without a capture; single-tick execution p99 = 3 µs bounds the software side | **OPEN (H)** — HIL-3 timer capture; no claim made here |
| SYS-003 | L0+L2 | `rc_loss` scenario → failsafe entry, `test_saf_*` RC-loss paths, HIL step "fault injection (rc_loss → LAND)" with `first_loss_us` matching the injected time | **MET (SIM)** |
| SYS-004 | L0+L1+L2 | companion 1 s heartbeat health, `ai_loss` scenario asserted in the runner (perception degrades to TOF_ONLY, flight continues), HIL link runs the same ICD-02 stream. **Phase 24** added TEST-SAF-COMPANION-NOT-FAILSAFE so companion loss is proven to stay *outside* the failsafe tier set (DEC-007) | **MET (SIM)** |
| SYS-005 | A + L0 | `01_hardware/00_system/POWER_BUDGET.md`, `02_power/POWER_ARCHITECTURE.md`; SAF-030/031 thresholds unit-tested at 3.5/3.4/3.1 V | **MET (analysis)**; real pack curve/brownout behaviour = **OPEN (H)** |
| SYS-006 | L0 | TEST_PARAM-POWERCYCLE: stored config survives a restart; magic-destroyed and CRC-broken images both fall back to safe defaults (SAF-030 default restored), and re-store recovers | **MET (SIM)** |
| SYS-007 | A | `01_hardware/00_system/POWER_BUDGET.md` (hover power, 4S 4500 mAh endurance budget), mass budget in the same record | **MET (analysis)**; endurance by measurement = **OPEN (H)** |

### Hardware (HW)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| HW-001 | R | DEC-002 + `01_hardware/01_flight_controller/pcb/PCB_DESIGN_RECORD.md` (STM32F765VIT6) | **MET (review)**; board unbuilt → **OPEN (H)** |
| HW-002 | R+T | ICM-42688-P datasheet review; SIM IMU stream at 1 kHz (age_max 0 µs measured through the HIL link) | **MET (review+SIM)**; SPI driver timing **OPEN (H)** |
| HW-003 | A | BMP390 review; altitude drift evidence 60 s mean 0.071 m (TEST-CTRL-ALT/drift) | **MET (analysis+SIM)**; bench noise **OPEN (H)** |
| HW-004 | R | NEO-M9N review; SIM GNSS 10 Hz with 3 s TTFF (measured age_max 99 ms through HIL) | **MET (review+SIM)** |
| HW-005 | R | TF-Luna forward + downward; SIM 25/50 Hz (measured age_max 39/19 ms) | **MET (review+SIM)** |
| HW-006 | T | `esc_dshot.c` frame+CRC-4, decoder round-trip (TEST-DSHOT-CAPTURE: every single-bit flip rejected), 24000 HIL-captured frames CRC-valid at exactly 1000 µs period | **MET (SIM)** for DShot600; **PARTIAL** — the PWM fallback named in the requirement is not implemented |
| HW-007 | T | battery sample type with 4-cell voltages + current + consumed mAh, SAF-030/031 thresholds tested | **MET (SIM)**; ADC/shunt accuracy **OPEN (H)** |
| HW-008 | R+T | DEC-003 ICD, `companion_link` task with 1 s heartbeat, HIL link carries the same bytes | **MET (SIM)**; **PARTIAL** — FC-controlled companion power-cycle is not implemented |
| HW-009 | R | `01_hardware/01_flight_controller/SCHEMATIC_REVIEW_CHECKLIST.md` (keying, TVS, fuse, reverse protection) | **OPEN (H)** — review checklist exists, no board to review against |
| HW-010 | R | PCB_DESIGN_RECORD.md (4-layer, IMU isolation, in-plane placement) | **OPEN (H)** — no PCB layout exists yet |

### Firmware (FW)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| FW-001 | T | `audit_requirements.py`: 51 algorithm files scanned, **0** backend-header includes; three targets build from one `fc_core` (`sim`, `hil`, `stm32` skeleton) | **MET** |
| FW-002 | T | `app_tick_1khz` fixed rate with `deadline_misses` counter; TEST_LOOP-TIMING distribution (mean 0.5 µs, p99 3 µs, p99.9 5 µs, max 23 µs); HIL watchdog interlock proves the reset path is armed | **MET (SIM)** |
| FW-003 | T | typed `hal_status_t` everywhere; `imu_dropout`/`rc_loss`/`gnss_loss` drive health counters and the flight continues on remaining sensors; sensor_health counters asserted | **MET (SIM)**; bus-level faults **OPEN (H)** |
| FW-004 | R | `audit_requirements.py`: **0** `malloc/calloc/realloc/free` sites in common/ + flight_controller/ + the world model; codecs use fixed buffers | **MET (audit)** |
| FW-005 | T | TEST_PARAM-POWERCYCLE: magic+CRC image, defaults on corruption, recovery after a failed store | **MET (SIM)**; on-chip flash endurance **OPEN (H)** |
| FW-006 | T | telemetry/status emission runs inside the tick with `deadline_misses=0` in every scenario, incl. the GS byte log | **PARTIAL** — budget observed, but there is no explicit overload-drop policy or measurement |
| FW-007 | T | `02_firmware/bootloader/` contains design placeholders only (configuration/linker/source READMEs) | **OPEN** — no bootloader code, no SIM mock-flash test |

### Sensors (SEN)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| SEN-001 | R+T | `sensor_hub.c` pipeline (driver → calibration → validation → health) with per-stage unit tests | **MET (SIM)** |
| SEN-002 | T | TEST-VAL-* rejection classes: range, NaN, impossible jump, stale timeout | **MET (SIM)** |
| SEN-003 | T | `calibration.c` gyro-bias/accel tests incl. residual-bias bounds | **MET (SIM)**; 6-face on hardware **OPEN (H)** |
| SEN-004 | T | timeout/range/error counters; forced timeout → unhealthy within a cycle and failsafe escalation asserted | **MET (SIM)** |
| SEN-005 | T | measured worst-case tick execution 23 µs over 20 000 ticks (TEST_LOOP-TIMING) | **MET (SIM)**; MCU interrupt latency **OPEN (H)** |

### Estimation (EST)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| EST-001 | L0+L2 | quaternion propagation + accel leveling; 15° perturbation recovery to 0.01°, rate tracking 0.967/1.0 rad/s (CONTROL_VALIDATION.md) | **PARTIAL** — the implemented filter is a complementary filter with gyro-bias estimation, **not** a quaternion EKF and there is **no magnetometer**; the requirement wording needs a requirements-review decision (recorded, not silently reinterpreted) |
| EST-002 | L0+L2 | baro + accel fusion; 60 s hover drift mean 0.071 m, divergence re-anchor case | **MET (SIM)** |
| EST-003 | L0+L2 | GNSS + IMU prediction with innovation gating; flown 6 m waypoint, est-vs-truth `err_xy = 0.010 m` (HIL correlation) | **PARTIAL** — optical flow is a NOT_READY stub, so the estimator is GNSS-only horizontally |
| EST-004 | L0 | injected outliers rejected, no estimate jump beyond the gate | **MET (SIM)** |
| EST-055 | L1+L2 | TEST-EST-HEALTH: GNSS loss after a healthy fix → `est_position_healthy()==false` after 1018 ms (declared 1.0 s timeout + one 10 Hz sample age) and the mission descends in place | **MET (SIM)** |
| EST-006 | R | every sample type carries `timestamp_us` + `valid`; consumers check them (sensor_hub, control, perception) | **MET (review)** |

### Control (CTRL)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| CTRL-001 | L0+L2 | cascade position→velocity→attitude→rate, D-on-measurement, anti-windup, saturation; crosswind rejected to 0.00 m | **MET (SIM)** |
| CTRL-002 | L0+L2 | 1 kHz / 250 Hz / 100 Hz / 50 Hz divides with a 900 µs deadline monitor, `deadline_misses=0` in every scenario | **MET (SIM)**; rates on hardware **OPEN (H)** |
| CTRL-003 | L0 | quad-X saturation minimisation, priority collective then stability axes; disarmed zero-output case | **MET (SIM)** |
| CTRL-004 | L1 | TEST-ARM-INTERLOCK: with RC dead the mission never leaves IDLE, motors stay STOP; restoring RC + arm switch starts it | **MET (SIM)** |
| CTRL-005 | L0+L1 | disarm → DShot STOP on all four motors (asserted in the interlock test and in motor_output paths) | **MET (SIM)** |
| CTRL-006 | R+T | parameters are read into fixed structs at init; no mid-loop mutation path exists | **PARTIAL** — the loop-boundary guarantee holds by construction, but there is no runtime parameter-update path yet to test |

### Navigation (NAV)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| NAV-001 | L0+L2 | waypoint arrival tolerance + auto RTL; full WP→RTL→LAND→DONE run asserted | **MET (SIM)** |
| NAV-002 | L0+L1+L2 | obstacle avoidance from the fused picture, flown end-to-end (min clearance 1.11 m, RETREAT), AI advisory only | **MET (SIM)** |
| NAV-003 | L0+L2 | takeoff/landing state machines with altitude guards; scenarios land and report DONE | **MET (SIM)** |
| NAV-004 | L0+L2 | geofence ceiling/floor/radius → RTL, LAND excluded from the fence; boundary case tested | **MET (SIM)** |

### Communication (COM)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| COM-001 | T | CRSF primary + SBUS fallback, 885–1795 µs → 1000–2000, protocol failsafe flag, invalid frames rejected | **MET (SIM)**; radio **OPEN (H)** |
| COM-002 | T | MAVLink v2 subset round-trip, CRC resync, degraded link tolerated; GS decodes 720 frames with 0 CRC errors | **MET (SIM)** |
| COM-003 | T | ICD-02 codec + link task: CRC error, out-of-order/reorder, versioned schema; HIL link re-uses the same codec across a real byte stream | **MET (SIM)** |
| COM-004 | T | FC failsafe **and action** are always on the wire (telemetry schema v2); the GS rejects the whole arming class (6 spellings) and any failsafe override. **Phase 24 added the FC-side receiver gate** `02_firmware/flight_controller/communication/command/cmd_gate.c`: magic/version/length/CRC16 checked before dispatch, the arming class refused unconditionally, EMERGENCY_STOP always accepted, mode whitelist, SET_PARAM clamped to FC-owned envelopes (NaN/unknown id refused), waypoint batches bounded (≤16 points, 8 B/point, 50 m fence). Evidence: TEST-CMDGATE-ARMING, TEST-TELEM-ACTION, runner GS schema-v2 step | **PARTIAL** — validation *and* the FC-side authority gate now exist and are proven, and the gate is fed by `FC_SIM_CMD_LOG` in the sim; **no live command transport exists yet**, so the wire path itself remains unproven. The boundary is enforced by the FC, not by the GS (DEC-021) |

### Ground station (GS)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| GS-001 | T | CLI decodes and reports attitude, position, altitude, battery, mode, failsafe, perception/avoidance mode from the real FC byte log; waypoint missions load/validate/save | **MET (SIM)** — text tool, no graphical display |
| GS-002 | T | `SET_PARAM` whitelisted with FC-envelope bounds, `SAVE_PARAMS`; mission write with validation. **Phase 24**: the same bounds are now enforced *on the FC* as well (SET_PARAM clamped to FC-owned envelopes; unknown id and NaN refused), so a GS bypass cannot widen a limit | **PARTIAL** — validation proven on both sides; no read-back confirmation round trip without a command transport |
| GS-003 | T | RC path: failsafe + disarm + motor stop asserted in the suite. GS path: `EMERGENCY_STOP` is unconditionally accepted and maps to the same bounded LAND action, with a unit test. **Phase 24**: the FC gate always accepts EMERGENCY_STOP and the GS decodes the v2 action field, raising MOTOR_STOP as its own CRITICAL SAFETY_ACTION line (GS `test_safety_action_on_the_wire`, runner schema-v2 step: 240 records, exit 1 under `--strict`) | **PARTIAL** — validated and now decoded end-to-end against firmware-generated bytes; **live transport still open** |

### Simulation (SIM)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| SIM-001 | T | every virtual sensor class with noise/bias/dropout/fault injection, unit-tested per class | **MET (SIM)** |
| SIM-002 | T | determinism double-run diff = 0 bytes, asserted in the runner | **MET (SIM)** |
| SIM-003 | T | open-loop thrust sanity + flown closed loop (6 m waypoint, altitude held, wind rejected) | **MET (SIM)**; model not HW-identified (DEC-009/017) |
| SIM-004 | T | 7 scripted scenarios with asserted nav sequences + `deadline_misses=0`, run by the regression | **MET (SIM)** |

### Safety (SAF)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| SAF-001 | R+T | priority/latch unit tests + scenario matrix across RC/IMU/battery/geofence/companion; **Phase 24**: the tier is now chosen by flyability (`failsafe_action()`, DEC-021) — TEST-SAF-ACTION-FEASIBLE reports RC loss while the action is MOTOR_STOP, TEST-SAF-COMPANION-NOT-FAILSAFE proves companion stays outside the tier set, and the `imu_rc_loss` scenario asserts `failsafe=1 action=4` on the live flight log | **MET (SIM)** |
| SAF-002 | T | each bounded action exercised: hover-in-place, RTL, descend-land, motor stop. **Phase 24** made the action a first-class derived quantity: MOTOR_STOP on IMU timeout **or** a deadline storm (dominates every other tier), RC→RTL, battery/latched band→RTL-LAND, geofence→RTL, estimator→LAND (never MOTOR_STOP). Evidence: TEST-SAF-ACTION-FEASIBLE, TEST-SAF-DEADLINE-STORM, TEST-SIM-IMUFAIL, scenario matrix (rc_loss/battery_low/imu_rc_loss), and the GS schema-v2 step reporting `action=MOTOR_STOP` from a real firmware log | **MET (SIM)**; the *physical* completion of a motor-stop descent is **OPEN (H)** |
| SAF-003 | T | IMU dropout → no surge, zero outputs, ABORT (scenario + unit); the historical 46 m climb regression stays covered | **MET (SIM)** |
| SAF-004 | T | SIM deadline counter + **negative test** on the host HIL rig: starving the feed expires the emulated IWDG and fails the run (exit 3). **Phase 24** additionally proved the safety consequence: TEST-SAF-DEADLINE-STORM drives `deadline_misses` past 100 and requires `FC_ACTION_MOTOR_STOP` rather than a flyable action | **MET (SIM + host HIL)**; real IWDG **OPEN (H)** |
| SAF-005 | T | TEST-ARM-INTERLOCK (RC gate, throttle-zero, healthy sensors). **Phase 24** adds the inbound side: the FC command gate refuses the entire arming class unconditionally — TEST-CMDGATE-ARMING verifies 10/10 arming frames (ARM, DISARM_OK, FORCE_ARM, CLEAR_ARM_GATE, OVERRIDE_FAILSAFE) are refused, and EMERGENCY_STOP is always accepted while remaining unable to arm or clear a failsafe | **MET (SIM)**; the live command path is **OPEN** (COM-004) |
| SAF-030 | T | 3.5 warn / 3.4 RTL / 3.1 land thresholds, hysteresis and first-seen-below-land rule | **MET (SIM)** |
| SAF-031 | T | battery failsafe cannot be cleared below critical (latch test) | **MET (SIM)** |
| SAF-040 | T | adversarial-input clamp test: every published avoidance component inside FC-owned limits, clamp applied last | **MET (SIM)** |
| SAF-041 | T | AI/ToF conflict in the ±20° cone → FC range wins, confidence ×0.5 | **MET (SIM)** |

### Edge-AI (AI)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| AI-001 | T | OBSTACLE_SET published at 25 Hz on the real ICD-02 wire path, consumed by the FC with freshness checks | **PARTIAL** — publish rate verified; the ≤100 ms sensor→message latency is a hardware/companion property, **OPEN (H)** |
| AI-002 | R | the AI payload carries only obstacle geometry; the FC converts it to a bounded velocity setpoint (DEC-007/014), verified by the clamp test | **MET (review)** |
| AI-003 | T | none: **no model is trained, installed or benchmarked on this host** | **OPEN** — no latency number is fabricated; benchmark harness lands with the companion target |
| AI-004 | T | companion health state machine (no inference / stale frame / low confidence) published in ICD-02 HEALTH and consumed by perception | **MET (SIM)** |
| AI-005 | R | none | **OPEN** — no dataset or model exists; explicitly not fabricated |

### Perception fusion (PERC) / avoidance (AVD)

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| PERC-001 | T | bounded ≤16 picture with confidence, source tag and FC-domain timestamps | **MET (SIM)** |
| PERC-002 | T | each invalidity class tested (stale AI/ToF, zero time, count>16, low confidence, out-of-band) | **MET (SIM)** |
| PERC-003 | T+L2 | NONE/TOF_ONLY/AI_ONLY/FUSED modes; companion death never escalates a failsafe (ai_loss scenario) | **MET (SIM)** |
| PERC-004 | T | conflict clamp + ToF-only synthesis below 8 m | **MET (SIM)** |
| AVD-001 | T | mode boundaries SLOW/STOP/RETREAT on the brake curve | **MET (SIM)** |
| AVD-002 | T | clamp-last bounds with adversarial inputs (≤2.0 fwd, ≤1.0 lateral, vertical 0) | **MET (SIM)** |
| AVD-003 | T+L2 | empty/invalid picture → inactive zero command; flown run reaches RETREAT without any failsafe | **MET (SIM)** |

### Testing (TEST) / manufacturing / operations

| ID | Level / method | Evidence | Status |
|---|---|---|---|
| TEST-001 | runner | `run_regression.sh` exit 0 with 33 executed steps | **MET** |
| TEST-002 | runner | 2 SKIP lines naming the missing toolchain/board; host HIL explicitly does **not** claim HIL-1..6 | **MET** |
| TEST-003 | suite | 2741 checks spanning math, filters, PID, fusion, mixer, protocols (ICD-02/MAVLink/CRSF/HIL), state machines, parameters | **MET (SIM)** |
| TEST-004 | audit | `audit_requirements.py` enforces a status row + traceability row for all 83 baseline IDs and is wired into the regression | **MET** |
| MFG-001 | R (manifest audit) | `12_manufacturing/RELEASE_MANIFEST.json` + `tools/audit_release_manifest.py` (runner step 8): 16 items, 8 digest-pinned and reproducing byte-identically, 8 gated with resolvable gates, 4 facility gates probed, 7 version bindings cross-checked FC↔GS, released parameter blob CRC + 14 fields re-derived independently; `tools/selftest_audit.py` 16/16 negative cases. Fabrication half (gerbers, drill, pick-and-place, stencil, assembly drawing, firmware image) **cannot exist**: no EDA toolchain, no layout, no `arm-none-eabi-gcc` | **PARTIAL** — the package is software-complete and audited; every fabrication/image/board item is gated, and the gates are probed rather than asserted |
| OPS-001 | R | `13_release/RELEASE_MANIFEST.json` + `13_release/tools/audit_release.py` (runner step 12): versioned RC-1 pins the source tree digest (no VCS exists, so the digest is the revision identifier) and 10 release items with sha256, gates 4 absent artifacts (STM32 image, PCB/fabrication, AI model, per-unit QC) with probed absence, checks the build targets and reproduction commands, and fails while OPS-001 reads OPEN; 7/7 negative cases | **PARTIAL** — the software/evidence release record exists and is audited; an operational (flyable) release is gated on `BOARD`/`STM32_TOOLCHAIN` |

## 4. Files created / modified

### Phase 25 (manufacturing release package)

Created: `12_manufacturing/RELEASE_MANIFEST.json`,
`12_manufacturing/MANUFACTURING_RELEASE_CHECKLIST.md` (rewritten: 21 lines with
evidence or gate each), `12_manufacturing/bom/CONTROLLED_BOM.csv` + `bom/README.md`,
`12_manufacturing/parameters/params_defaults.{bin,json}`,
`12_manufacturing/fabrication/FABRICATION_DATA_SPEC.md`,
`12_manufacturing/pcb_inspection/INSPECTION_CRITERIA.md`,
`12_manufacturing/assembly/ASSEMBLY_SEQUENCE.md`,
`12_manufacturing/PROGRAMMING_AND_PRODUCTION_TEST.md`,
`12_manufacturing/tools/{dump_param_defaults.c,audit_release_manifest.py,selftest_audit.py}`.

Rewritten from placeholders: `12_manufacturing/README.md`, `bom/README.md`,
`gerbers/README.md`, `drill_files/README.md`, `assembly/README.md`,
`pcb_inspection/README.md`, `qc/README.md`, `release/README.md` — every one of
these was the literal string "Purpose: define and store artifacts for this project
area." before this phase.

Modified: `08_testing/regression_tests/run_regression.sh` (new step 8, three
report lines; hardware step renumbered to 9).

### Phase 24 (safety analysis)

Created: `00_project_control/04_safety/FAILURE_MODE_AND_EFFECTS_ANALYSIS.md` (22
failure modes, stated project-local risk method),
`00_project_control/04_safety/FAULT_TREE_ANALYSIS.md` (top event, T1–T7, 10 derived
safety requirements), `00_project_control/04_safety/PRELIMINARY_HAZARD_ANALYSIS.md`
(13 hazards + 5 forward hazards), `00_project_control/04_safety/SAFETY_CASE.md`
(6 claims, 6 explicit non-claims),
`02_firmware/flight_controller/communication/command/cmd_gate.{h,c}`.

Modified: `02_firmware/common/fc_types.h` (`fc_safety_action_t`),
`failsafe/failsafe_sm.{h,c}` (action model, geofence/estimator tiers, deadline gate),
`navigation/mission_sm.{h,c}` (`mission_request_rtl`), `app/app_main.c` (action
switch, failsafe feeds, `safety:` log line, `cmd_gate_init`),
`communication/telemetry/telemetry.{h,c}` (schema v2, 52 → 53 B payload),
`hal/sim/main_sim.c` (`FC_SIM_CMD_LOG`, `snap.action`), `hal/sim/sim_model.c`
(`imu_rc_loss` scenario), `tests/test_main.c` (+5 cases, 2667 → 2741 checks),
`06_communication/ground_station/gs_protocol.py` (schema v2 decoder) +
`ground_station.py` (reference encoder, `SAFETY_ACTION` classification, +2 tests,
16 → 18), `08_testing/regression_tests/run_regression.sh` (`imu_rc_loss` step and
GS schema-v2 decode step).

### Phase 23 (verification)

Created: `08_testing/VERIFICATION_REPORT.md` (this file),
`08_testing/audit_requirements.py`.
Modified: `02_firmware/tests/test_main.c` (+5 cases, 1600 → 2667 checks),
`06_communication/ground_station/gs_commands.py` + `ground_station.py`
(EMERGENCY_STOP + test, 15 → 16 tests),
`00_project_control/01_governance/TRACEABILITY_MATRIX.md` (rows for every baseline ID),
`08_testing/regression_tests/run_regression.sh` (audit step).

## 5. Interfaces affected

**Phase 24 — wire-visible interface changes (breaking; versioned):**

- **Telemetry status record: schema v1 → v2.** Payload 52 → **53 bytes**, with a new
  `uint8_t action` field at payload offset 45 (`fc_safety_action_t`:
  NONE/HOLD/RTL/LAND/MOTOR_STOP). A v1 decoder sees a length mismatch and must be
  updated; the GS refuses v1 frames rather than mis-decoding them. Both ends changed in
  the same phase and the round trip is asserted end-to-end against a real log.
- **`failsafe_sm.h`**: new `failsafe_action()`, `failsafe_notify_geofence()`,
  `failsafe_notify_estimator()`; `failsafe_mode_request()` now derives from the action.
- **`mission_sm.h`**: new `mission_request_rtl()` (idempotent, never preempts
  LAND/DONE/ABORT).
- **New inbound frame type** `cmd_gate.h`: `MAGIC 'C' | VER | TYPE | LEN | payload |
  CRC16-CCITT`, max frame 160 B. This adds a contract rather than changing one — there
  is no live transport for it to be compatible with.
- **New sim harness input** `FC_SIM_CMD_LOG` (newline-separated command frames fed to
  `cmd_gate_rx()` before the flight loop) plus a `cmdgate:` summary line.
- **New sim scenario** `imu_rc_loss`.

**Phase 23:** `gs_commands.py` `ALLOWED_COMMANDS` gained `EMERGENCY_STOP`. The audit
enforces FW-001/FW-004 as invariants rather than adding interfaces.

## 6. Verification performed

**Phase 25 (2026-10-04):**

- `python 12_manufacturing/tools/dump_param_defaults.c`-driven regeneration, run by
  the regression step: `gcc` builds the generator against the firmware's own
  `parameters.c` + `crc16.c`, which writes
  `12_manufacturing/parameters/params_defaults.bin` (90 B = 88 B struct + crc16) and
  `.json`. The renewed blob **reproduced the pinned digests**, which is what makes
  "reproducible manufacturing package" a measurement rather than a claim.
- `python 12_manufacturing/tools/audit_release_manifest.py` → **exit 0**, 7 check
  groups PASS: structure (16 items), present items (8 files, digests match, no stub),
  gated items (8, every gate resolves to a requirement that is not MET or to a
  declared facility gate), facility gates (4 blockers still probed true), version
  bindings (7; FC and GS agree and the released blob matches the firmware), parameter
  default file (CRC 0xF6C9 re-derived, 14 fields bit-exact, version 1), controlled BOM
  (16 rows, revision MFG-A, 8 unpinned rows all gated).
- `python 12_manufacturing/tools/selftest_audit.py` → **16/16 cases behaved as
  specified**: the control (untouched package audits clean) plus 14 defect classes
  (digest drift, stub-as-evidence, missing digest, invented status, ungated unpinned
  BOM row, unresolvable gate, BOM/package revision mismatch, gated directory not
  empty, expired facility probe, FC/GS schema disagreement, tampered blob CRC, gate
  already MET, unbumped re-baseline, re-baseline then re-check) and a successful
  re-baseline round trip.
- Full regression, script status captured directly (no pipeline):
  `bash 08_testing/regression_tests/run_regression.sh` → **exit 0**, 26 executed
  steps PASS + 2 H-gated SKIPs. New evidence lines:
  `parameter default file regeneration (rebuild from firmware defaults reproduced the pinned blob)`,
  `manufacturing release manifest audit (7 checks passed)`,
  `release manifest audit negative tests (16/16 cases behaved as specified)`.

**Phase 29 (2026-10-04):**

- `python 08_testing/final_audit.py` → **`FINAL AUDIT: PASS`**: 47 marker
  occurrences in 12 files, every one dispositioned (39 prose, 8 residual with
  named gates); the register is two-way checked.
- `python 08_testing/final_audit.py --selftest` → **6/6 cases behaved as
  specified** (control, unregistered marker, count mismatch, RESIDUAL without a
  gate, recorded fix with zero occurrences, stale register entry).
- **Two stale documents fixed at cause**: the task/priority table still carried
  10 Phase 03 `TBD` cells (now the rates and deadline the code actually runs),
  and the traceability matrix had three stale `TBD`/`Open` evidence cells plus a
  duplicate, conflicting SYS-003 row (now the current evidence and status).
- Full regression with step 13: **exit 0**, **33 executed steps PASS + 2 H-gated
  SKIPs**; new lines `final audit (marker register) (47 marker occurrence(s) in
  12 file(s))` and `final audit negative tests (6/6 cases behaved as specified)`.
- Release re-baselined to **RC-2** (590-file tree digest re-pinned; a changed
  artifact without a new release id fails step 12 by design).

**Phase 28 (2026-10-04):**

- `python 13_release/tools/audit_release.py` → **`RELEASE: PASS`**: tree digest
  and 588-file count match the manifest, 10/10 present items digest-matched,
  4/4 gated artifacts verified absent, OPS-001 recorded as PARTIAL.
- `python 13_release/tools/audit_release.py --selftest` → **7/7 cases behaved
  as specified** (control; tree-digest drift; item modified without re-baseline;
  missing item; a gated artifact that appeared; OPS-001 still OPEN; PARTIAL
  with no named gap).
- The audit's first real run **found the release's own outstanding contradiction**:
  the manifest existed while the OPS-001 row still read "OPEN". The row was
  corrected at cause — a release record now exists, so `OPEN` was no longer the
  truth — before the release could be claimed.
- Full regression with step 12: **exit 0**, **31 executed steps PASS + 2 H-gated
  SKIPs**; new lines `release candidate manifest audit (10 present item(s)
  digest-matched, 4 gated item(s) verified absent)` and
  `release candidate manifest audit negative tests (7/7 cases behaved as
  specified)`.

**Phase 27 (2026-10-04):**

- `python 08_testing/vnv_gate.py` → **`VNV: PASS`**: 83 baseline requirements
  classified (49 verified, 11 partial, 18 blocked, 5 open), 23 critical — 13
  verified and 10 explicitly blocked/partial/open with written blockers — 27
  evidence references checked, 0 broken, 0 failed review items.
- `python 08_testing/vnv_gate.py --selftest` → **7/7 cases behaved as
  specified** (control, written-reason cases, missing row, missing reason,
  broken evidence path, non-critical residual risk) — the same CLI run against
  fixture trees.
- Full regression with step 11: **exit 0**, **29 executed steps PASS + 2 H-gated
  SKIPs**; new lines
  `V&V requirement review gate (unverified 34 (partial 11, blocked 18, open 5))`,
  `V&V gate negative tests (7/7 cases behaved as specified)`.
- Review deliverable: `08_testing/VNV_REVIEW.md` — the gate also fails the run if
  the review stops quoting the current counts, so the narrative cannot drift from
  the classification.

**Phase 26 (2026-10-04):**

- `python 11_documentation/tools/docs_audit.py` → **`DOCS: PASS`**: 44 authoritative
  index rows (38 content + 6 task stubs), 44 local links in 4 entry documents
  resolved, 497 markdown files scanned with 282 scaffolds + 19 task stubs and **0
  unmarked**, 9 derived facts re-read from their sources, 11 observed counts compared
  against the run that measured them, 10 values quoted in the walkthrough checked.
- Full regression with step 10: **exit 0**, **27 executed steps PASS + 2 H-gated
  SKIPs**; the new line is
  `documentation audit (282 scaffold(s), 19 task stub(s))`.
- The negative path was demonstrated, not assumed: with `executed_steps` still at 26
  and the run measuring 27, the step failed and printed the exact re-baseline command
  (`--update-facts --observed executed_steps=27`); after the deliberate re-baseline,
  the quote check then failed on the walkthrough's stale "26 executed steps" until the
  prose was updated. Both stale-number paths (file vs run, prose vs file) are enforced.
- Marking pass: `mark_scaffolds.py --write` wrote 282 scaffold banners and 19
  task-stub banners; the 20th previous stub, `DOCUMENTATION_INDEX.md`, had already
  been rewritten as real content and was skipped as such.

**Phase 24 (2026-10-04):**

- `02_firmware/tests/fc_tests.exe` → **2741/2741**. New cases:
  `test_safety_action_feasibility` (RC_LOSS reported while the action is MOTOR_STOP),
  `test_safety_companion_not_a_failsafe`, `test_safety_deadline_storm_stops`
  (>100 missed deadlines ⇒ MOTOR_STOP), `test_command_gate_arming_class_refused`
  (10/10 arming frames refused; CRC, length, magic, version, unknown type, short
  frame, NaN parameter, parameter clamp and waypoint cap/offset/fence all refused),
  `test_telemetry_action_on_the_wire` (v2 round trip, v1 length refused, HEARTBEAT
  CRITICAL). `test_estimator_health_escalation` expectation updated: position loss is
  now `FC_FAILSAFE_ESTIMATOR` with action LAND and never MOTOR_STOP.
- `python ground_station.py --self-test` → **18 tests** OK (exit 0), including
  `test_safety_action_on_the_wire` (all five action codes; unknown codes surfaced, not
  dropped to NONE) and `test_status_record_v1_length_is_refused`.
- `python 08_testing/audit_requirements.py` → baseline 83, 83 status rows, 83
  traceability rows, **53** algorithm files scanned, FW-001 violations 0, FW-004
  allocation sites 0, PASS (exit 0).
- Full regression, script status captured directly (no pipeline):
  `bash 08_testing/regression_tests/run_regression.sh` → **exit 0**, 23 executed steps
  PASS + 2 H-gated SKIPs. New evidence lines:
  `scenario imu_rc_loss (safety action dominates) (TAKEOFF HOLD ABORT |safety: failsafe=1 action=4)`
  and `ground station telemetry schema v2 (action on the wire) (decoded 240 status records)`.

**Phase 23:**

- `fc_tests.exe` → **2667/2667** (was 1600). New: TEST_PARAM-POWERCYCLE,
  TEST_LOOP-TIMING (mean 0.5 µs, p50 0, p99 3, p99.9 5, max 23 µs),
  TEST-EST-HEALTH (1018 ms), TEST-ARM-INTERLOCK, TEST-NO-ALLOC-API.
- GS self-test → 16 tests OK.

## 7. Risks / TBDs

1. **Two requirements are not met as written and are marked PARTIAL, not reinterpreted**:
   EST-001 asks for a quaternion EKF with magnetometer correction (implemented: complementary
   filter + gyro-bias, no mag) and EST-003 asks for optical-flow-aided position estimation
   (implemented: GNSS + IMU). Both need a requirements decision, not a test.
2. **SYS-001/002 stay open for hardware.** Host tick timing bounds the software side; it is
   not MCU timing and is never presented as such.
3. **AI-003/AI-005 stay open and unfaked.** No model, dataset or latency number exists here.
4. HW-006's PWM fallback, FW-006's overload-drop policy, FW-007's bootloader and COM-004/GS-002/
   GS-003's command transport are unimplemented and recorded as such.
5. Requirement IDs were compared by parsing; a future baseline edit with a new naming shape
   would need the audit's parser updated (it fails loudly rather than silently ignoring).
6. **The manufacturing package is half a package on purpose.** MFG-001 lists gerbers,
   pick-and-place, a firmware binary and a parameter default file. Only the parameter
   file, the BOM and the procedures exist; the fabrication and image items are gated on
   `EDA_TOOLCHAIN`, `HW-010`, `STM32_TOOLCHAIN` and `BOARD`. The audit probes those
   blockers, so the day a layout or a toolchain appears the audit fails until the
   artifact is produced — but until then no board can be built from this repository,
   and that is a limitation of the workspace, not of the package.
7. Package digests make revision control real but also make edits interruptive: any
   change under `12_manufacturing/` fails the audit until `--update-digests --revision
   <new>` is run deliberately. That friction is intended (it is the only revision
   control this workspace has), but it must be paid in Phase 26 rather than silenced.

## 8. Next dependency

- **Phase 24 is complete** (`00_project_control/04_safety/`): the FMEA consumed the
  PARTIAL/OPEN rows above as failure-mode inputs — EST-001 (undetectable IMU bias, FM-02,
  CRITICAL), EST-003 (wrong GNSS fix, FM-08, HIGH), SYS-005 (power collapse, FM-06),
  FW-006/FW-007 (flash stall, FM-22), COM-004/GS-002/GS-003 (no transport, HZ-10) — and
  the analysis changed the firmware as a result (DEC-021: flyability-based action model,
  telemetry schema v2, FC-side command gate).
- **Phase 25 is complete** for everything the workspace can produce: the package, its
  audit and the audit's negative tests (see above). It hands Phase 26 a package whose
  documents are digest-pinned — so documentation edits touching `12_manufacturing/`
  must be followed by a deliberate re-baseline, not a silent one.
- **Phase 26 (documentation)** owns the new-engineer reproducibility pass; **Phase 28
  (release)** owns OPS-001 and the operational release record, which is already gated
  in the manifest (`MFG-PKG-16`).
- The audit must stay wired into the runner: a new requirement without a status row must
  fail the regression rather than be forgotten.
- **Next technical dependency, ordered by how much safety work each unblocks:**
  1. a live command transport — closes the COM-004/GS-002/GS-003 PARTIAL rows and gives
     the already-proven `cmd_gate` something to actually protect;
  2. bench identification of the control gains (all sim-class placeholders, DEC-009/017)
     plus a real sensor-fault injection run — the only thing that can close FM-01, FM-02
     and FM-13;
  3. HIL-3 MCU loop-rate/latency capture (SYS-001/SYS-002).
  None of these are achievable in this workspace: no board, no `arm-none-eabi-gcc`, no
  rig — and the runner reports both hardware steps as SKIP rather than PASS.