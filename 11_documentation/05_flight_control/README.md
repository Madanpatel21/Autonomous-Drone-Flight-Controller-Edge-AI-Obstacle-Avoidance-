# 05_flight_control — control law documentation

| What | Where |
|---|---|
| Control validation: rate/attitude/altitude loops, anti-windup, disarmed path | [`02_firmware/03_control/CONTROL_VALIDATION.md`](../../02_firmware/03_control/CONTROL_VALIDATION.md) |
| Tuning record — what the gains are, and what they are *not* | [`07_simulation/SIM_TUNING_RECORD.md`](../../07_simulation/SIM_TUNING_RECORD.md) |
| Cascade architecture and airframe limits | `DEC-005`, `DEC-017`, `DEC-018` in [`00_project_control/DECISION_LOG.md`](../../00_project_control/DECISION_LOG.md) |
| Mixer and frame convention (motors M1–M4 = FR, FL, RL, RR) | `DEC-008`; code in `02_firmware/flight_controller/motor_control/mixer_quadx.c` |

Code: `flight_controller/control/{ctrl_rate.c,ctrl_attitude.c,ctrl_altitude.c,ctrl_position.c}`.

## The one thing to understand before tuning

**Every gain in this repository is a sim-class placeholder.** They were chosen to
make a simulated vehicle fly under the simulation's own dynamic model
(`DEC-009`), and the model's parameters are estimates, not bench measurements of
the real airframe. Two measured findings from Phase 21 shaped the current design
and are worth keeping in mind:

* a P-only position loop drifts under steady wind — measured 4.39 m under a
  1.5 m/s crosswind before integral action was added;
* avoidance needs a bounded RETREAT below 1.5 m, because holding station drove
  the vehicle *into* a closing obstacle (measured clearance 0.30 m, the contact
  floor).

Control authority limits (2.5 m/s, 3.0 m/s², 25° tilt) are enforced as the last
step before the command is used, so no upstream consumer — including the AI — can
exceed them (`SAF-040`).
