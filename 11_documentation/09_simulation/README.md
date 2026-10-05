# 09_simulation — SIM, SIL, HIL and the vehicle model

| What | Where |
|---|---|
| Simulation validation: model limits, determinism, replay | [`07_simulation/SIMULATION_VALIDATION.md`](../../07_simulation/SIMULATION_VALIDATION.md) |
| Tuning record (what the gains are, and the measurements behind them) | [`07_simulation/SIM_TUNING_RECORD.md`](../../07_simulation/SIM_TUNING_RECORD.md) |
| Host HIL rig design: link, clocking, fault injection, evidence | [`07_simulation/hardware_in_the_loop/HIL_DESIGN.md`](../../07_simulation/hardware_in_the_loop/HIL_DESIGN.md) |
| Rig usage | [`hardware_in_the_loop/README.md`](../../07_simulation/hardware_in_the_loop/README.md) |
| Determinism contract | `SIM-002`; enforced as a byte-identical double run in the regression |

Code: `02_firmware/hal/sim/{hal_sim.c,main_sim.c,sim_model.c}` and
`07_simulation/hardware_in_the_loop/{hil_rig.c,dshot_analyzer.py}`.

## The distinction this whole repository rests on

| Level | What it is | Trustworthy for |
|---|---|---|
| SIM | Whole world in-process, 1 kHz tick | algorithms, state machines, fault behaviour |
| HIL (host) | The *real application* behind a framed CRC-16 link; the world is owned by a separate rig process that is the clock master | link integrity, sensor-rate fidelity, DShot bit stream, injection latency, CRC robustness, watchdog interlock |
| HIL (hardware) | Same rig against a real board | **does not exist** — `HIL-1..6` are OPEN |

A result from SIM or host HIL is **never** hardware evidence. The regression
runner prints the hardware steps as `SKIP` with the missing prerequisite so this
cannot drift unnoticed.

Two known model limitations, stated in the validation record: linear isotropic
drag only, and a single-obstacle corridor for avoidance flight tests. Gains
identified against this model require bench re-identification before flight.
