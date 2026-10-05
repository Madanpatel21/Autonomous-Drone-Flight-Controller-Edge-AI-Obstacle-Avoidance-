# Task and Priority Table

Phase 03 scaffolding left the rate and deadline cells as `TBD`. The Phase 29
final audit found that and replaced every cell with the value the application
actually runs — verified against the source, not restated from the design:

- `02_firmware/flight_controller/app/app_main.c`: `TICK_HZ 1000`, `ATT_DIV 4`,
  `FAILSAFE_DIV 10`, `LOW_DIV 20`, altitude every 10th tick, deadline check
  `dt > 900 µs` (SAF-004).
- `02_firmware/hal/sim/main_sim.c`: ground-station stream every 50th tick (20 Hz).

| Task | Target rate | Priority class | Deadline | Inputs | Outputs | Overrun action |
|---|---:|---|---:|---|---|---|
| Rate control (`control_path`) | 1000 Hz (every tick) | Highest | 900 µs tick budget (SAF-004) | attitude / setpoints | torque commands → mixer → DShot | safe output; stale-command stop (SAF-003) |
| Attitude estimation + loop | 250 Hz (`tick % ATT_DIV`) | High | 900 µs tick budget | IMU | attitude quaternion, rate commands | flag / degrade |
| Altitude hold | 100 Hz (`tick % 10`) | High | 900 µs tick budget | barometer | altitude setpoint | flag / degrade |
| Failsafe monitor + companion RX | 100 Hz (`tick % FAILSAFE_DIV`) | High | 900 µs tick budget | health counters, ICD-02 frames | failsafe action, perception feed | safe output (action by flyability) |
| Sensor hub + navigation + estimation + perception | 50 Hz (`tick % LOW_DIV`) | Medium | 900 µs tick budget | IMU/baro/GNSS/RC/ToF | setpoints, mission state, advisory obstacle picture | hold / abort |
| Ground-station telemetry stream | 20 Hz (`tick % 50`, sim harness) | Low | 900 µs tick budget | system state | MAVLink v2 frames + versioned status record | drop / defer (FW-006) |

**How this table is verified.** The scheduler deadline monitor is a real check,
not this document: `dt > 900 µs` increments `deadline_misses`, escalates
`failsafe_notify_deadline_miss()`, and a storm forces `MOTOR_STOP` (DEC-021).
The regression asserts `deadline_misses=0` in every scenario, so a table that
drifted from the code would show up as a failed scenario long before anyone read
it. The 23 µs worst-case tick measured over 20 000 ticks (TEST_LOOP-TIMING) is
the headroom against the same 900 µs budget.

**Not claimed here:** these are host/SIM measurements of the shared application.
MCU interrupt latency and real rates remain `OPEN (H)` (SEN-005, SYS-001) until a
board exists.
