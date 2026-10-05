# Fault Tree Analysis (FTA) — top event: loss of controlled flight

Phase 24, 2026-10-04. Revision A.

## Method and its limits

Symbolic decomposition of the single top event the vehicle must never reach:
**the vehicle leaves controlled flight** (uncommanded free-fall, tumble,
flyaway, or flight outside the permitted volume). Gates use **AND / OR / INHIBIT
(priority)** notation.

**Limits, stated up front.** This is a qualitative structure derived from the code
that exists, not a quantitative reliability model: no FIT rates, no component
populations, no measured failure rates, no fault-injection coverage percentages.
No gate below is marked *quantitatively proven*. Where a cut set turns out to be
empty in the design, that is recorded as an **UNDETECTED** branch rather than
papered over — those are the two real hazards the project is carrying (HZ-01
undetected IMU bias, HZ-03 loss of thrust).

## Top event

```
TOP: Loss of controlled flight
│
├── OR
│   ├── T1  Uncontrolled descent
│   ├── T2  Uncontrolled rotation / tumble
│   ├── T3  Flyaway outside the permitted volume
│   └── T4  Unintended impact while nominally "in a failsafe"
│
├── AND
│   ├── T5  Actuation authority unavailable
│   └── T6  No correctable setpoint available
```

`T5 AND T6` is the pair that matters: either alone is survivable (a vehicle with
no position but good attitude can land; a vehicle holding attitude with a wrong
target flies to a bounded wrong place). Together they are a fall.

---

## T1 — Uncontrolled descent

```
T1 Uncontrolled descent
├── OR
│   ├── OR
│   │   ├── A1 Attitude reference lost
│   │   │   ├── OR
│   │   │   │   ├── IMU sample timeout          [DETECTABLE]  -> IMU failsafe, zero outputs, ABORT
│   │   │   │   └── IMU bias / wrong data       [UNDETECTED]  -> no second attitude source exists
│   │   │   └── (AND)
│   │   │       ├── Control deadline repeatedly missed
│   │   │       └── (INHIBIT) motor stop never executed because the loop is stalled
│   │   └── AND
│   │       ├── A2 No thrust
│   │       │   ├── OR
│   │       │   │   ├── Motor / ESC failure     [UNDETECTED - no motor feedback]
│   │       │   │   ├── Power rail collapse     [UNDETECTED - silent by nature]
│   │       │   │   └── Battery depleted        [DETECTABLE]  -> 3.5/3.4/3.1 staged response
│   │       │   └── INHIBIT Battery monitor reads healthy because the sag is faster than the sample rate
│   │       └── (INHIBIT) RC override available — NOT AVAILABLE on any T1 path by definition
│   └── AND
│       ├── A3 Position reference lost -> cannot place the landing
│       │   ├── OR
│       │   │   ├── GNSS outage      [DETECTABLE]  -> ESTIMATOR failsafe -> LAND in place
│       │   │   └── GNSS wrong fix   [UNDETECTED]  -> confident flight to the wrong place
│       │   └── (INHIBIT) battery reserve sufficient for a controlled descent — usually true
│       └── (INHIBIT) geofence / landing logic can steer to a safe spot — often false
```

**Two UNDETECTED leaves under T1: IMU bias and motor loss.** Both are named in
FMEA FM-02 / FM-13 and both are structural (no second attitude reference, no
motor feedback) rather than test gaps.

---

## T2 — Uncontrolled rotation / tumble

```
T2 Uncontrolled rotation
├── OR
│   ├── B1 Asymmetric thrust from a single actuator failure   [UNDETECTED] -> FM-13
│   ├── B2 Mixer saturation sustained beyond the recoverable envelope
│   │   ├── OR
│   │   │   ├── Setpoint outside the AF envelope   [INHIBITED] -> per-axis clamp, tilt <= 25 deg
│   │   │   ├── Attitude loop commanded beyond authority
│   │   │   └── (AND) integral windup during a stall -> anti-windup present, margin unproven on hardware
│   │   └── (INHIBIT) rate loop able to arrest the rotation — false when the demand exceeds max rate
│   └── AND
│       ├── B3 Correct torque sign or convention
│       │   └── DEC-008 unified body frame + spin directions; proven in SIM
│       └── (INHIBIT) installation correct  [UNVERIFIABLE without a bench hover]
```

---

## T3 — Flyaway outside the permitted volume

```
T3 Flyaway / out-of-bounds flight
├── AND
│   ├── C1 No position or position believed, uncorrected
│   │   ├── OR
│   │   │   ├── GNSS wrong fix                 [UNDETECTED] -> FM-08
│   │   │   ├── Estimator divergence           [DETECTABLE] -> plausibility clamp
│   │   │   └── Baro-only vertical drift       [DETECTABLE] -> deadband + ground reference
│   │   └── (INHIBIT) horizontal controller available — true while attitude survives
│   └── C2 No containment
│       ├── OR
│       │   ├── Geofence not enforced          [OPEN] -> enforcement exists in SIM only
│       │   ├── Geofence radius itself wrong   [OPEN] -> fence is a function of a single position source
│       │   └── Geofence check not reached because the loop is stalled (FM-15)
│       └── (INHIBIT) pilot commands a return — unavailable if RC is also lost
└── NOTE: a mission-target error inside the fence is contained by the fence; a
    wrong *position estimate* corrupts the fence itself, which is why FM-08 is CRITICAL.
```

---

## T4 — Impact while nominally in a failsafe

This is the event Phase 24 was built to prevent, and it is the reason the failsafe
reports an **action** as well as a failsafe name.

```
T4 Impact while nominally in a failsafe
├── AND
│   ├── D1 A failsafe was reported
│   └── D2 The reported failsafe did NOT imply the action actually taken
│       ├── OR
│       │   ├── Operator believed "RTL" while the vehicle was cutting motors
│       │   │   └── (cause) action not carried on the wire, OR
│       │   │       telemetry schema drift between FC and GS  [FOUND & FIXED, Phase 24]
│       │   ├── Operator believed "LAND" while the vehicle was RTL-ing at speed into an obstacle
│       │   │   └── (cause) mode/action conflation in the reported state
│       │   └── Operator saw a stale or refused frame and assumed the old, safer state
│       │       └── (cause) unknown schema refused with no explicit "version unsupported" state
│       └── (INHIBIT) action reported explicitly — NOW PRESENT (schema v2, DEC-021)
```

**D2's first two leaves were live before Phase 24.** The GS rendered a motor stop
as whatever failsafe name happened to be reported; the regression scenario
`imu_rc_loss` exists specifically to hold this closed, and the runner asserts
`failsafe=1 action=4` on the live log plus `action=MOTOR_STOP` after a real
firmware-generated decode.

---

## T5 — Actuation authority unavailable

The first half of the top-event AND pair: the vehicle cannot produce thrust or
torque it has been asked for.

```
T5 Actuation authority unavailable
├── OR
│   ├── E1 No electrical power to the actuators
│   │   ├── OR
│   │   │   ├── Battery depleted / cell open
│   │   │   │   └── [DETECTABLE] voltage monitor -> staged RTL/LAND (T1/A2)
│   │   │   └── Voltage collapse under load    [UNDETECTED] -> FM-06, no ride-through
│   ├── E2 Actuator does not respond
│   │   ├── OR
│   │   │   ├── Single ESC or motor failure     [UNDETECTED] -> FM-13, no motor feedback
│   │   │   ├── DShot frame rejected by the ESC (CRC/logic error)
│   │   │   └── Motor disconnected             [UNDETECTED] -> no detection path at all
│   │   └── (INHIBIT) a redundant actuator path — NOT PRESENT in this design
│   └── E3 Authority commanded away
│       ├── OR
│       │   ├── Motor stop commanded by the failsafe action (FC_ACTION_MOTOR_STOP)
│       │   ├── Disarm state reached            [DETECTABLE] -> motors zero (CTRL-005)
│       │   └── Output saturated so long the demand cannot be met   -> T2/B2
│       └── (INHIBIT) attitude loop able to arrest  -> true unless T1/A1
└── NOTE: a motor stop is a *deliberate* T5 occurrence. It is correct only
    because the action is chosen by flyability (DEC-021); choosing it when the
    vehicle could still fly home converts a recoverable failure into T1.
    This is why SR-06/SR-07 exist and why the action is reported on the wire.
```

## T6 — No correctable setpoint available

The second half of the pair: even with full actuation the vehicle does not know
where it should go, so full actuation cannot help.

```
T6 No correctable setpoint available
├── OR
│   ├── C1 Position reference absent or untrusted
│   │   ├── OR
│   │   │   ├── GNSS outage        [DETECTABLE] -> LAND in place, attitude retained
│   │   │   ├── GNSS wrong fix     [UNDETECTED] -> T3/C1, FM-08
│   │   │   └── Estimator divergence             [DETECTABLE] -> plausibility clamp
│   └── AND
│       ├── C2 No alternative reference can be brought in
│       │   ├── Optical flow — NOT_READY stub     [OPEN] -> EST-003 PARTIAL
│       │   ├── Magnetometer — not fitted        [OPEN] -> EST-001 PARTIAL
│       │   └── Visual/VIO — not implemented      [OPEN]
│       └── (INHIBIT) re-acquisition within the energy reserve — possible, but
│           only if the geofence has not already been corrupted (see T3/C2)
└── NOTE: T6 alone is survivable (LAND in place). It becomes fatal only in AND
    with T5 — hence the top event's AND gate rather than two independent ORs.
```

## T7 — Command path grants authority the FC does not own

Not a loss-of-flight event by itself, but the only branch where an external
actor can *cause* one, so it is decomposed rather than buried under T5/E3.

```
T7 Command path grants authority the FC does not own
├── AND
│   ├── F1 A command frame reaches the FC
│   │   ├── OR
│   │   │   ├── Corrupted in transit            [DETECTABLE] -> CRC16 refused
│   │   │   ├── Wrong protocol version          [DETECTABLE] -> refused
│   │   │   ├── Truncated / over-long frame     [DETECTABLE] -> length refused
│   │   │   └── Hostile, well-formed frame      -> passes the transport checks
│   └── F2 The frame is not re-validated inside the FC
│       ├── OR
│       │   ├── Arming class accepted           -> DISARMED by cmd_gate, unconditionally
│       │   ├── Parameter outside envelope applied -> clamped by cmd_gate to FC limits
│       │   └── Waypoint outside the fence applied -> refused (cap/offset/fence bounds)
│       └── (INHIBIT) the FC re-validates every field it acts on
└── NOTE: F2 is the cut set. If cmd_gate is bypassed or removed, a single
    well-formed hostile frame re-opens the whole tree. This branch has no
    live transport behind it yet (COM-004 PARTIAL), so it is proven against
    the gate itself (10/10 arming frames refused) and not against a wire.
```

---

## Safety requirements traced out of the tree

Every mitigation cut set below corresponds to code that exists and to a check
that is executed on every regression run.

| SR | Derived from | Requirement | Implementation | Verification |
|---|---|---|---|---|
| SR-01 | T1 / A1 | a stale IMU sample must never be delivered as valid | stale samples dropped in `sensor_hub`; zero actuator outputs (DEC-011) | `TEST-SIM-IMUFAIL`, `imu_dropout` |
| SR-02 | T1 / A2 | battery depletion must produce a staged, latched response | 3.5 warn / 3.4 RTL / 3.1 land, hysteresis, critical latch (SAF-030/031) | threshold + latch tests, `battery_low` |
| SR-03 | T1 / A3 | position loss must not be reported as an attitude problem | `failsafe_notify_estimator` feeds a distinct tier; GNSS TTFF cannot trip it | Phase 23 `test_estimator_health_escalation` (position loss → LAND, never MOTOR_STOP) |
| SR-04 | T2 / B2 | no published setpoint may exceed the FC-owned envelope | SAF-040 clamp applied as the **last** publish step | `SAF-040` adversarial-input test |
| SR-05 | T3 / C2 | out-of-range mission/command input must be refused at the FC | `cmd_gate` waypoint count/offset/fence bounds | `test_command_gate_arming_class_refused` |
| SR-06 | T4 | the failsafe report must carry the action actually taken | `failsafe_action()` by flyability (DEC-021); telemetry schema v2 `action` byte | `test_safety_action_on_the_wire`, `imu_rc_loss` scenario, runner schema-v2 step |
| SR-07 | T4 | MOTOR_STOP must dominate every other tier | `attitude_authority_lost()` gate ahead of the action table | `test_safety_deadline_storm_stops`, `test_safety_action_feasibility` |
| SR-08 | T7 / F2 | the arming command class must be refused unconditionally, inside the FC | `cmd_gate` rejects ARM/DISARM_OK/FORCE_ARM/CLEAR_ARM_GATE/OVERRIDE_FAILSAFE before dispatch | 10/10 arming frames refused in the gate test |
| SR-09 | *excluded from the tree by DEC-007* | companion loss must not become a flight-critical failure, so it is deliberately **not** a branch of any top-event gate | perception degrades to ToF-only, no failsafe; FC-owned ToF keeps a minimum picture | `ai_loss` scenario asserted in runner; `test_safety_companion_not_a_failsafe` |
| SR-10 | T5 / T1 | a stalled control loop must expire the watchdog, and a deadline storm must force MOTOR_STOP | IWDG emulation with real semantics in the host rig; `attitude_authority_lost()` treats `deadline_misses > 100` as attitude loss | host-HIL watchdog negative test (exit 3); `test_safety_deadline_storm_stops` |

## Undetected / unprotected branches — the honest bottom line

| Branch | Status | Consequence for the safety case |
|---|---|---|
| IMU bias / wrong data (T1/A1) | **UNDETECTED**, no mitigation | The safety case cannot claim an independent attitude reference. `EST-001` stays PARTIAL. |
| Motor/ESC failure (T2/B1, T1/A2) | **UNDETECTED**, no feedback | Cannot claim any post-failure thrust management. FM-13. |
| Power-rail collapse (T1/A2) | **UNDETECTED** | Silent; no flight-log persistence. SYS-005 OPEN (H). |
| GNSS wrong fix (T3/C1) | **UNDETECTED** | Corrupts the geofence that is the only containment. EST-003 PARTIAL. |
| Real MCU timing (T1/A1 deadline branch) | **UNPROVEN** | Host timing is not MCU timing. SYS-001/002 OPEN (H), experiment HIL-3. |

Five branches cannot be closed with the hardware and evidence available. They are
carried as `OPEN (H)` rows in `08_testing/VERIFICATION_REPORT.md` and as forward
hazards FH-01..FH-05 in the PHA, so the next project inherits them as known work
rather than as a surprise.
