# State Estimation Spec & Validation Record

Status: APPROVED 2026-10-03 (Phase 11). Requirement refs: EST-001..006.

## Estimator 1 — Attitude (est_attitude.c)

- **State**: quaternion q (body→NED), gyro bias b (3), filtered rates.
- **Inputs**: gyro (rad/s, calibrated), accel (m/s², calibrated) @1 kHz.
- **Outputs**: q, filtered body rates, bias, convergence 0..1.
- **Method**: quaternion propagation + accel-vector correction (cross-product form), gain-scheduled (fast convergence → steady), bias adaptation on accel-orthogonal error. Selected over full EKF per "no unnecessary complexity": meets EST-001 RMSE target in SIM tests; EKF slot (`estimation/ekf/`) reserved for GPS/flow fusion stage.
- **Init**: q = level, bias = 0; convergence metric requires 3000 good updates.
- **Rejection**: |a| outside 0.71g–1.12g → update skipped; dt outside 0–50 ms → clamped.
- **Failure behavior**: invalid IMU → no propagation; health flagged upstream (SEN-004).
- **SIM metrics (measured, deterministic runs)**: stationary 5000-tick: |q−identity| < 0.02 (TEST-EST-001). 20° tilt convergence: roll/pitch error < 0.03 rad within 20 s simulated (TEST-EST-002). Bias injection 0.02 rad/s → removed by calibration + estimator bias adaptation (TEST-EST-003).
- **Hardware-gated**: real IMU noise/ vibration performance — NOT EXECUTED.

## Estimator 2 — Altitude (est_alt.c)

- **State**: fused alt, vz (bias state REMOVED in Phase 15 — see est_alt.c header history: it absorbed position error and left vz unanchored).
- **Inputs**: baro @50 Hz, vertical accel @1 kHz.
- **Method (Phase 16 revision)**: complementary — predict `az = accel.z − g`, correct `alt += 0.50·err` per baro sample; vz correction `0.02·err` gated by a 0.30 m deadband (baro noise must not random-walk vz); ground reference = mean of the first 1 s of at-rest samples (single-sample capture produced a permanent −0.56 m offset); >50 m divergence re-anchors.
- **Rejection**: invalid baro/IMU skipped; IMU loss → predict stops (stale samples never integrated — SAF-003).
- **SIM metrics (measured)**: 60 s stationary under 5 Pa baro noise: mean offset 0.071 m, |vz| < 0.2 m/s (TEST-EST-ALT-DRIFT, Phase 16 — supersedes the pre-noise-model "drift <±0.5 m" wording; instantaneous ±0.5 m bounce is sensor noise by design of a hard-anchored estimate). Divergence re-anchor: 100 m teleport recovered in one sample (TEST-EST-ALT-DIVERGENCE).
- **Hardware-gated**: baro noise floor on real BMP390 — NOT EXECUTED.

## Estimator 3 — Position/velocity (Phase 14, GNSS+flow)

Design frozen: EKF with 6-state (pos NED, vel NED) + gyro/accel bias reuse; GNSS 10 Hz update, optical-flow velocity @50 Hz, innovation chi-square gating (EST-004), divergence detection → EST_UNHEALTHY → failsafe (EST-055).

## Fault behavior summary

| Fault | Detection | Response |
|---|---|---|
| IMU dropout | 3 ms timeout | hub invalid → ctrl zeros motors (SAF-003) |
| IMU stuck | jump validation + bias divergence | unhealthy flag → failsafe |
| Baro stale | 200 ms timeout | alt estimator degrades to accel-only leaky |
| Battery stale | 200 ms timeout | failsafe monitor flags |

Evidence: `02_firmware/build/tests/` run log (50/50 at Phase 10 completion; Phase 11 additions below).
