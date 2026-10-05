# Preliminary Hazard Analysis (PHA)

Phase 24, 2026-10-04. Revision A. Scope and method: see
[`FAILURE_MODE_AND_EFFECTS_ANALYSIS.md`](FAILURE_MODE_AND_EFFECTS_ANALYSIS.md)
(FM-xx cross-references below).

This PHA is a **hazard log**, not a hazard analysis closure. The aviation term for
what is missing is *hazardous failure conditions*: each hazard must eventually be
shown to end in a defined aircraft state ("after 3 s the vehicle is descending at
≤1 m/s with motors commanded low"), not merely in a detection flag. That mapping
is `OPEN` and belongs with a real airworthiness method, which this demonstrator
does not have and does not claim.

Severity uses the FMEA scale (1–5). Every row's evidence class is **SIM or
host-HIL** — no flight test, no board, no bench rig exists in this workspace.

## Hazard log

| HZ | Hazard | Cause | Effect on the vehicle | Effect on people/ground | Detected by | Mitigation as built | Sev | Closed? | Verification (evidence class) |
|---|---|---|---|---|---|---|---|---|---|
| HZ-01 | Loss of attitude reference | IMU stall, wrong data, or undetectable bias (FM-01/02) | motors stopped, uncontrolled descent from current attitude | injury/property damage if over people | sample-age timeout; **bias is undetectable in this design** | zero outputs + `FC_FAILSAFE_IMU` + NAV_ABORT; no second attitude source | 5 | **NO** — FM-02 has no mitigation | `imu_dropout`, `imu_rc_loss` scenarios; EST-001 **PARTIAL** — **SIM** |
| HZ-02 | Loss of pilot command | RC radio fade or failure (FM-03) | autonomous RTL, pilot cannot intervene | loss of manual recovery | frame-age watchdog | `FC_FAILSAFE_RC_LOSS` → RTL; arming requires a live RC link | 4 | **PARTIAL** — RTL is flown but never against a real radio | `rc_loss` scenario; host-HIL injection latency 0 µs — **SIM/host HIL** |
| HZ-03 | Loss of thrust | brownout, ESC or motor failure (FM-06/13) | uncontrolled descent | injury/property damage | **none available** | none beyond pre-emptive battery staging | 5 | **NO** | power budget **analysis only**; SYS-005 **OPEN (H)** |
| HZ-04 | Loss of position | GNSS outage or wrong fix (FM-07/08) | LAND in place (outage) or a confident flight to the wrong place (wrong fix) | flyaway; geofence is the only containment | GNSS age/fix status | `FC_FAILSAFE_ESTIMATOR` → LAND; no re-acquisition hold; no second position source | 4–5 | **PARTIAL/NO** — wrong-fix case has no detection | `gnss_loss` scenario; EST-003 **PARTIAL** (GNSS-only) — **SIM** |
| HZ-05 | Loss of energy reserve | battery depletion (FM-05) | staged RTL then LAND | unplanned landing | per-cell voltage + current integration | 3.5 warn / 3.4 RTL / 3.1 land, hysteresis, latch below critical | 4 | **PARTIAL** — thresholds are sim-class, pack curve unknown | `battery_low` scenario; SAF-030/031 tests — **SIM** |
| HZ-06 | Flight outside the permitted volume | bad mission, wind drift, operator error (FM-10) | geofence violation → RTL | flyaway into an occupied area | mission position vs fence radius | both GS and FC validate the fence; FC gate rejects out-of-range frames (50 m cap) | 4 | **PARTIAL** — depends on the fence itself and on a single-source position estimate | geofence tests; `cmd_gate` range rejection — **SIM** |
| HZ-07 | Uncommanded control degradation | missed control-cycle deadlines, task starvation (FM-15/22) | control rate drops, then authority is lost | uncontrolled descent | per-cycle deadline counter | none in-loop; counter is diagnostic only; STM32 timing unmeasured | 5 | **NO** for real MCU timing | `deadline_misses=0` in every scenario; mean 0.5 µs / max 23 µs vs 900 µs — **SIM**; SYS-001/002 **OPEN (H)** |
| HZ-08 | Reset in flight | watchdog expiry, firmware wedge, flash stall (FM-16) | FC reboots with motors at their reset state | uncontrolled descent | IWDG expiry counter | reset-time output safe state; boot ordering | 5 | **PARTIAL** — logic proven on the host rig, STM32 backend unverified | host-HIL watchdog negative test (exit 3) — **host HIL**; **OPEN (H)** |
| HZ-09 | Loss of obstacle-avoidance capability | companion or AI failure (FM-11/12) | vehicle flies a pre-planned path with no obstacle picture | collision | ICD-02 heartbeat; per-detection confidence gate | **advisory by design (DEC-007)**: degrades to ToF-only, flight continues, no failsafe | 3 | **YES, by design** — but AI-003/AI-005 are **OPEN** so there is no model to degrade from | `ai_loss` scenario asserted in runner — **SIM** |
| HZ-10 | Hostile or malformed command | forged frame, schema drift, bad parameter (FM-17/18/19) | **none intended** — the arming class is refused unconditionally | — | magic/version/length/CRC before dispatch; schema version check | `cmd_gate.c` refuses the whole arming/override class, clamps parameters to FC-owned envelopes, bounds waypoint batches | 5 | **PARTIAL** — the gate is proven, but there is **no live command transport**, so the delivery path is untested | `test_command_gate_arming_class_refused`; GS 18 tests; runner schema-v2 step — **SIM** |
| HZ-11 | Misinformed operator | GS renders a safety state incorrectly (FM-19) | operator acts on a wrong belief (e.g. assumes RTL while the vehicle is falling) | bad command decisions at the worst moment | telemetry schema version check; refuses unknown versions | **schema v2** carries the *action* separately from the *failsafe* (DEC-021); MOTOR_STOP is raised as its own CRITICAL line | 5 | **YES (SIM)** — closed for the decode path; a live GS link is still H-gated | GS self-test `test_safety_action_on_the_wire`; runner step "ground station telemetry schema v2" — **SIM** |
| HZ-12 | Misconfiguration | wrong gains, mixer convention, parameter image (FM-14/20) | instability or flip on arm | airframe loss | build-time convention checks; config magic + CRC | DEC-008 unified conventions; corrupt images fall back to safe defaults | 5 | **PARTIAL** — all gains are sim-class placeholders | mixer/attitude tests; `TEST_PARAM-POWERCYCLE` — **SIM**; bench hover **OPEN (H)** |
| HZ-13 | Software defect in the control path | derivation or logic error (FM-21) | wrong response, oscillation, divergence | uncontrolled flight | executable regression suite | the suite **is** the mitigation: 9 defects found and fixed at cause across Phases 15–24 by this mechanism | 5 | **PARTIAL, permanently open** — a defect class is never closed | 2741/2741 checks, 23-step runner exit 0 — **SIM/host HIL** |

## Residual hazard summary

- **Not mitigated at all (2):** HZ-01 undetected IMU bias, HZ-03 motor/ESC/brownout
  loss of thrust. Both are structural gaps in the design (no second attitude
  source, no motor feedback), not missing test cases.
- **Mitigated only in SIM/host-HIL (8):** HZ-02, HZ-04, HZ-05, HZ-06, HZ-07, HZ-08,
  HZ-10, HZ-12. Each has working mitigation logic and every mitigation is
  demonstrated, but the demonstration is on a host CPU running the same
  application — not on an STM32 with real DMA, real bus contention and real
  electrical behaviour.
- **Closed for the designed behaviour (1):** HZ-09 (companion/AI failure is
  advisory by decision, and the decision is tested).
- **Closed for the decode path only (1):** HZ-11.
- **Structurally open (1):** HZ-13 — a software-defect class, mitigated by rate of
  detection only.

## Forward hazards (not yet analysable)

| ID | Hazard | Why it cannot be analysed yet |
|---|---|---|
| FH-01 | AI model behaviour on out-of-distribution imagery | no model, no dataset, no measured inference latency (AI-003/AI-005 **OPEN**) |
| FH-02 | Real GNSS jam/spoofing | needs a receiver and a controlled RF environment (**H-gated**) |
| FH-03 | Propwash / airframe-specific aerodynamics | the vehicle model is linear isotropic drag with sim-class parameters; Phase 25 hardware changes the aerodynamics |
| FH-04 | Thermal derating of MCU, ESC or regulator | no thermal instrumentation exists |
| FH-05 | Crew error during flight testing | OPS-001, Phase 28 |
