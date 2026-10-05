# HIL Design — Phase 22 (host rig EXECUTED, hardware still H-gated)

Response to `15_prompts/22_22_hil.prompt.md` (8-section format).

**Status: PARTIAL — the host-side rig is implemented, executed and part of the
regression; the hardware rig is H-gated.** There is no STM32 board, no ARM
toolchain and no rig in this workspace. Per DEC-001 the SIM, HIL and STM32
targets share ONE application, so a rig only replaces the HAL backend — no
flight-code changes are needed to move from what runs today to a board.

**Nothing in this document is hardware evidence.** Section 6 splits the
acceptance criteria into what is now measured on the host (HIL-0a..0f) and what
still requires a board (HIL-1..6).

---

## 1. Inputs / assumptions

- Production target STM32F765VIT6 (DEC-002) on a custom board (Phase 07 record;
  no board manufactured), `arm-none-eabi-gcc` unavailable.
- HIL host: a PC owns the vehicle, the sensor environment and virtual time; the
  FC runs the **real firmware binary**; the two meet at the FC's electrical
  interfaces, which the rig replaces with a framed byte link.
- Determinism (SIM-002) carries over: the rig is fixed-step and seedable and
  reuses `hal/sim/sim_model.c` — the *same* physics and sensor error model the
  SIM runs. A second, drifting physics copy on the host was explicitly
  rejected; it would make every SIM-vs-HIL difference untraceable.
- Electrical timing must be preserved across the link, otherwise the FC's
  staleness and heartbeat logic is tested against fiction.

## 2. Work performed

### 2.1 Model extraction (the enabler)

`hal/sim/hal_sim.c` used to hold both the world model and the HAL. It was split
into `hal/sim/sim_model.{c,h}` (physics, sensor error model, fault injection,
companion byte stream — no HAL symbol) and a thin `hal_sim.c` adapter that
keeps every `hal_*` and `hal_sim_*` entry point unchanged. The SIM evidence
base is unchanged: the same constants, the same PRNG call order, the same
trajectories. The rig now links `sim_model.c`; the SIM target and the HIL target
cannot disagree about the world.

### 2.2 HIL link protocol (`common/protocols/hil_link.{c,h}`, DEC-019)

Frame (little-endian, CRC-16/CCITT-FALSE over header+payload):

```
+0  u8  0xA5 SOF0      +4 u8  seq (per direction)   +8  u64 t_us (sender clock)
+1  u8  0x5A SOF1      +5 u8  flags                 +16 payload[len]
+2  u8  version       +6 u16 len                   +16+len u16 crc
+3  u8  type
```

Message types: `TICK` (FC→rig, 4 production DShot600 words + armed flag),
`SENSORS`, `AI_SET`, `COMPANION` (rig→FC), `FAULT`, `SCHED_FAULT`, `SCENARIO`,
`FLASH`, `ACK`, `ERROR`, `WORLD`, `SHUTDOWN`. Explicit little-endian field
access (no struct punning), a bounded 4096-byte RX ring with CRC, sequence-gap
and resync accounting, and a pure `hil_link_decode()` that never mutates its
input.

### 2.3 FC side (`hal/hil/hal_hil.{c,h}`, `hal/hil/hil_main.c`)

A third HAL backend, built with `-DFC_TARGET=hil`. It implements every
`hal_interfaces.h` entry point against the link:

- **Clock**: the rig is the clock master. `hal_time_us()` serves the rig
  timestamp of the last received frame, so the FC's time base *is* the host's
  virtual clock and every latency is measurable in one shared time domain.
- **No blocking in the control path**: `hal_*_read()` serves cached data and
  `hal_actuator_write()` only encodes into a TX ring; socket I/O happens in
  `hil_link_poll()` at the tick boundary (the stand-in for DMA interrupts).
- **Sensors keep their real rates**: the rig refreshes IMU at 1 kHz, baro 50 Hz,
  ToF-down 50 Hz, ToF-forward 25 Hz, battery 10 Hz, RC 100 Hz, GNSS 10 Hz with a
  3 s TTFF, and stamps each sample with the time it was produced. The FC
  therefore observes true sample ages, not "everything arrives at 1 kHz".
- **Actuator path is the production one**: `hal_actuator_write()` puts the
  frames `motor_output` already built (`esc_dshot.c`, CRC-4 included) on the
  wire, so the rig and the capture analyser verify the exact 16-bit words an
  ESC would receive.
- **Watchdog emulated with IWDG semantics** (SAF-004): once it expires it stops
  accepting feeds, exactly like a real one, and the run is failed.
- **Companion and storage** cross the link as bytes: the ICD-02 stream and the
  parameter image are served by the rig, so the FC exercises serialize → CRC →
  parse and the flash contract end to end.

### 2.4 Host rig (`07_simulation/hardware_in_the_loop/hil_rig.c`)

A separate process (`hil_rig.exe`) that binds a loopback socket, owns the world
and advances it **only when a valid actuator frame arrives** — the same causal
chain as a real rig (FC → ESC signal → vehicle → sensors → FC). Options:
`--scenario`, `--seed`, `--ticks`, `--fault NAME@AT_US`, `--capture`,
`--trace`, `--seed-noise on|off`, `--corrupt-every N` (wire corruption),
`--drop-every N`, `--idle-timeout-ms`. It answers control queries (flash,
world snapshot, shutdown) during a post-run grace period.

### 2.5 DShot capture analyser (`dshot_analyzer.py`)

Decodes the capture with an **independent** implementation of the DShot CRC-4
convention and checks: every frame's CRC, throttle bounds, STOP while
disarmed, exactly 4 motor frames per tick, and an even 1000 µs tick period.
Independence is the point — agreement between two implementations is the
software equivalent of comparing an ESC analyser against the wire. It is not
evidence about a real ESC.

## 3. Files created / modified

Created:
- `02_firmware/common/protocols/hil_link.{c,h}` — link codec, RX ring, payloads, fault vocabulary
- `02_firmware/hal/sim/sim_model.{c,h}` — shared deterministic world model
- `02_firmware/hal/hil/hal_hil.{c,h}` — HIL HAL backend
- `02_firmware/hal/hil/hil_main.c` — FC entry point for the HIL target
- `07_simulation/hardware_in_the_loop/hil_rig.c` — host rig
- `07_simulation/hardware_in_the_loop/dshot_analyzer.py` — capture analyser

Modified:
- `02_firmware/hal/sim/hal_sim.c` — now a thin adapter over `sim_model`
- `02_firmware/CMakeLists.txt` — `fc_sim_model` library + `FC_TARGET=hil` (builds `fc_hil.exe` and `hil_rig.exe`)
- `02_firmware/flight_controller/motor_control/esc_dshot.{c,h}` — added `dshot_decode_frame`, `dshot_unit_from_throttle` (decoder side of the same frame format)
- `02_firmware/tests/test_main.c` — 8 new cases (1600 checks, was 795)
- `08_testing/regression_tests/run_regression.sh` — HIL build + 6 executed HIL steps
- This document, plus the governance records in section 7.

## 4. Interfaces affected

- New wire interface: the HIL link (above). Documented here; it must be added to
  `INTERFACE_CONTROL_DOCUMENT.md` when hardware exists, because the STM32 rig
  will speak it over a serial/UDP bridge.
- No change to `hal_interfaces.h`: the HIL backend implements the existing
  contract, which is the point of the HAL (FW-001).
- `esc_dshot.h` gained two decoder-side functions; the frame format itself is
  unchanged, so no previously recorded interface moves.

## 5. Verification performed

All of it on the host, from `08_testing/regression_tests/run_regression.sh`
(Phase 22 snapshot: 19 executed steps PASS, 2 H-gated SKIPs, script exit 0; the
suite has grown since — 33 executed steps as of Phase 29. The rows below describe
the Phase 22 evidence this document owns):

| Check | Result |
|---|---|
| `fc_tests` suite | **1600/1600** (795 before Phase 22) |
| SIM determinism double-run | diff = 0 bytes |
| HIL target build (`FC_TARGET=hil`) | clean, no warnings |
| HIL loopback nominal, 6000 ticks | `TAKEOFF HOLD`; link_stalls=0, crc_errors=0, seq_gaps=0, tx_drops=0, resyncs=0, overruns=0; rig: dshot_crc_err=0 |
| Sensor-age fidelity (the synchronisation evidence) | imu 0 µs, baro 19 ms, gnss 99 ms, tof-down 19 ms, tof-fwd 39 ms, battery 99 ms, rc 9 ms |
| DShot capture analysis | 24000 records, 6000 ticks, 0 CRC failures, period exactly 1000 µs, armed from 2.02 s, max throttle 978, 0 disarm violations |
| Fault injection `rc_loss` over the link | rig applied at 8 000 000 µs, visible to the FC in the same frame (latency 0 µs ≤ 1 control cycle), FC `first_loss_us=8000000` — identical to the scheduled time; nav `TAKEOFF HOLD LAND DONE` |
| Fault injection `imu_dropout` over the link | FC `first_loss_us=6000000`; nav `TAKEOFF HOLD ABORT` (SAF-003 path) |
| Link robustness (18 wire-corrupted frames) | 18/18 rejected on CRC, 18 sequence gaps detected, 20 resyncs, 17 stalls, mission still completed; FC exits 1 *because* frames were lost, which is the honest result for a lossy link |
| Watchdog interlock (negative test) | with `--wdg-starve` the emulated IWDG expires, the FC reports `wdt_expiries=1` and exits 3 |
| Estimator-vs-truth correlation | 6 s: `err_xy=0.010 m`, `err_alt=-0.151 m` |

Scenario matrix equivalence: `rc_loss` and `imu_dropout` produce the *same* nav
sequences over the link as in-process SIM (steps 4 and 15 of the same run).

Throughput (loopback, informational): ≈6 400 control ticks/s wall-clock for a
virtual 1 kHz rate, ~184 bytes/tick rig→FC and 27 bytes/tick FC→rig — the numbers
to size a physical link against.

## 6. Acceptance criteria

### Executed here (host rig; repeatable, in the regression)

| # | Criterion | Verification |
|---|---|---|
| HIL-0a | The real application runs unchanged behind a HIL HAL, 1 tick = 1 exchange, zero link stalls/drops over 6000 ticks | regression step "HIL loopback nominal" |
| HIL-0b | Sensor rates survive the transport: the FC sees 1 kHz IMU, 50 Hz baro/ToF, 25 Hz ToF-forward, 10 Hz GNSS/battery, 100 Hz RC, measured as per-sensor sample age | same step (age assertions) |
| HIL-0c | Actuator outputs cross the link as production DShot600 words; an independent decoder validates every frame's CRC, the STOP-while-disarmed rule, bounds and tick periodicity | "HIL DShot capture analysis" |
| HIL-0d | Fault injection reaches the FC within one control cycle, and the safety response is unchanged versus SIM | "HIL fault injection (rc_loss/imu_dropout)" |
| HIL-0e | Link robustness: corrupted frames are rejected on CRC, resynced, counted, never silently accepted | "HIL link robustness" |
| HIL-0f | Watchdog interlock is armed: starving the feed is detected and fails the run | "HIL watchdog interlock" |

### Still H-gated (require the board + toolchain; **not** satisfied by anything above)

| # | Criterion | Verification |
|---|---|---|
| HIL-1 | Real FC binary runs ≥60 s on the rig without a watchdog reset | H (SWD + log) |
| HIL-2 | Same scenario scripts produce equivalent nav transitions on hardware | H |
| HIL-3 | Control-loop rate measured on hardware ≥500 Hz (SYS-001), p99 latency ≤5 ms (SYS-002) | H (timer capture) |
| HIL-4 | Sensor-model correlation: bench-identified noise/bias within a factor of 2 of the sim-class values | H (bench data) |
| HIL-5 | DShot600 frames decoded by a real ESC analyser match `esc_dshot` output bit-for-bit | H (analyser vs TIM1 capture) |
| HIL-6 | Bus-level fault injection (I2C NACK, UART framing errors) takes effect within one control cycle | H |

## 7. Risks / TBDs

1. **Blocked on hardware + toolchain**: no board, no `arm-none-eabi-gcc`, no
   rig. Not a project blocker under the SIM-first mandate; H-gated work.
2. **Timing model**: the rig's clock is exact and the transport is lossless
   TCP; a real bus has jitter, and SWO/ITM bandwidth may limit logging during
   DShot activity. HIL-3 must measure that, not assume it.
3. **Two clock domains**: FC free-running vs host fixed-step means SIM-002
   bit-identity does **not** carry to hardware HIL; only statistical equivalence
   is achievable, which is why HIL-2 is written that way.
4. **Electrical fidelity**: a byte link is not an ESC, a SPI bus or a UART. The
   capture analyser validates the bit stream, not the wire.
5. **A rejected frame costs a stall**: when the rig corrupts a frame the FC has
   no new sensor frame for that tick and reports a stall. That is the honest
   cost of link loss on a host-paced link; on hardware the FC would keep
   running on its own timer and the estimator would age the sample instead.
6. Parameters are not persisted by the rig (the image is RAM, 0xFF at start),
   so each run starts from defaults — deliberate for repeatability.

## 8. Next dependency

- Phase 08 bring-up (board + `arm-none-eabi-gcc`) is the prerequisite for
  HIL-1..6; the HIL target added here is the software half of that path.
- The rig needs a serial/UDP transport mapping before it can talk to a board;
  the frame format is transport-agnostic by design.
- `MASTER_VERIFICATION_PLAN.md` carries HIL-0a..0f as executed and HIL-1..6 as
  H-gated; Phase 16's test strategy references both ladders.