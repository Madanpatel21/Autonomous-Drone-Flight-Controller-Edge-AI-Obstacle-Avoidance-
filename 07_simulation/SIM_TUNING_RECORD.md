# SIM Tuning Record — Phase 21

Gains and airframe limits used by the SIM control stack, with the measurement
that justifies each one. **SIM-CLASS ONLY.** These numbers are tuned against a
minimal rigid-body model (DEC-009) and are NOT identified from a real vehicle;
bench re-identification is required before any hardware use.

## Controller gains (flight_controller/control/ctrl_cascade.c, ctrl_position.c)

| Gain | Value | Where | Evidence |
|---|---|---|---|
| rate roll/pitch kp,ki,kd | 0.135, 0.05, 0.003 | ctrl_cascade.c | Phase 12 sim sweep; 15° perturbation recovery → 0.01° |
| rate yaw kp,ki | 0.20, 0.03 | ctrl_cascade.c | TEST-CTRL-YAW |
| attitude roll/pitch kp | 4.0 | ctrl_cascade.c | Phase 12 sim sweep |
| altitude kp,ki | 1.2, 0.05 | ctrl_cascade.c | Phase 14/15: true 1.73 m vs 1.50 m sp |
| altitude K_ALT (baro corr) | 0.50 /sample @50 Hz | est_alt.c | Phase 16 (60 s drift 0.071 m mean) |
| velocity K_VZ + deadband | 0.02, 0.30 m | est_alt.c | Phase 16 (vz random-walk fix) |
| position kp | 1.20 /s | ctrl_position.c | Phase 21: 6 m step → peak 6.00 m, no overshoot beyond target, |y|max 0.00 m |
| position ki | 0.35 /s² | ctrl_position.c | Phase 21: 1.5 m/s crosswind rejected to 0.00 m (P-only gave 4.39 m) |
| velocity kp | 2.50 /s | ctrl_position.c | Phase 21: drag/retreat response; AF accel clamp is the binding limit |

## Airframe limits (FC-owned constants, ctrl_position.h / obstacle_avoidance.h)

| Limit | Value | Rationale |
|---|---|---|
| AF_VEL_MAX_M_S | 2.5 m/s | mission/avoidance requests are clamped here |
| AF_ACC_MAX_M_S2 | 3.0 m/s² | ≈0.3 g lateral; keeps the vehicle inside the attitude loop's authority |
| AF_TILT_MAX_RAD | 25° | ~0.42 g lateral component at 1 g thrust margin |
| TILT_SLEW_RAD_S | 60 °/s | Phase 21: an unlimited tilt step dragged the vehicle into the ground at takeoff |
| AIRBORNE_MIN/FULL_M | 0.30 / 1.00 m | horizontal authority scales in with altitude (takeoff is vertical first) |
| AVOID_BRAKE/STOP/RETREAT | 4.0 / 2.0 / 1.5 m | Phase 21 flight: retreat needed below 1.5 m (obstacle closes at 0.5 m/s) |
| AVOID_V_FWD/LAT/RETREAT | 2.0 / 1.0 / 1.0 m/s | SAF-040 clamps (DEC-014/DEC-018) |
| I_CLAMP (position integral) | 0.8 × AF_VEL_MAX | anti-windup |

## Vehicle model parameters (hal/sim/hal_sim.c, sim-class)

| Parameter | Value | Note |
|---|---|---|
| SIM_MASS | 0.65 kg | placeholder |
| SIM_TW_RATIO | 3.0 | hover throttle 1/3 |
| SIM_IXX/IYY/IZZ | 0.0045 / 0.0045 / 0.0085 kg·m² | placeholders |
| SIM_ARM | 0.115 m | quad-X arm |
| SIM_DRAG_PER_S | 0.9 /s | linear horizontal drag, Phase 21 |
| wind | configurable, default 0 | disturbance model (`hal_sim_set_wind`) |
| obstacle closure | 0.5 m/s, contact floor 0.3 m | environment model |

## Tuning method actually used

1. Instrument-and-observe: each candidate gain set was run in the deterministic
   SIM (fixed seed) and read out through the existing test prints, not asserted
   by eye.
2. Failure first: the P-only position loop was *rejected* by the measured wind
   drift (4.39 m), which is what motivated integral action.
3. Limits were set by observed safe behaviour, then frozen as FC-owned
   constants and covered by `TEST-POS-LIMITS` (adversarial inputs).
4. Every change is regression-locked by the suite (795/795) and the runner
   (exit 0, determinism diff = 0 bytes).

## Open tuning items (not done here)

- Attitude-loop gain schedule vs tilt (currently constant).
- Per-axis normalization of the mixer under large tilt.
- Motor/ESC thrust curve identification (DShot, H-gated).
- Wind/gust spectra beyond a constant value; no turbulence model.
