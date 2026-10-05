# Failure Mode and Effects Analysis (FMEA)

Phase 24, 2026-10-04. Revision A.

## Scope and honesty statement

Covers the vehicle-level failure modes of the autonomous drone flight controller
and edge-AI obstacle avoidance system: sensing, estimation, control, navigation,
failsafe, actuation, command/telemetry, companion computer and edge-AI perception.

**Evidence class for every row below is SIM or host-HIL.** There is no STM32
board, no PCB, no bench rig and no flight test in this workspace, so nothing in
this document may be cited as hardware evidence. Rows whose verification can only
be closed on hardware are marked `OPEN (H)` and are carried into
`08_testing/VERIFICATION_REPORT.md` as open requirement rows rather than being
quietly closed by argument. This is DEC-001 plus DEC-020 applied to safety:
absence of a rig is a status, not a footnote.

## Risk methodology (project-local, not a certification method)

Numeric scores 1–5 (5 worst). `RPN = S x O x D`.

| Score | Severity (S) | Occurrence (O) | Detectability (D) |
|---|---|---|---|
| 1 | no vehicle effect | not credible in the design as built | always detected before any effect |
| 2 | recoverable annoyance | once per 1000+ flight hours | detected with wide margin |
| 3 | mission abort or property damage | once per 100–1000 h | detected, margin < 2x |
| 4 | uncontrolled descent / injury risk | once per 10–100 h | detected only by a secondary path |
| 5 | uncontrolled flight, airframe loss or injury | once per < 10 h | not detected before effect |

**RPN bands (project-local):** 1–24 LOW, 25–59 MEDIUM, 60–99 HIGH, ≥100 CRITICAL.
Any row with S = 5 is treated as CRITICAL regardless of RPN.

This is deliberately *not* ARP4761/DO-178C/ISO 26262 methodology — the vehicle is
a research demonstrator with no airworthiness claim, and claiming a
certification-grade rating scheme from a simulation would be the exact kind of
unearned confidence DEC-020 forbids. Re-scoring with a real methodology is
`OPEN` (MFG-001 / Phase 25 boundary).

## FMEA table

| ID | Function | Failure mode | Cause | Local effect | Vehicle effect | Detection | Mitigation (as built) | S | O | D | RPN | Band | Residual risk | Verification (evidence class) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FM-01 | IMU sampling | IMU produces no new samples | SPI/UART stall, sensor brownout, wire fault | stale samples, age grows | attitude unobservable | `sensor_hub` age timeout -> `FC_FAILSAFE_IMU` | stale sample never delivered as valid; zero actuator outputs; mission ABORT (DEC-011) | 5 | 3 | 2 | 30 | MEDIUM | motor stop, no attitude hold; **H-open** (no real sensor to unplug) | `TEST-SIM-IMUFAIL` scenario, unit test, `imu_rc_loss` scenario — **SIM**; IWDG/IMU on real bus **OPEN (H)** |
| FM-02 | IMU sampling | IMU reports plausible-but-wrong data (bias/step) | gain error, mount slip, EMI | estimator converges to a wrong attitude | gradual divergence, slow control failure | innovation/attitude-consistency check | no independent attitude reference exists (no magnetometer, no optical-flow lock) — **not detectable** | 5 | 4 | 5 | 100 | **CRITICAL** | undetected until control error is visible; EST-001 deviation | EST-001 (complementary filter + gyro-bias, **PARTIAL**), `CONTROL_VALIDATION.md` — **SIM** |
| FM-03 | RC input | RC link lost | radio fade, antenna, operator distance | no fresh RC frames | loss of pilot command | frame age watchdog (`RC_TIMEOUT`) | `FC_FAILSAFE_RC_LOSS` -> RTL; mission cannot arm without RC | 4 | 3 | 1 | 12 | LOW | autonomous RTL instead of manual recovery; no telemetry at distance — **H-open** | `rc_loss` scenario + host-HIL fault injection (latency 0 us) — **SIM/host HIL**; real radio **OPEN (H)** |
| FM-04 | RC input | RC frame structurally valid but semantically stuck (throttle high) | stuck trim, wrong stick mapping, reversed channel | valid frames accepted | unintended climb on arm | **no cross-check available in SIM (no pilot present)** | arming gate requires throttle-low (SAF-005) — only catches the arm-time case | 4 | 3 | 4 | 48 | MEDIUM | a mid-flight stick fault is indistinguishable from pilot intent | `TEST-ARM-INTERLOCK` — **SIM**; bench RC rig **OPEN (H)** |
| FM-05 | Power | Battery depletion below RTL band | normal flight time, cell imbalance | min cell voltage falls | forced return/landing | per-cell voltage monitor + current integration | staged thresholds 3.5 warn / 3.4 RTL / 3.1 land, hysteresis, latch below critical (SAF-030/031) | 4 | 3 | 1 | 12 | LOW | a healthy-looking pack can sag in flight faster than it is measured — **H-open** | `battery_low` scenario, threshold/latch unit tests — **SIM**; real pack curve **OPEN (H)** |
| FM-06 | Power | Voltage collapse / brownout under load | ESC inrush, damaged pack, regulator fault | rail drops out | sudden loss of control, uncontrolled descent | **not detectable after the fact** — a brownout is silent | none available; only pre-emptive battery staging | 5 | 2 | 5 | 50 | MEDIUM | uncontrolled descent; no ride-through, no flight log persistence | `POWER_BUDGET.md`, `POWER_ARCHITECTURE.md` analysis only; SYS-005 **PARTIAL (analysis) + OPEN (H)** |
| FM-07 | Position | GNSS fix lost (sky, jam, antenna) | urban canyon, interference, cold start | no horizontal reference | no RTL possible | GNSS age/fix-status monitor | `FC_FAILSAFE_ESTIMATOR` -> LAND in place (position degrades, attitude survives) | 4 | 3 | 1 | 12 | LOW | lands instead of returning; no GNSS re-acquisition hold | `gnss_loss` scenario + Phase 23 escalation test — **SIM**; real GNSS outage **OPEN (H)** |
| FM-08 | Position | GNSS reports a wrong fix (multipath) | multipath, spoofing, bad ephemeris | plausible but wrong position | vehicle flies to the wrong place confidently | no second position source | none — EST-003 is GNSS-only (optical flow is a NOT_READY stub) | 5 | 3 | 4 | 60 | **HIGH** | undetected wrong-fix flight | EST-003 **PARTIAL**; geofence is the only containment |
| FM-09 | Position | Estimator diverges (attitude/altitude) | soft fault compounding in the filter | state leaves physical bounds | control setpoint leaves envelope | per-axis plausibility clamp applied last | clamped output, so a diverged estimate cannot command a divergent setpoint | 4 | 3 | 2 | 24 | LOW | degraded control quality, not loss of control | estimator unit tests, clamp tests — **SIM** |
| FM-10 | Navigation | Mission target outside the geofence | bad mission load, operator error, wind drift | violation flag | vehicle driven toward a boundary | `mission_geofence_violated()` -> `FC_FAILSAFE_GEOFENCE` -> RTL | mission validation in both GS and FC command gate (50 m); violation forces RTL | 4 | 2 | 1 | 8 | LOW | relies on the fence itself being right | geofence unit tests + `cmd_gate` range rejection — **SIM** |
| FM-11 | Companion computer | Companion process/link dies | crash, power loss, UART fault | no ICD-02 frames | loss of AI obstacle perception | ICD-02 heartbeat window (SYS-004) | **advisory by design (DEC-007)**: perception degrades to TOF_ONLY, flight continues, **no failsafe** | 3 | 3 | 1 | 9 | LOW | obstacle avoidance capability is reduced, never safety-critical | `ai_loss` scenario asserted in runner — **SIM**; HW-008 power cycle **OPEN** |
| FM-12 | Edge AI | Model emits a wrong/nonsense obstacle set | out-of-distribution input, quantisation, bad preprocessing | spurious obstacles | vehicle slows, stops or retreats for nothing | per-detection confidence gate 0.5; FC ToF conflict rule SAF-041 | bounded output, clamp-last (SAF-040); AI can only reduce speed, never command attitude | 2 | 4 | 2 | 16 | LOW | nuisance behaviour only; **no model or dataset exists yet**, so this is a design-bound not a measured rate | Perception/avoidance unit tests with synthetic obstacle sets — **SIM**; AI-003/AI-005 **OPEN** |
| FM-13 | Actuation | One motor/ESC fails (no thrust or runaway) | mechanical, ESC failure, prop damage | asymmetric thrust | uncontrolled roll/yaw, tumble | motor feedback where available — **not present in this build** | mixer saturation bounds the demand; nothing detects the asymmetry | 5 | 2 | 5 | 50 | MEDIUM | uncontrolled descent within one rotor period; no motor-health telemetry | `esc_dshot.c` encoder tests, mixer saturation tests — **SIM**; motor-out test **OPEN (H)** |
| FM-14 | Actuation | Motor direction / mixer configuration wrong | wrong install, config error | wrong torque map | instability or immediate flip on arm | no runtime check | build-time convention agreement only (DEC-008) | 5 | 2 | 3 | 30 | MEDIUM | caught in bench/SIM, not in flight | mixer + attitude unit tests — **SIM**; bench hover **OPEN (H)** |
| FM-15 | Scheduler | Control loop overruns its deadline | blocking I/O, unbounded loop, RTOS priority inversion | control rate drops | control authority degrades, then is lost | per-cycle deadline counter (`deadline_misses`) | none in-loop; the counter is diagnostic only | 5 | 2 | 3 | 30 | MEDIUM | no enforced shedding or degraded-mode scheduling | deadline counter asserted `deadline_misses=0` in every regression scenario; measured mean 0.5 us / max 23 us against a 900 us budget — **SIM**; SYS-001/002 need an MCU capture (HIL-3) — **OPEN (H)** |
| FM-16 | Watchdog | IWDG expires (genuine or spurious) | firmware wedge, flash stall, clock fault | FC resets in flight | reboot mid-flight, motors at reset default | IWDG expiry counter | reset-time output safe state (motors stopped); STM32 backend path unverified | 5 | 2 | 2 | 20 | LOW | a reset is itself a loss of control; no in-flight restart state recovery | host-HIL watchdog interlock negative test (exit 3, `wdt_expiries=1`) — **host HIL**; real IWDG **OPEN (H)** |
| FM-17 | Command link | Malformed / forged command frame | bit errors, wrong protocol version, hostile input | rejected frame | **no unsafe effect — this is the intended outcome** | magic/version/length/CRC16 check before any dispatch | FC-side command gate (`cmd_gate.c`): arming class rejected unconditionally, mode whitelist, parameter clamping to FC-owned envelopes, NaN refused, waypoint bounds | 5 | 4 | 1 | 20 | LOW | a rejected frame costs availability, never authority | `test_command_gate_arming_class_refused` (10 arming frames refused, CRC/length/version/NaN/param/waypoint bounds) — **SIM** |
| FM-18 | Command link | Valid but out-of-envelope parameter from GS | operator error or older GS | clamped or rejected | none if clamped | envelope comparison in the gate | clamp to FC-owned min/max; unknown parameter id refused | 3 | 4 | 1 | 12 | LOW | clamped, not silently applied | same gate test + GS `validate_command` bounds test — **SIM** |
| FM-19 | Ground station | GS shows the wrong safety state | schema drift (exactly what Phase 24 fixed) | operator misinformed | operator believes the vehicle is RTL when it is falling | versioned record, refusal of unknown schema | telemetry **schema v2** carries the *action* separately from the *failsafe* (DEC-021); unknown versions are refused, not guessed | 5 | 3 | 1 | 15 | LOW | none once action is on the wire | `test_safety_action_on_the_wire`, `test_status_record_v1_length_is_refused`, GS self-test 18 tests, runner step "ground station telemetry schema v2" (240 records decoded) — **SIM** |
| FM-20 | Config | Stored parameters corrupted | flash wear, brownout during write, CRC bug | bad config image | wrong limits, e.g. a raised geofence or a lowered RTL band | image magic + CRC check | both a magic-destroyed and a CRC-broken image fall back to safe defaults (SAF-030 restored) | 4 | 2 | 1 | 8 | LOW | safe defaults are coarser than the flown configuration | `TEST_PARAM-POWERCYCLE` — **SIM**; flash endurance **OPEN (H)** |
| FM-21 | Software | Control-loop defect (gain sign, double-counted thrust, missing integral) | design/derivation error | wrong or unstable response | oscillation, divergence, crash | unit + scenario regression | regression suite is executable CI: **9 such defects were found and fixed at Phases 15, 16, 21, 22 and 24** by this exact mechanism | 5 | 4 | 2 | 40 | MEDIUM | the class is never closed; each fix only raises the rate of detection | 2741/2741 checks + 23-step runner (exit 0) — **SIM/host HIL** |
| FM-22 | Software | Runtime flash/parameter write stalls the loop | non-atomic write, long disable-interrupt region | one missed control cycle | single-cycle control transient | deadline counter | no deferral to a background context — FW-007 bootloader **OPEN**, FW-006 overload-drop **OPEN** | 4 | 3 | 2 | 24 | LOW | a single missed cycle at 1 kHz is survivable; a repeated stall is not | deadline counter assertions — **SIM**; **OPEN (H)** |

## Critical and high rows requiring explicit handling

Four rows carry S = 5. None of them is closed by evidence available in this
workspace, and the safety case must say so rather than imply otherwise.

| Row | Why it stays open | What would close it |
|---|---|---|
| FM-02 (undetectable wrong IMU) | there is no second attitude reference in the design | magnetometer or optical-flow-aided attitude + bench sensor-fault tests (**EST-001 is PARTIAL for exactly this reason**) |
| FM-08 (wrong GNSS fix) | position is single-source; optical flow is a stub | optical-flow/VIO fusion (EST-003 PARTIAL) + GNSS fault-injection on real hardware |
| FM-06 (brownout) | cannot be detected after the fact and there is no ride-through | real pack + ESC current measurement, brownout injection on the bench (**SYS-005 OPEN (H)**) |
| FM-13 (motor/ESC loss) | no motor feedback exists in this build | motor-current telemetry or ESC telemetry + motor-out bench test |

FM-15, FM-21, FM-22 share one honest qualifier: **the evidence is SIM/host-HIL
timing and logic, not MCU timing.** The 900 µs budget and the 0.5 µs measured mean
come from a host CPU running the same application; an STM32 at 168 MHz with real
DMA and real bus contention is a different machine. SYS-001/SYS-002 remain
`OPEN (H)` for that reason and HIL-3 is the designated experiment.

## Defects found by this analysis and fixed at cause in Phase 24

| # | Defect | Consequence | Fix |
|---|---|---|---|
| 1 | `cmd_gate` early returns (bad magic/version/length/CRC/short) did not update the observable last-result state | the gate reported ACCEPT after a refusal, so a test (and any caller reading that state) could conclude a hostile frame was accepted | `refuse()` helper sets the state on **every** rejection path |
| 2 | waypoint length check used a 5-byte stride while 8 bytes were then read | a short frame was read past its own end | `CMD_WP_BYTES 8`, `CMD_MAX_FRAME` raised 128 → 160 so a full 16-waypoint mission fits |
| 3 | an in-loop waypoint rejection was overwritten by an unconditional `CMD_ACCEPT` after the loop | rejected waypoint batches were silently accepted | explicit `bad` flag; the final assignment is conditional |
| 4 | the battery RTL band latched but never produced a safety action | the requirement passed while being tested through `failsafe_mode_request()`, which the application never called — dead behaviour | action model routes the latched band to RTL/LAND; `failsafe_mode_request()` now derives from `failsafe_action()` |

Defect 4 is the important class: **a requirement that is only ever exercised
through a helper the application does not call is not a requirement.** It is why
every failsafe tier is now asserted on the live `safety:` log line and through a
sim scenario (`imu_rc_loss`), not only in a unit test.
