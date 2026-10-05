# Programming & Production Test

Per-unit procedure for a built flight controller, and the station-by-station
status of what can actually be executed today.

## Serial number scheme

`<MFG-rev>-<YYWW>-<NNNN>` — package revision, year+ISO week, sequence within
that week. Example: `MFGA-2640-0001`. The revision field is what makes a unit
traceable: a board's serial must correspond to a release package on record, so a
unit recovered from the field can be tied to the BOM, parameter blob and
firmware digest it was programmed with. Serial is applied as a label **and**
stored in the parameter image (see step PT-7), not only written on the bag.

## Programming

1. **SWD first, bootloader later.** The bootloader (`FW-007`) is **OPEN** — no
   bootloader code exists yet, so no field update path is offered. An update
   today means SWD with the unit on the bench. Do not advertise an over-the-air
   update path until FW-007 has code and a SIM mock-flash test.
2. Program the firmware image built for the pinned toolchain. Until the STM32
   toolchain gate closes, this step cannot execute at all (no image exists) —
   that is a designed-in blocker, not an oversight.
3. Write the parameter default file:
   [`parameters/params_defaults.bin`](parameters/params_defaults.bin) — the exact
   bytes `params_store()` writes (`[struct][crc16 LE]`), generated from the
   firmware's own `params_defaults()` and digest-pinned in the manifest. Never
   hand-type parameter values onto a board: the blob is the interface.
4. Verifying the write is not optional: read back and compare the CRC. The
   firmware already falls back to safe defaults on a bad CRC (`FW-005`), which
   means a *silently corrupt* store looks like a healthy board with wrong gains —
   exactly the failure the readback catches.

## Production test stations

| Station | What it checks | Executable today | Gate |
|---|---|---|---|
| PT-0 | Pre-power: rail-to-ground resistance, no shorts, connector orientation, keying | **no** | `BOARD` |
| PT-1 | Firmware acceptance: build + unit/integration suite + determinism double-run | **yes** | — |
| PT-2 | Power-up: 3V3/5V within ±5 %, current-limited bring-up, idle draw vs power budget | **no** | `BOARD` |
| PT-3 | MCU: SWD ID, option bytes, clock, watchdog armed | **no** | `BOARD`, `STM32_TOOLCHAIN` |
| PT-4 | Sensors: IMU/baro/GNSS/ToF/flow identity + live data, thermal image of the buck | **no** | `BOARD` |
| PT-5 | Motor-off DShot validation on all four outputs (decode with `07_simulation/hardware_in_the_loop/dshot_analyzer.py`) | **no** | `BOARD` |
| PT-6 | Links & failsafe: telemetry v2 on the wire, RC failsafe, geofence, arming interlock | **no** (decode path itself is exercised in the regression against replayed bytes) | `BOARD` |
| PT-7 | Parameter store: write blob, read back, CRC match, safe-default fallback on corruption | **no** (blob generation + CRC cross-check runs in the regression) | `BOARD` |

### PT-1 — what "yes" means right now

```
08_testing/regression_tests/run_regression.sh          # build + suite + determinism + scenarios
```

executes, on this host, the same application source that the STM32 build uses
(`FW-001`), and its log is PT-1's record. It is **software acceptance**: the
host is x86 with a simulated HAL, so PT-1 passing says nothing about real MCU
timing (`SYS-001/SYS-002` remain OPEN (H)). No per-unit PT-1 record is written
because there is no unit; the station is marked executable so that the first
build does not discover it was never defined.

### Failure handling

A failed station quarantines the unit: no release, record written with the
failure and the measured value, board stays with the tester. A station that was
skipped is recorded as **skipped**, never as passed, and a unit with a skipped
safety-relevant station (PT-2, PT-5, PT-6, PT-7) is not releasable. This mirrors
the regression runner's rule that hardware-gated steps report SKIP rather than
PASS.

## Record format

One JSON file per unit at `qc/records/<serial>.json` — fields and rules in
[`qc/README.md`](qc/README.md). The `BOARD` facility gate in
[`RELEASE_MANIFEST.json`](RELEASE_MANIFEST.json) is probed by the manifest audit:
while it is open, any record appearing there fails the regression, and when a
record legitimately appears the gate must be closed with a revision bump rather
than left open.
