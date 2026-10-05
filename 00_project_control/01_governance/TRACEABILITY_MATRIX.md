# Requirements Traceability Matrix

Requirement → design → implementation → test → evidence. Updated per phase. Verification: A=analysis, I=inspection, R=review, T=SIM/SIL test, H=hardware test (only after real execution).

| Requirement ID | Requirement (short) | Design Element | Implementation | Verification | Evidence | Status |
|---|---|---|---|---|---|---|
| SYS-001 | Attitude stabilization ≥500 Hz | Control hierarchy (DEC-005) | ctrl_cascade.c (1 kHz rate / 250 Hz attitude loops) | TEST-CTRL-HOV (SIM) | 02_firmware/03_control/CONTROL_VALIDATION.md; 53→57/57 tests | SIM-VERIFIED (H pending) |
| SYS-002 | Latency ≤5 ms p99 | Timing budget (DEC-004) | scheduler + drivers | TEST_LOOP-TIMING (single-tick p99 = 3 µs over 20 000 ticks) | end-to-end latency needs a capture: **OPEN (H)**, HIL-3 timer | OPEN (H) |
| CTRL-001 | Cascade PID, anti-windup, saturation | ctrl_cascade.c | ctrl_cascade.c | unit + closed loop | TEST-CTRL-RATE (tracked 0.967/1.0 rad/s) | SIM-VERIFIED |
| CTRL-003 | Quad-X mixer, saturation minimization | mixer_quadx.c (DEC-008) | mixer_quadx.c | unit + closed loop | TEST-MIX-01/02; TEST-CTRL-SAT | SIM-VERIFIED |
| CTRL-005 | Disarm → motors zero | ctrl_set_armed gate | ctrl_cascade.c | unit | TEST-CTRL-DISARM | SIM-VERIFIED |
| EST-001 | Quaternion attitude estimate | est_attitude.c (DEC-008 convention) | est_attitude.c | unit + closed loop | TEST-EST-001/002/003 | SIM-VERIFIED |
| SEN-002 | Validation rejects NaN/range/jump | validation.c | validation.c | unit | TEST-VAL-01 | SIM-VERIFIED |
| SEN-003 | Calibration (gyro bias, accel 6-face) | calibration.c | calibration.c | unit | TEST-CAL-01 | SIM-VERIFIED |
| SEN-004 | Health timeout escalation | sensor_health.c | sensor_health.c | unit | TEST-HEALTH-01 | SIM-VERIFIED |
| SAF-030 | Battery thresholds (3.5/3.4/3.1 V/cell) | failsafe_sm.c | failsafe_sm.c | unit | TEST-SAF-BATT | SIM-VERIFIED |
| SYS-003 | RC loss failsafe ≤500 ms | failsafe SM | failsafe_sm.c + mission_sm.c RTL hook | unit + SIM | TEST-SAF-RC (timeout entry), TEST-NAV-RCLOSS (RTL) | SIM-VERIFIED (timing H-gated) |
| NAV-001 | Waypoints + auto RTL | mission_sm.c | mission_sm.c | SIM deterministic | TEST-NAV-SEQ | SIM-VERIFIED |
| NAV-003 | Takeoff/landing guards | mission_sm.c | mission_sm.c | SIM | TEST-NAV-SEQ | SIM-VERIFIED |
| NAV-004 | Geofence → RTL | mission_sm.c | mission_sm.c | SIM | mission geofence unit (fence branch) | SIM-VERIFIED |
| EST-002 | Altitude/vertical velocity fusion | est_alt.c | est_alt.c | SIM closed loop | TEST-CTRL-ALT (true/est 1.50 m) | SIM-VERIFIED |
| HW-006 | DShot600 interface | esc_dshot.c + motor_output.c | esc_dshot.c | unit (frames/CRC convention) | TEST-ESC-DSHOT/INTERLOCK; **Phase 22**: frames cross the HIL link and are re-decoded by an independent analyser (TEST-DSHOT-CAPTURE, 24000 frames, 0 CRC failures) | SIM-VERIFIED (H pending: real ESC analyser) |
| SYS-003 | RC loss failsafe ≤500 ms | Failsafe SM (DEC-006) | failsafe/rc_loss | TEST-SAF-RC (timeout entry), TEST-NAV-RCLOSS (RTL) | SIM fault injection: RC stop → failsafe ≤500 ms; timing on hardware **OPEN (H)** | SIM-VERIFIED (timing H-gated) |
| SYS-004 | Companion loss fallback ≤1 s | Companion link ICD (DEC-003); perception health window (DEC-013) | companion_link.c (heartbeat 1 s health, DEC-015); perception_fusion.c (ai_healthy ≤1 s) | TEST-COM-LINK; TEST-PERC-DEATH | COMMUNICATION_DESIGN.md §5 (heartbeat timeout → unhealthy, advisory only) | SIM-VERIFIED (physical UART H-gated) |
| HW-001 | STM32 F/H-series class | Component baseline (DEC-002) | HAL stm32 backend | R | DEC-002 | Approved |
| FW-001 | Shared app, no backend leaks | HAL boundary (DEC-001) | 02_firmware/hal | include-audit | `audit_requirements.py`: 53 algorithm files, 0 backend-header includes, enforced every run | MET (enforced) |
| SAF-040 | AI bounded to FC limits | Edge-AI boundary (DEC-007); avoidance clamp-last design (DEC-014) | obstacle_avoidance.c | TEST-SAF-AI; TEST-AV-CLAMP (adversarial 1000 m/s obstacle velocity, 0.05 m range → bounded) | OBSTACLE_AVOIDANCE.md §5 | SIM-VERIFIED (position/velocity controller integration pending lateral dynamics) |
| AI-001 | ≥10 Hz bounded-latency perception | Edge-AI architecture; virtual companion 25 Hz (hal_sim) | 03_edge_ai; perception_fusion.c | TEST-AI-*; TEST-PERC-* (link-side rates) | PERCEPTION_FUSION.md (25 Hz stream consumed at 50 Hz) | PARTIAL (SIM companion; real companion Phase 19) |
| SIM-001 | Virtual sensors + noise/bias/dropout/failure injection | hal/sim/hal_sim.c | hal_sim.c | SIM unit + scenario sweep | TEST-SIM-NOISE/FAULTS; scenario matrix (SIMULATION_VALIDATION.md §5) | SIM-VERIFIED (flow not modeled) |
| SIM-002 | Deterministic runs, bit-identical telemetry | xorshift32 PRNG + fixed step | hal_sim.c + main_sim.c | double-run diff | seed 1234, 8000 ticks, diff = 0 bytes | SIM-VERIFIED |
| SIM-003 | Rigid-body physics adequate for control validation | minimal rigid body (DEC-009); horizontal dynamics + drag + wind (DEC-017) | hal_sim.c | open/closed loop | TEST-CTRL-HOV/RATE/ALT; TEST-SIM-NOISE; TEST-SIM-LATERAL (6.00 m waypoint), TEST-SIM-DRAG, TEST-SIM-WIND (1.5 m/s rejected to 0.00 m), TEST-AVOID-FLOWN (clearance 1.11 m) | SIM-VERIFIED (model not HW-identified) |
| SIM-004 | Scripted + replayable fault scenarios | scenario presets + FC_SIM_TICKS/SEED | main_sim.c | scenario matrix | 7 scenarios, 0 deadline misses (SIMULATION_VALIDATION.md §5) | SIM-VERIFIED |
| EST-003 | Position/velocity from GNSS + gating | est_position.c (Phase 15 subset) | est_position.c | SIM unit | TEST-EST-003 (track 50.50 vs 50.00 m < 1.5 m) | PARTIAL (horizontal vehicle dynamics stub) |
| EST-004 | Outlier rejection before fusion | 25 m innovation gate | est_position.c | SIM unit | TEST-EST-004 (500 m outlier rejected) | SIM-VERIFIED |
| SAF-003 | No sensor failure → uncommanded motor surge | zero-output path + IMU health monitor | ctrl_cascade.c, failsafe_sm.c, app_main.c | SIM fault injection | TEST-SIM-IMUFAIL (z 1.50→0.00 m, peak 1.50, no surge) — regression after 46 m climb defect found by Phase 15 sweep | SIM-VERIFIED |
| SAF-001 | Failsafe hierarchy RC>IMU>battery>geofence>companion | level/latch failsafe SM (DEC-012) | failsafe_sm.c | SIM fault combination | TEST-SAF-PRIORITY (RC wins over battery-critical; RC restore clears; battery latched→RTL); geofence via TEST-NAV-FENCE; companion tier: TEST-PERC-DEATH/TEST-AI-LOSS (no escalation from perception loss, DEC-007) | SIM-VERIFIED |
| SAF-001 | Failsafe hierarchy, now flyability-ranked (Phase 24, DEC-021) | `failsafe_action()` + `attitude_authority_lost()`; action table in FAILURE_MODE_AND_EFFECTS_ANALYSIS.md | failsafe_sm.c, app_main.c | unit + app scenario + on-the-wire | TEST-SAF-ACTION-FEASIBLE (reports RC_LOSS while action is MOTOR_STOP); TEST-SAF-DEADLINE-STORM; `imu_rc_loss` scenario asserted in the runner (`safety: failsafe=1 action=4`) | SIM-VERIFIED |
| SAF-002 | Each bounded action exercised: hover, RTL, land, motor stop | flyability-based action model (DEC-021) | failsafe_sm.c, mission_sm.c (`mission_request_rtl`), app_main.c action switch | unit + scenario + regression step | TEST-SAF-ACTION-FEASIBLE, TEST-SAF-DEADLINE-STORM, `imu_rc_loss` (MOTOR_STOP), `rc_loss`/`battery_low` (RTL/LAND), GS schema-v2 step reports `action=MOTOR_STOP` | SIM-VERIFIED |
| SAF-004 | Watchdog / control-loop deadline enforcement | IWDG emulation with real semantics (DEC-019) | hal/hil (IWDG), failsafe_sm.c deadline counter | unit + host-HIL negative test | TEST-SAF-DEADLINE-STORM (deadline storm forces MOTOR_STOP); host-HIL watchdog interlock (exit 3, wdt_expiries=1) | SIM-VERIFIED (real IWDG **OPEN (H)**) |
| SAF-005 | Arming interlock; arming authority is FC/RC-only | arming gate + `cmd_gate` refusal of the whole arming class | ctrl_set_armed, cmd_gate.c | unit + gate test | TEST-ARM-INTERLOCK; TEST-CMDGATE-ARMING (10/10 arming-class frames refused unconditionally) | SIM-VERIFIED (live transport **OPEN**) |
| SYS-004 | Companion loss fallback ≤1 s | companion link ICD (DEC-003); perception health window (DEC-013) | companion_link.c (heartbeat 1 s health, DEC-015); perception_fusion.c (ai_healthy ≤1 s) | TEST-COM-LINK; TEST-PERC-DEATH | COMMUNICATION_DESIGN.md §5 (heartbeat timeout → unhealthy, advisory only) | SIM-VERIFIED (physical UART H-gated) |
| SYS-004 | Companion loss is advisory, never a failsafe tier (Phase 24) | tier set in `failsafe_action()` deliberately excludes companion (DEC-007) | failsafe_sm.c, perception_fusion.c | unit + app scenario | TEST-SAF-COMPANION-NOT-FAILSAFE; `ai_loss` scenario (perception → TOF_ONLY, flight continues) | SIM-VERIFIED |
| PERC-001 | Bounded obstacle picture w/ confidence + timestamps (Phase 17) | perception_fusion.c (DEC-013) | perception_fusion.c | unit | TEST-PERC-FUSED/ROTATION (16-entry cap, source tags, body+stability frame) | SIM-VERIFIED |
| PERC-002 | Stale/invalid sets excluded (>200 ms ICD-02); out-of-band ToF rejected | perception validity windows (DEC-013) | perception_fusion.c | unit | TEST-PERC-STALE/INVALID (stale AI excluded; conf<0.5 dropped; bad ToF rejected) | SIM-VERIFIED |
| PERC-003 | Degraded modes NONE/TOF_ONLY/AI_ONLY/FUSED; loss of all sources must not failsafe (DEC-007) | perception mode machine | perception_fusion.c + app_main.c | unit + app-level scenario | TEST-PERC-DEATH; ai_loss scenario (advisory degradation, flight continues) | SIM-VERIFIED |
| PERC-004 | FC sensor outranks AI geometry on conflict (SAF-041) | forward-cone conflict rule (DEC-013) | perception_fusion.c | unit | TEST-PERC-CONFLICT (10 m AI vs 4 m ToF → 4 m published, conf 0.45, src AI|TOF); TEST-PERC-TOF-SYNTH | SIM-VERIFIED |
| AVD-001 | Collision risk: cone threat + brake curve + hard stop + bounded retreat | brake/stop/retreat policy (DEC-014/018) | obstacle_avoidance.c | unit + end-to-end flight | TEST-AV-SLOW/STOP_AND_RETREAT/LATERAL; TEST-AVOID-FLOWN (flown: peak 6.06 m, min clearance 1.11 m, RETREAT) | SIM-VERIFIED |
| AVD-002 | Avoidance commands clamped to FC limits (SAF-040) | clamp-last publish path (DEC-014) | obstacle_avoidance.c | unit (adversarial) | TEST-AV-CLAMP; TEST-POS-LIMITS (AF limits hold for absurd inputs) | SIM-VERIFIED |
| AVD-003 | Deterministic degraded behavior; no failsafe from avoidance | inactive-on-invalid policy (DEC-014) | obstacle_avoidance.c + app_main.c | unit + app scenario | TEST-AV-OFF; TEST-AV-APP; TEST-AVOID-FLOWN (mission not aborted, no failsafe) | SIM-VERIFIED |
| CTRL-004 | Position/velocity control stage closed and disturbance-rejecting | position controller (DEC-017) | ctrl_position.c | closed loop | TEST-SIM-LATERAL/WIND/POS-LIMITS (SIM_TUNING_RECORD.md) | SIM-VERIFIED (gains H-gated) |
| COM-001 | RC input, protocol failsafe, invalid-frame handling | ICD-04 CRSF primary / SBUS fallback | rc_protocol.c | unit | TEST-COM-CRSF/SBUS (CRC reject, normalization, flags failsafe) | SIM-VERIFIED (radio H-gated) |
| COM-002 | MAVLink v2 telemetry, degraded link tolerated | ICD-03 subset + telemetry records; GS stream decoder (DEC-016) | mavlink2.c, telemetry.c, gs_protocol.py | unit + end-to-end | TEST-COM-MAVLINK/TELEMETRY; GS self-test + firmware-log decode (720 frames, 0 CRC errors) | SIM-VERIFIED |
| COM-003 | Companion link: CRC, sequence, heartbeat, versioned schema | DEC-015 ICD-02 codec + link task | icd02_frame.c, companion_link.c | unit + app byte path | TEST-COM-ICD02/OBSTACLE-SET/LINK/BYTEPATH; TEST-COM-TELEMETRY (SCHEMA_VER) | SIM-VERIFIED |
| COM-004 | FC master of safety data; GS cannot arm | failsafe state in every status record/FC_STATE; GS command validation (DEC-016) | telemetry.c, companion_link.c, gs_commands.py | unit + end-to-end CLI | TEST-COM-FCSTATE/TELEMETRY; GS self-test (6 arming-class commands rejected); battery_low log raises CRITICAL + --strict exit 1 (GROUND_STATION.md §5) | SIM-VERIFIED (no command transport yet; FC-side ignore-arming on receipt still to prove when built) |
| SAF-031 | Battery RTL latch + land thresholds | hysteresis + band rule (DEC-012) | failsafe_sm.c | SIM boundary | TEST-SAF-BATT/PRIORITY (3.39 latches; 3.05 first-seen → LAND); battery_low→LAND scenario | SIM-VERIFIED |
| TEST-001 | Automated regression on every change, 0 failures | run_regression.sh | 08_testing/regression_tests | runner | exit 0 (script status): build + 2741/2741 + determinism + 8 scenarios (incl. imu_rc_loss) + GS self-test (18 tests) + GS telemetry schema-v2 decode step + GS end-to-end decode + 6 executed host-HIL steps + requirement/architecture audit + documentation audit (step 10: index both directions, links, markers, 9 derived + 11 observed facts, quoted prose numbers) + V&V review gate (step 11: 83 requirements classified, 23 critical verified or explicitly blocked, 0 failed, 7/7 negative cases) + release manifest audit (step 12: tree digest + 10 pinned items + 4 probed-absent gates, 7/7 negative cases) | MET (SIM + host HIL) |
| TEST-002 | H tests marked, explicit skip message | runner SKIP lines | run_regression.sh | runner output | 2 SKIP lines naming the missing toolchain/board (HIL-1..6 stay H-gated; the host rig does NOT satisfy them) | MET |
| TEST-003 | Coverage: math/filters/PID/fusion/mixer/protocol/SM | test_main.c (2741 checks) | test_main.c | suite | all domains ≥1 case (TEST_GENERATION.md §2, §15, §16) | MET (SIM) |
| HIL-0a..0f | Critical-interface evidence on the host rig (link integrity, sensor-rate fidelity, DShot bit stream, fault-injection latency, CRC robustness, watchdog interlock) | DEC-019 HIL backend + host rig | common/protocols/hil_link.{c,h}, hal/hil/*, 07_simulation/hardware_in_the_loop/hil_rig.c, dshot_analyzer.py | host HIL + unit | TEST-HIL-* (8 cases), 6 runner steps (HIL_DESIGN.md §5) | MET (host) — **not** hardware evidence |
| HIL-1..6 | Rig against the real board: 60 s run, nav equivalence, loop rate/latency capture, sensor correlation, ESC analyser comparison, bus fault injection | Phase 15/22 design | hal/stm32 (skeleton) | hardware | none — no board, no toolchain | Open (H) |
| TEST-004 | Every requirement ≥1 traceability row + a status row | this matrix + VERIFICATION_REPORT.md | audit_requirements.py | audit (wired into run_regression.sh step 7) | 83 baseline IDs → 83 status rows + 83 traceability rows, enforced each run | MET |
| SIM-001 | hal/sim/sim_model.c virtual sensors | TEST-SIM-* per fault class (noise, bias, dropout, stale, gnss_loss) | MET (SIM) |
| SAF-003 | app_main.c zero-output path + est_alt gating | TEST-SIM-IMUFAIL, imu_dropout -> ABORT (scenario + runner) | MET (SIM) |

Full per-requirement rows are appended as phases implement and verify them. Pre-baseline placeholders SYS-001/002 (old wording) superseded by REQUIREMENTS_DOMAINS.md definitions; AI-001 updated to measurable form.

## Baseline coverage rows (Phase 23)

Every ID of `REQUIREMENTS_DOMAINS.md` (83) appears above or below. Full status,
levels and evidence: `08_testing/VERIFICATION_REPORT.md`. Completeness is enforced by
`08_testing/audit_requirements.py` (TEST-004), which fails the regression if an ID has
no row here and no status there.

| ID | Implementation | Test / evidence | Status |
|---|---|---|---|
| SYS-001 | app_main.c scheduler (TICK_HZ 1000) | TEST_LOOP-TIMING (host p99 3 us) + deadline monitor | PARTIAL: software verified; MCU rate OPEN(H) |
| SYS-004 | companion_link.c 1 s heartbeat | ai_loss scenario (runner), perception TOF_ONLY degradation | MET (SIM) |
| SYS-005 | power records + failsafe_sm.c thresholds | POWER_BUDGET.md, TEST-SAF-BATT | MET (analysis); pack curve OPEN(H) |
| SYS-006 | parameters.c flash image | TEST_PARAM-POWERCYCLE | MET (SIM) |
| SYS-007 | power/mass records | POWER_BUDGET.md | MET (analysis); endurance OPEN(H) |
| HW-001 | PCB_DESIGN_RECORD.md (DEC-002) | design review | MET (review); board OPEN(H) |
| HW-002 | hal_imu_read + sensor_hub | SIM 1 kHz stream (HIL age_max 0 us) | MET (review+SIM) |
| HW-003 | est_alt.c | 60 s drift mean 0.071 m | MET (analysis+SIM) |
| HW-004 | hal_gnss_read | SIM 10 Hz + 3 s TTFF (age_max 99 ms) | MET (review+SIM) |
| HW-005 | hal_tof_read | SIM 25/50 Hz (age_max 39/19 ms) | MET (review+SIM) |
| HW-006 | esc_dshot.c + motor_output.c | TEST-DSHOT-CAPTURE, HIL capture 24000 frames 0 CRC bad | MET (SIM) for DShot600; PWM fallback PARTIAL (not implemented) |
| HW-007 | fc_battery_sample_t + failsafe battery | TEST-SAF-BATT, battery_low scenario | MET (SIM); ADC/shunt accuracy OPEN(H) |
| HW-008 | companion_link.c (DEC-003) | TEST-COM-ICD02/LINK, HIL link byte path | MET (SIM); FC power-cycle PARTIAL (not implemented) |
| HW-009 | SCHEMATIC_REVIEW_CHECKLIST.md | design review | OPEN(H): checklist exists, no board |
| HW-010 | PCB_DESIGN_RECORD.md | design review | OPEN(H): no PCB layout |
| FW-002 | app_main.c scheduler + watchdog | TEST_LOOP-TIMING, HIL watchdog negative test | MET (SIM) |
| FW-003 | hal_interfaces.h typed status + sensor_health | imu_dropout/rc_loss/gnss_loss scenarios | MET (SIM); bus faults OPEN(H) |
| FW-004 | whole control path | audit_requirements.py: 0 allocation sites | MET (audit) |
| FW-005 | parameters.c magic+CRC | TEST_PARAM-POWERCYCLE | MET (SIM) |
| FW-006 | telemetry.c + GS byte log | deadline_misses=0 with logging on | PARTIAL: no overload-drop policy |
| FW-007 | 02_firmware/bootloader/ (placeholders) | none | OPEN: no bootloader code |
| SEN-001 | sensor_hub.c pipeline | TEST-SEN-HUB stages | MET (SIM) |
| SEN-002 | validation.c | TEST-VAL-* rejection classes | MET (SIM) |
| SEN-003 | calibration.c | TEST-CAL-* | MET (SIM) |
| SEN-004 | sensor_health.c counters | TEST-HEALTH escalation | MET (SIM) |
| SEN-005 | all drivers | TEST_LOOP-TIMING max 23 us | MET (SIM) |
| EST-001 | est_attitude.c complementary filter | 15 deg recovery to 0.01 deg | PARTIAL: no EKF, no magnetometer (deviation recorded) |
| EST-002 | est_alt.c | 60 s drift mean 0.071 m | MET (SIM) |
| EST-004 | est_position.c innovation gate | TEST-EST-003/004 | MET (SIM) |
| EST-055 | est_position.c health + app_main response | TEST-EST-HEALTH (1018 ms) | MET (SIM) |
| EST-006 | fc_types.h timestamps + sensor_hub consumers | code audit | MET (review) |
| CTRL-001 | ctrl_cascade.c + ctrl_position.c | wind rejection 0.00 m, tilt/limits tests | MET (SIM) |
| CTRL-002 | app_main.c divides + deadline monitor | deadline_misses=0 in 7 scenarios | MET (SIM); hardware rates OPEN(H) |
| CTRL-003 | mixer_quadx.c | TEST-MIX saturation cases | MET (SIM) |
| CTRL-004 | ctrl_set_armed gates | TEST-ARM-INTERLOCK | MET (SIM) |
| CTRL-005 | motor_output.c STOP frames | TEST-ARM-INTERLOCK, TEST-CTRL-DISARM | MET (SIM) |
| CTRL-006 | parameters.c | parameters read at init, no mid-loop path | PARTIAL: no runtime update path to test |
| NAV-001 | mission_sm.c | full WP->RTL->LAND->DONE run | MET (SIM) |
| NAV-002 | obstacle_avoidance.c | TEST-AVOID-FLOWN (clearance 1.11 m) | MET (SIM) |
| NAV-003 | mission_sm.c takeoff/land | scenario land + DONE | MET (SIM) |
| NAV-004 | mission_sm.c geofence | TEST-NAV-FENCE | MET (SIM) |
| COM-001 | rc_protocol.c | TEST-COM-CRSF/SBUS | MET (SIM) |
| GS-001 | ground_station.py CLI | end-to-end decode of the real FC log | MET (SIM) |
| GS-002 | gs_commands.py SET_PARAM/SAVE_PARAMS + **FC-side** `cmd_gate.c` envelope clamp | GS self-test bounds case; TEST-CMDGATE-ARMING (SET_PARAM clamped to FC-owned envelopes, NaN/unknown id refused) | PARTIAL: FC-side receiver gate now exists and is proven; still no transport read-back |
| GS-003 | gs_commands.py EMERGENCY_STOP + RC failsafe; FC gate always accepts EMERGENCY_STOP | GS self-test emergency-stop case; TEST-ARM-INTERLOCK; TEST-CMDGATE-ARMING (emergency accepted, arming class refused) | PARTIAL: validated on both sides; transport open |
| GS-003 | Telemetry schema v2 carries the action, so a motor stop cannot read as an RTL (Phase 24, DEC-021) | telemetry.c encode/parse; gs_protocol.py decode; classify() CRITICAL SAFETY_ACTION | TEST-TELEM-ACTION (v2 round-trip, v1 length refused, HEARTBEAT CRITICAL); GS `test_safety_action_on_the_wire` + `test_status_record_v1_length_is_refused`; runner step "ground station telemetry schema v2" (240 records) | MET (SIM) |
| SIM-004 | runner scenario matrix | 8 checks with asserted sequences (nominal, rc_loss, imu_dropout, battery_low, gnss_loss, noiseless, ai_loss, imu_rc_loss) | MET (SIM) |
| SAF-001 | failsafe_sm.c | TEST-SAF-PRIORITY + scenarios | MET (SIM) |
| SAF-002 | failsafe_sm.c `failsafe_action()` + app_main.c action switch (DEC-021) | TEST-SAF-ACTION-FEASIBLE, TEST-SAF-DEADLINE-STORM, `imu_rc_loss` scenario, GS schema-v2 step | MET (SIM) |
| SAF-004 | hal/hil IWDG + failsafe_sm.c deadline counter | TEST-SAF-DEADLINE-STORM; host-HIL watchdog interlock (exit 3) | MET (SIM + host HIL); real IWDG OPEN(H) |
| SAF-005 | ctrl_set_armed + cmd_gate.c arming-class refusal | TEST-ARM-INTERLOCK; TEST-CMDGATE-ARMING | MET (SIM) |
| COM-004 | telemetry.c schema v2 (failsafe **and** action), cmd_gate.c FC-side authority boundary | TEST-TELEM-ACTION; TEST-CMDGATE-ARMING; runner GS schema-v2 step | PARTIAL: validation + FC-side gate exist and are proven; live command transport still open |
| SAF-002 | failsafe_sm.c actions | hover/RTL/land/stop cases | MET (SIM) |
| SAF-004 | deadline counter + hal_hil IWDG emulation | HIL watchdog negative test (exit 3) | MET (SIM); real IWDG OPEN(H) |
| SAF-005 | ctrl_set_armed + motor_output | TEST-ARM-INTERLOCK | MET (SIM) |
| SAF-030 | failsafe battery thresholds | TEST-SAF-BATT | MET (SIM) |
| SAF-031 | failsafe battery latch | TEST-SAF-BATT latch | MET (SIM) |
| SAF-040 | obstacle_avoidance.c clamp-last | TEST-AVOID-CLAMP adversarial | MET (SIM) |
| SAF-041 | perception_fusion.c conflict rule | TEST-PERC-CONFLICT | MET (SIM) |
| AI-002 | ICD-02 payload contract (DEC-007/014) | code review + clamp test | MET (review) |
| AI-003 | none (no model on host) | none | OPEN: no benchmark, nothing fabricated |
| AI-004 | companion_link.c health + ICD-02 HEALTH | TEST-PERC-DEATH, ai_loss scenario | MET (SIM) |
| AI-005 | none | none | OPEN: no dataset/model |
| PERC-001 | perception_fusion.c | TEST-PERC-FUSED structure/bounds | MET (SIM) |
| PERC-002 | perception_fusion.c validity gates | TEST-PERC-STALE/INVALID/CONF-GATE | MET (SIM) |
| PERC-003 | perception_fusion.c modes | TEST-PERC-DEATH + ai_loss scenario | MET (SIM) |
| PERC-004 | perception_fusion.c conflict + synthesis | TEST-PERC-CONFLICT/TOF-SYNTH | MET (SIM) |
| AVD-001 | obstacle_avoidance.c brake curve | TEST-AV-SLOW/STOP/RETREAT | MET (SIM) |
| AVD-002 | obstacle_avoidance.c clamp-last | TEST-AV-CLAMP | MET (SIM) |
| AVD-003 | obstacle_avoidance.c inactive path | TEST-AV-OFF + TEST-AVOID-FLOWN | MET (SIM) |
| MFG-001 | `12_manufacturing/RELEASE_MANIFEST.json` (16 items), `bom/CONTROLLED_BOM.csv`, `parameters/params_defaults.{bin,json}`, `fabrication/FABRICATION_DATA_SPEC.md`, `pcb_inspection/INSPECTION_CRITERIA.md`, `assembly/ASSEMBLY_SEQUENCE.md`, `PROGRAMMING_AND_PRODUCTION_TEST.md`, `MANUFACTURING_RELEASE_CHECKLIST.md`, `tools/{dump_param_defaults.c,audit_release_manifest.py,selftest_audit.py}` | runner step 8: `audit_release_manifest.py` (7 check groups) + `selftest_audit.py` (16/16 negative cases) + parameter-default regeneration against pinned digests | PARTIAL — software half complete and audited; fabrication data, firmware image and per-unit records gated (`EDA_TOOLCHAIN`/`HW-010`/`STM32_TOOLCHAIN`/`BOARD`), gates probed every run |
| OPS-001 | `13_release/RELEASE_MANIFEST.json` (RC-2: source tree digest + 10 digest-pinned items), `13_release/RELEASE_NOTES.md`, `13_release/tools/audit_release.py` | runner step 12: manifest re-derived every run (tree digest, item digests, 4 probed-absent gate artifacts, OPS-001 row rule) + 7/7 negative cases | PARTIAL — software/evidence release record exists and is audited; operational (flyable) release gated on `BOARD`/`STM32_TOOLCHAIN` |
