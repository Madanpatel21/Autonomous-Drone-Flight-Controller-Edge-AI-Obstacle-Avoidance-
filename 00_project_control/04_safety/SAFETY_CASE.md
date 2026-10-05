# Safety Case

Phase 24, 2026-10-04. Revision A. Supersedes the 5-claim template.

A safety case is an **argument**, not a document: claims are stated, and each one
must be backed by evidence that someone else could re-run. What follows is that
argument for this project, together with an explicit statement of what it does
**not** establish.

## Evidence status of this case

> **Every claim below is supported by SIM or host-HIL evidence only.**
> There is no STM32 board, no PCB, no bench rig and no flight test in this
> workspace. No claim here may be cited as hardware evidence, and no vehicle
> flight is authorised on the basis of this document. This is DEC-001 and
> DEC-020 applied to safety.

Re-runnable evidence base (2026-10-04):

| Evidence | Command | Result |
|---|---|---|
| Firmware unit + integration suite | `02_firmware/build/fc_tests.exe` | **2741/2741 checks PASS** |
| Regression runner (build, suite, determinism, 8 scenarios, GS, HIL host rig, requirement audit, manufacturing package audit, documentation audit, V&V review gate, release manifest audit, final A-Z audit) | `08_testing/regression_tests/run_regression.sh` | **33 executed steps PASS + 2 H-gated SKIPs, script exit 0** (Phase 29, 2026-10-04) |
| V&V review gate (critical requirements verified or explicitly blocked) | `08_testing/vnv_gate.py` | **83 requirements classified: 49 verified, 34 unverified with written blockers, 0 failed; 23/23 critical verified or explicitly blocked; no certification claimed** |
| Ground-station self-test | `06_communication/ground_station/ground_station.py --self-test` | **18 tests PASS** |
| Requirement/architecture audit | `08_testing/audit_requirements.py` | **83 baseline requirements, 1 status row each; FW-001 and FW-004 re-checked** |
| Manufacturing package audit (and its own negative tests) | `12_manufacturing/tools/{audit_release_manifest,selftest_audit}.py` | **7/7 check groups PASS, 16/16 negative cases behaved as specified** |

## Claims

### Claim 1 — The vehicle cannot depend on the AI for basic stabilization

**Argument.** The AI publishes obstacle detections only; it never produces an
attitude, rate or thrust command (`DEC-007`). The control cascade from mission
target to motor demand is the FC's own, and every published avoidance component
passes through a hard FC-owned clamp applied as the **last** step before the
navigation interface sees it (`SAF-040`). If the companion process dies or the
model emits nonsense, the bounded path still commands a legal setpoint.

**Evidence.** `ai_loss` scenario: companion stream dies at 6 s, perception
degrades to `TOF_ONLY` (mode 1), flight continues `TAKEOFF → HOLD`, no failsafe,
no abort — asserted in the regression runner. `SAF-040` adversarial-input test:
every published component inside FC-owned limits. `SAF-041` conflict test: in the
±20° cone the FC range wins and AI confidence is halved.

**Strength.** Strong for the *architecture* claim (the dependency does not
exist). **Weak for the perception capability**: `AI-003`/`AI-005` are **OPEN** —
there is no model, no dataset and no measured inference latency in this project.
The claim is that the AI cannot destabilise the vehicle, not that it works.

### Claim 2 — Loss of the companion computer is detected and handled

**Argument.** ICD-02 frames carry a heartbeat; the link task maintains a 1 s
health window (`SYS-004`). On expiry the fused picture degrades and the flight
continues. Companion death is deliberately **not** a failsafe, because treating an
advisory system as flight-critical would convert a perception outage into a
forced landing.

**Evidence.** `ai_loss` scenario in the runner (see Claim 1); `HEALTH` frame
length-mismatch and heartbeat-window unit tests from Phase 19.

**Gap.** `HW-008` (companion power-cycle command) is **OPEN**; the companion can
be detected as lost but not yet power-cycled by the FC.

### Claim 3 — Every RC/GPS/sensor failure has a defined, bounded response

**Argument.** Each failure tier is a distinct failsafe, and the **action** taken
is chosen by what the vehicle can still physically do, not by which failure was
detected first (`DEC-021`):

| Condition | Reported failsafe | Action | Why that action |
|---|---|---|---|
| IMU timeout, or >100 missed deadlines | `FC_FAILSAFE_IMU` | **MOTOR_STOP** | without attitude there is no flyable action |
| RC link lost | `FC_FAILSAFE_RC_LOSS` | RTL | position and attitude both survive |
| Battery critical, or the RTL band latched | `FC_FAILSAFE_BATTERY` | RTL / LAND | energy, not information, is the limit |
| Geofence violated | `FC_FAILSAFE_GEOFENCE` | RTL | bounded return is the cheapest correct outcome |
| Position/estimator unhealthy | `FC_FAILSAFE_ESTIMATOR` | LAND | attitude and altitude survive; position does not |
| Companion lost | *(not a failsafe)* | none, continue | advisory system by decision (`DEC-007`) |

`MOTOR_STOP` dominates every other tier. The GNSS time-to-first-fix cannot trip
the estimator tier, because the tier is driven by an `est_ever_healthy` latch
rather than by instantaneous health.

**Evidence.** `test_safety_action_feasibility` reports RC loss while the action is
`MOTOR_STOP` — the cross-domain case where a naive priority list would have said
"returning home". `test_safety_deadline_storm_stops` proves a deadline storm
forces `MOTOR_STOP`. `test_estimator_health_escalation` proves position loss is
`FC_FAILSAFE_ESTIMATOR` with action `LAND` and never `MOTOR_STOP`. Scenario matrix
in the runner: `rc_loss`, `imu_dropout`, `battery_low`, `gnss_loss`, `ai_loss`,
`imu_rc_loss`.

**Strength.** The decision logic is thoroughly exercised. **Weakness:** every one
of these responses is a *commanded* response on a host CPU. Nothing demonstrates
the vehicle physically completing a landing after a real IMU or radio failure.

### Claim 4 — The reported safety state cannot be misread as safer than it is

**Argument.** This is the claim Phase 24 added. A failsafe name describes a
*cause*; an action describes a *consequence*. Before Phase 24 the wire carried
only the cause, so an operator watching the vehicle during an IMU failure saw
`RC_LOSS` (the most recently latched cause) and would reasonably have believed the
vehicle was returning home while it was in fact cutting its motors. Telemetry
schema **v2** carries the action as a separate field; `MOTOR_STOP` is raised to
the GS as its own `CRITICAL` line; the GS refuses unknown schema versions rather
than guessing; MAVLink `HEARTBEAT` reports `MAV_STATE_CRITICAL` for any failsafe
*or* any action.

**Evidence.** `test_telemetry_action_on_the_wire`: v2 round-trip, a v1-length
frame refused, `HEARTBEAT` CRITICAL. GS `test_safety_action_on_the_wire`: all
five action codes decode, IMU-failure-with-RTL-action is distinguishable from
IMU-failure-with-motor-stop. GS `test_status_record_v1_length_is_refused`: a
52-byte v1 frame is rejected, not mis-decoded. Runner step "ground station
telemetry schema v2 (action on the wire)": a real firmware-generated log decodes
to 240 status records, shows `action=MOTOR_STOP`, and exits 1 under `--strict`.
Scenario `imu_rc_loss` asserts `failsafe=1 action=4` on the live flight log.

**Strength.** Closed for the decode path, end-to-end against firmware-generated
bytes. **Weakness:** there is no live command/transport link (see Claim 5).

### Claim 5 — Command input cannot grant authority the FC does not own

**Argument.** The FC-side command gate validates before dispatch: magic, schema
version, length and CRC16 first; then the arming class — `ARM`, `DISARM_OK`,
`FORCE_ARM`, `CLEAR_ARM_GATE`, `OVERRIDE_FAILSAFE` — is refused
**unconditionally**, with no payload that can unlock it. `EMERGENCY_STOP` is
always accepted and maps only to the FC's existing bounded `LAND` action, so it
can stop the vehicle but never arm it or clear a failsafe. Modes are whitelisted.
`SET_PARAM` is clamped to FC-owned envelopes and unknown or NaN parameter ids are
refused. Waypoint batches are bounded (≤16 points, 8 B/point, 50 m fence).

**Evidence.** `test_command_gate_arming_class_refused`: 10/10 arming-class frames
refused; CRC, length, magic, version, short-frame, unknown type, NaN parameter and
parameter-range violations all refused; waypoint cap, length and fence violations
refused. GS `CommandTests`: the whole arming class rejected client-side with
`COM-004` cited; `EMERGENCY_STOP` accepted unconditionally and never an arming or
override path.

**Gap — stated plainly.** The gate is proven, but **there is no live command
transport** in this build (COM-004/GS-002/GS-003 remain PARTIAL). The gate's
input is currently an `FC_SIM_CMD_LOG` file probe, not a radio link. A gate with
no wire behind it protects against a threat that has not yet been connected.

### Claim 6 — Flight testing progresses from bench to controlled tests

**Argument.** The project has a staged plan (`MILESTONE_PLAN.md`) and a defined
HIL design, but **only the first, simulated stages have been executed.**

**Status.** Executed: SIM scenario matrix, host HIL rig (real HAL contract, framed
link, DShot words over the wire, IWDG semantics). Not executed: STM32 build
(no `arm-none-eabi-gcc`), board bring-up, PCB, HIL-1..HIL-6, any flight test.
This claim is **NOT YET ESTABLISHED**, and the regression runner reports both
H-gated items as SKIP rather than PASS so that this stays visible.

## Explicit non-claims

To keep the case honest, the following are stated as things this project does
**not** establish:

1. **No airworthiness or certification claim.** The risk method is
   project-local (RPN over a 1–5 scale); it is not ARP4761, DO-178C or
   ISO 26262 methodology, and a real hazard analysis must map each hazard to a
   *hazardous failure condition* — a defined end state — which this does not do.
2. **No hardware evidence.** Host timing is not MCU timing. The 900 µs budget,
   mean 0.5 µs / max 23 µs figures, and every fault-injection latency come from a
   host CPU executing the same application. SYS-001/SYS-002 are `OPEN (H)`.
3. **No independent attitude reference.** `EST-001` is PARTIAL: a complementary
   filter with gyro-bias, not the specified quaternion EKF, and no magnetometer.
   An undetected IMU bias is an UNDETECTED branch in the fault tree (FM-02).
4. **No thrust-failure management.** No motor or ESC feedback exists, so
   single-actuator failure is undetected (FM-13, HZ-03).
5. **No AI capability claim.** No model, no dataset, no latency number. Claim 1 is
   about isolation, not about performance.
6. **No flight authorisation.** Gains are sim-class placeholders; Phase 21 tuning
   requires bench identification, and no flight test has been performed.

## Residual risk statement

Five fault-tree branches remain open and are carried as `OPEN (H)` rows in
`08_testing/VERIFICATION_REPORT.md` and forward hazards in
`PRELIMINARY_HAZARD_ANALYSIS.md`:

| Branch | Carried as |
|---|---|
| Undetected IMU bias / wrong data | EST-001 **PARTIAL** |
| Motor/ESC failure, power collapse | SYS-005 **OPEN (H)** |
| GNSS wrong fix | EST-003 **PARTIAL** |
| Real MCU control timing | SYS-001/SYS-002 **OPEN (H)**, experiment HIL-3 |
| Live command transport | COM-004 / GS-002 / GS-003 **PARTIAL** |

The single most useful next safety action is therefore not another simulated test:
it is **bench identification of the control gains and a real sensor-fault
injection run on hardware**, because every remaining critical branch needs either
the board or better sensors to close.
