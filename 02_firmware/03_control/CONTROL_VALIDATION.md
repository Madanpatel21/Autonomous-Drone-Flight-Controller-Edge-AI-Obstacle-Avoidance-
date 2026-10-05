# Flight Control Validation Record — Phase 12

Status: COMPLETE (SIM only; hardware validation pending — never claim otherwise). Requirement refs: CTRL-001..006, SYS-001/002.

## Implemented

- `ctrl_cascade.c`: rate loop 1 kHz, attitude loop 250 Hz (quaternion error `qs⁻¹⊗q`, negative-feedback mapping — sign fix verified by closed loop), PID with derivative-on-measurement + anti-windup + output saturation, arm gate (disarmed → motors 0), hover-throttle collective (altitude-loop hook).
- `mixer_quadx.c`: FLU frame, M1..M4 = FR, FL, RL, RR, spins CW, CCW, CW, CCW → torque map diag(4,4,4) (DEC-008); saturation minimization (collective > roll/pitch > yaw).
- SIM physics: minimal rigid-body quad (thrust/moments/rates/quaternion), SIM-class parameters, accel near-hover approximation documented in `hal/sim/hal_sim.c` (SIM-003 partial; full aero model is Phase 15).

## Measured results (deterministic SIM runs, `fc_tests`)

| Test | Scenario | Result |
|---|---|---|
| TEST-CTRL-HOV | 15° roll perturbation → level, 5 s @1 kHz | final roll 0.01°, pitch −0.00°, \|ω\| 0.0001 rad/s — PASS |
| TEST-CTRL-RATE | rate step 1 rad/s roll | tracked 0.967 rad/s, motors bounded — PASS |
| TEST-CTRL-SAT | absurd setpoint 50 rad/s, 2 s | motors stay in [0,1], no divergence — PASS |
| TEST-MIX-01/02 | hover exactness + saturation limiting | PASS |

## Known limitations (honest)

- Gains are placeholder-initialization values (classic 5-inch practice), NOT identified from an airframe. Formal tuning (sim sweep) is Phase 21 optimization; any hardware use requires re-validation.
- SIM physics is control-validation grade only: no blade flapping, motor desaturation asymmetry, battery sag or propeller thrust curve. DO NOT record as flight evidence.
- Attitude estimator lags true attitude during fast transients (visible in probe trace) — acceptable for stabilization, investigate in Phase 21.

## Hardware-gated items (NOT EXECUTED)

- Real plant identification, real ESC/motor latency, real vibration effects on the rate loop, p99 timing on STM32 (SYS-001/002 `H` verification).
