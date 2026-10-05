# Simulation Validation Record — Phase 15

Response to `15_prompts/15_simulation/01_simulation.prompt.md` (required 8-section format).
Scope: SIM-class environment per DEC-001/DEC-009. **This record contains no hardware
evidence.** Every result below was produced by the deterministic SIM target
(`fc_sim.exe`, MinGW GCC, 1 kHz fixed-step virtual clock). Hardware correlation is
H-gated and tracked as TBD.

---

## 1. Inputs / assumptions

- No STM32 hardware or ARM toolchain exists in this workspace (`arm-none-eabi-gcc` not
  available). SIM-first mandate per `30_target_strategy.prompt.md` and DEC-001.
- Physics: minimal rigid-body quad (DEC-009), FLU frame, T/W = 3.0 (SIM_TW_RATIO),
  Hamilton quaternion convention R^T·ẑ (DEC-008). Ground reaction support term added
  in Phase 15 so the estimator does not see free-fall while on the pad.
- Sensor error models are **sim-class estimates**, not identified from a real vehicle:
  gyro 0.01 rad/s RMS + constant bias (0.005/−0.004/0.003 rad/s), accel 0.10 m/s² RMS,
  baro 5 Pa RMS (≈0.4 m), GNSS 3 s TTFF + 0.10 m/s velocity noise, ToF 0.01 m,
  battery ~10 A at hover on a 4S 4500 mAh pack.
- Correlation points with measured hardware are **defined but unpopulated**: the model
  parameters above must be replaced by identified values from motor-bench and bench
  sensor logging (Phase 08 bring-up record is NOT EXECUTED — no board).

## 2. Work performed

1. Deterministic PRNG (xorshift32) + pseudo-gaussian; per-session gyro bias; noise
   classes for IMU/baro/GNSS/ToF.
2. Virtual GNSS (3 s TTFF, N/E velocity noise, `gnss_loss` fault), downward ToF,
   battery discharge model.
3. `est_position.{c,h}`: GNSS position/velocity tracker with 25 m innovation gate,
   1 s age/health timeout (EST-003/EST-004 subset).
4. Fault-injection API (`hal_sim_fault`) + time-scheduled injection
   (`hal_sim_schedule_fault`) + scenario presets
   (`nominal|imu_dropout|rc_loss|battery_low|gnss_loss|ai_loss|noiseless`).
5. Batch/automation switches: `FC_SIM_FAST` (disable real-time pacing),
   `FC_SIM_TICKS=N` (deterministic run length), `FC_SIM_TRACE=1` (periodic trace),
   seed via argv/`FC_SIM_SEED` (SIM-004 replayability).
6. Test coverage: TEST-SIM-DET (seed determinism), TEST-SIM-NOISE (closed-loop noise
   robustness), TEST-SIM-FAULTS (GNSS loss / IMU dropout health), TEST-EST-003
   (GNSS tracking), TEST-EST-004 (outlier rejection), TEST-SIM-IMUFAIL (SAF-003
   regression through the real app tick path).
7. **Defects found by the scenario sweep and fixed (root causes, not symptoms):**
   - `rc_loss`/`battery_low`/`gnss_loss` scenarios injected their fault at t = 0, so
     the FC never armed and the scenarios exercised nothing (rc_loss produced zero
     nav transitions). Faults are now scheduled in flight (6–10 s, after arm at 2 s
     and takeoff).
   - **SAF-003 violation**: on IMU dropout the app kept feeding the last sample
     (still `valid = true`), so the zero-output path never ran and
     `est_alt_imu_feed` integrated a frozen accel at 1 kHz → uncommanded climb from
     1.1 m to 46 m. Fixed: stale sample marked invalid (SAF-003 zero-output path),
     `failsafe_imu_update()` escalates `FC_FAILSAFE_IMU`, mission aborts
     (`NAV_ABORT`), and `c.motor` telemetry is zeroed with the actuators.
   - `battery_low` / `gnss_loss` were injected in flight but no code consumed them:
     battery-critical now forces a controlled landing (SAF-030), position loss
     (ever-healthy latch guard) descends in place since RTL is unflyable without
     horizontal position.
8. Deleted leftover probes (`build/alt_probe.c`, `build/debug_probe.c`).

## 3. Files created / modified

Created: `flight_controller/estimation/est_position.{c,h}`,
`07_simulation/SIMULATION_VALIDATION.md` (this file).
Modified: `hal/sim/hal_sim.c` (PRNG, noise, faults, scheduling, scenarios),
`hal/sim/main_sim.c` (CLI/automation), `hal/hal_interfaces.h` (SIM-only hooks),
`flight_controller/app/app_main.c` (fault consumption wiring),
`flight_controller/control/ctrl_cascade.c` (zero-output telemetry fix),
`flight_controller/failsafe/failsafe_sm.{c,h}` (IMU health monitor),
`flight_controller/navigation/mission_sm.{c,h}` (`NAV_ABORT`, `mission_land_now`),
`tests/test_main.c` (6 new tests).

## 4. Interfaces affected

- `hal_interfaces.h`: added SIM-only hook `hal_sim_schedule_fault(name, at_us)` in the
  SIM-only section; app-facing interfaces unchanged (DEC-001 boundary intact).
- `mission_sm.h`: new `NAV_ABORT` state + `mission_abort()` / `mission_land_now()`.
- `failsafe_sm.h`: new `failsafe_imu_update(bool)`.
- No app-layer signature changes; the HAL contract remains backend-agnostic.

## 5. Verification performed

Environment: MinGW GCC + ninja, `FC_SIM_FAST=1 FC_SIM_TICKS=30000 FC_SIM_SEED=42`.

Unit/integration suite: **525/525 PASS** (`02_firmware/build/tests/fc_tests.exe`, exit 0).

Determinism (SIM-002): `fc_sim nominal`, seed 1234, 8000 ticks, two full-process runs —
`diff` of complete stdout = **zero bytes** (bit-identical telemetry, exit 0 both runs).

Scenario matrix (25–30 s simulated, seed 42; 0 deadline misses in every run):

| Scenario | Fault (in-flight t) | Nav sequence | Result |
|---|---|---|---|
| nominal | — | TAKEOFF → HOLD | stable hold, est ≈1.2–1.4 m |
| rc_loss | RC lost @ 8 s | TAKEOFF → HOLD → RTL → LAND → DONE | **RTL flown**, landed |
| imu_dropout | IMU lost @ 6 s | TAKEOFF → HOLD → ABORT | motors stopped, z 1.50 → 0.00 m, **no surge** (SAF-003) |
| battery_low | 3.05 V/cell @ 10 s | TAKEOFF → HOLD → LAND → DONE | controlled descent |
| gnss_loss | GNSS lost @ 6 s | TAKEOFF → HOLD → LAND → DONE | descend-in-place on position loss |
| ai_loss | companion lost @ 6 s | TAKEOFF → HOLD | **advisory degradation verified (Phase 17)**: perception degrades to TOF_ONLY (FC ToF carries), flight continues, no failsafe (PERCEPTION_FUSION.md §5) |
| noiseless | noise off | TAKEOFF → HOLD | reference run |

Closed-loop metrics under noise (TEST-SIM-NOISE, 12 s): roll −1.6° (<6° limit),
altitude 1.48 m vs 1.50 m setpoint. Altitude hold (TEST-CTRL-ALT): true 1.60 m /
est 1.92 m around 1.50 m setpoint. Position tracker (TEST-EST-003): 50.50 m estimate
vs 50.00 m true (<1.5 m). Innovation gate (TEST-EST-004): 500 m outlier rejected,
estimate held at 2.00 m.

## 6. Acceptance criteria

| Requirement | Criterion | Status |
|---|---|---|
| SIM-001 | Virtual IMU/baro/GNSS/ToF/battery/RC + noise/bias/dropout/failure injection | **MET (SIM)** — RC flow inert by design; flow not modeled yet |
| SIM-002 | Fixed seed → bit-identical telemetry | **MET (SIM)** — double-run diff = 0 |
| SIM-003 | Rigid-body physics adequate for control validation | **MET (SIM)** — closed-loop hover/rate/alt convergence; model NOT hardware-identified |
| SIM-004 | Scripted + replayable fault scenarios | **MET (SIM)** — scenario presets + seed/tick automation |
| EST-003 | Position tracking with 10 Hz GNSS | **PARTIAL (SIM)** — tracker meets <1.5 m on the stub (no lateral vehicle dynamics yet) |
| EST-004 | Outlier rejection before fusion | **MET (SIM)** — TEST-EST-004 |
| SAF-003 | No sensor failure causes uncommanded surge | **MET (SIM)** — regression test TEST-SIM-IMUFAIL after fixing the 46 m climb |
| H-correlation | Model params from measured data | **TBD (H-gated)** — requires bench/board (Phase 08 record NOT EXECUTED) |

## 7. Risks / TBDs

1. **Horizontal dynamics stub**: virtual GNSS reports (0,0) + noise; the vehicle has no
   lateral dynamics, so EST-003 "steady-state NE error" is only verified for a
   stationary/spoofed target. Aerodynamics/obstacle environment is a later phase.
   **RESOLVED Phase 21**: the vehicle now has horizontal rigid-body dynamics with
   drag and wind, GNSS reports the true simulated position/velocity, and EST-003
   is verified against a moving vehicle (TEST-SIM-LATERAL: 6.00 m waypoint flight,
   estimator tracking truth within 1 m). The model is still NOT hardware-identified.
2. ~~`ai_loss` has no FC-side consumer~~ **Resolved Phase 17**: the perception
   fusion module consumes the stream (advisory-only, DEC-013) and the scenario
   is asserted in the regression runner. The full companion link (UART, 0x01–0x11
   frames) remains Phase 19.
3. All sensor model magnitudes are sim-class guesses until bench identification
   (H-gated). No comparison against logged hardware data exists.
4. Near-hover accelerometer approximation (documented in `hal_sim.c`) limits aggressive
   attitude validity.
5. Gains remain placeholders until Phase 21; nothing here is tuning evidence.

## 8. Next dependency

- `15_prompts/15_simulation/02_hil.prompt.md` → HIL design (design-only, H-gated;
  see `07_simulation/hardware_in_the_loop/HIL_DESIGN.md`).
- Phase 16 (`16_testing_verification/`) consumes this suite as the regression base.
- Phase 08 board bring-up will populate the correlation points in §1/§5 with measured
  data; until then every SIM result must be labeled SIM-only.
