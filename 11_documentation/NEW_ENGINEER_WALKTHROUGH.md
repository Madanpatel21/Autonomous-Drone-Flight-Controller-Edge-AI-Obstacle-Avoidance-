# New Engineer Walkthrough

Every command below has been executed in this workspace; the "observed" value is
copied from that run, not written from memory. Steps 1–8 are also executed
automatically by `08_testing/regression_tests/run_regression.sh` (step 9), which
is the fastest way to check a fresh clone: it does all of this and prints one line
per step.

## 0. What you need

| Tool | Version used here | Needed for |
|---|---|---|
| CMake | 4.4.1 | build system |
| Ninja | 1.13.2 | generator (`cmake -G` picks it up) |
| GCC (host) | MinGW-W64 UCRT 16.1.0 | SIM/HIL targets, the parameter-file generator |
| Python 3 | 3.14.0 | ground station, audits, rig capture analysis |
| `arm-none-eabi-gcc` | **not present** | the STM32 target — its absence is expected, not a bug |

Nothing else is required: the ground station has no third-party dependencies and
the firmware builds with no external libraries.

## 1. Build and run the test suite

```bash
cd 02_firmware
cmake -S . -B build
cmake --build build -j 4
./build/tests/fc_tests.exe
```

Observed: the suite prints each case's result and ends with

```
    action feasibility: rc_loss+imu_dead -> report RC_LOSS, action MOTOR_STOP
    command gate: 10/10 arming-class frames refused, 0 accepted
2741/2741 passed
```

`echo $?` is `0`. A non-zero exit means a case failed; read the last line printed
before the summary, it is the case's own description.

## 2. Fly a simulated mission

```bash
FC_SIM_FAST=1 FC_SIM_TICKS=30000 FC_SIM_SEED=42 ./build/fc_sim.exe nominal
```

Observed: `nav: TAKEOFF` then `nav: HOLD`, and the final line contains
`deadline_misses=0`. `FC_SIM_FAST=1` removes real-time pacing; `FC_SIM_SEED`
fixes the sensor noise; `FC_SIM_TICKS` sets the run length (30 000 ticks = 30 s
of simulated time).

Now break something on purpose — the scenario that exists to keep the safety
model honest:

```bash
FC_SIM_FAST=1 FC_SIM_TICKS=30000 FC_SIM_SEED=42 ./build/fc_sim.exe imu_rc_loss > /tmp/imu_rc.log 2>&1
grep -E 'nav:|safety:' /tmp/imu_rc.log
```

Observed: `nav: TAKEOFF → HOLD → ABORT` and

```
safety: failsafe=1 action=4
```

Read that as: *the reported cause is RC loss (1), and the action is motor stop
(4)*. It looks contradictory and it is not — the IMU died first, so the vehicle
cannot fly, and the action is chosen by flyability (`DEC-021`). This is the case
the whole safety model was rebuilt around.

## 3. Determinism

```bash
FC_SIM_FAST=1 FC_SIM_TICKS=8000 FC_SIM_SEED=1234 ./build/fc_sim.exe nominal > /tmp/a.log 2>&1
FC_SIM_FAST=1 FC_SIM_TICKS=8000 FC_SIM_SEED=1234 ./build/fc_sim.exe nominal > /tmp/b.log 2>&1
diff -q /tmp/a.log /tmp/b.log && echo identical
```

Observed: `identical`. Two runs of the same scenario are byte-for-byte equal
(`SIM-002`). If this breaks, something in the control path reads wall-clock time
or an uninitialised value.

## 4. Ground station

```bash
cd 06_communication/ground_station
python ground_station.py --self-test
```

Observed: `Ran 18 tests ... OK`.

Decode a real firmware log (the firmware emits one with `FC_SIM_GS_LOG`):

```bash
cd 02_firmware
FC_SIM_FAST=1 FC_SIM_TICKS=12000 FC_SIM_SEED=42 FC_SIM_GS_LOG=/tmp/gs.bin ./build/fc_sim.exe battery_low > /dev/null
cd ../06_communication/ground_station
python ground_station.py --from-log /tmp/gs.bin --strict; echo "exit $?"
```

Observed: `exit 1` — and that is the **pass** condition for this scenario: the
station decoded the log, raised a CRITICAL line for the battery RTL band, and
`--strict` means "make unhealthy states exit non-zero" so a script can gate on it.

## 5. Host HIL rig (real application behind a link)

```bash
cd 02_firmware
cmake -S . -B build-hil -DFC_TARGET=hil
cmake --build build-hil -j 4
./build-hil/hil_rig.exe --port 45701 --ticks 6000 --capture /tmp/dshot.bin > /tmp/rig.log 2>&1 &
sleep 1
./build-hil/fc_hil.exe --port 45701 --ticks 6000 > /tmp/fc.log 2>&1; echo "fc exit $?"
wait
cd ../07_simulation/hardware_in_the_loop
python dshot_analyzer.py /tmp/dshot.bin --expect-ticks 6000
```

Observed (via the runner, which picks a free port automatically): `fc exit 0`,
nav `TAKEOFF → HOLD`, per-sensor ages `imu=0us gnss=99000us baro=19000us
rc=9000us`, DShot capture `records=24000 crc_ok=24000 crc_bad=0`.

Start the **rig first** — the flight controller retries the link, the rig does
not. This is host HIL: real application, real byte stream, simulated world. It is
**not** hardware evidence.

## 6. The audits

```bash
python 08_testing/audit_requirements.py            # requirements + architecture
python 08_testing/vnv_gate.py                      # V&V review (critical items)
python 12_manufacturing/tools/audit_release_manifest.py
python 12_manufacturing/tools/selftest_audit.py    # proves the previous one can fail
python 11_documentation/tools/docs_audit.py        # documentation claims
python 13_release/tools/audit_release.py           # release manifest matches artifacts
```

Observed: `audit: PASS` (83 baseline requirements, 53 algorithm files, 0 FW-001
violations, 0 FW-004 allocation sites); `VNV: PASS` (83 requirements classified,
23 critical, 49 verified, 34 unverified with written blockers, 0 failed);
`AUDIT: PASS (7 checks passed, 0 failed, 0 warnings)`;
`SELFTEST: PASS (16/16 cases behaved as specified)`; and the docs audit reports
the document inventory with `DOCS: PASS`.

## 7. Everything at once

```bash
bash 08_testing/regression_tests/run_regression.sh
```

Observed (Phase 29): `REGRESSION: ALL EXECUTED STEPS PASSED`, exit 0, **33
executed steps PASS + 2 H-gated SKIPs**. The two `SKIP` lines name the missing
prerequisite (`arm-none-eabi-gcc`, no board) and are exactly what a working
environment looks like today.

## 8. Where the numbers live

`11_documentation/PROJECT_FACTS.json` is the single source of truth for the
counts quoted in this walkthrough and in the index. Regression step 10 re-reads
the derived facts from the files that own them (firmware headers, the released
parameter blob, the release manifest), then compares the counts only a run can
know — suite size, executed step count, GS test count, scenario count, package
audit results — against what that run just measured. The same step also checks
the numbers printed in *this* walkthrough against the file, so a stale number
cannot hide in prose either.

When a change legitimately moves a number, re-baseline it deliberately and say
why in the decision log:

```bash
python 11_documentation/tools/docs_audit.py --update-facts \
    --observed executed_steps=27
```

The failing step prints the exact `--observed` arguments it needs; copy them
only if you agree the new value is correct.

## What you still cannot do here

Build or flash the real board, run the AI, measure MCU timing, or turn a wheel —
no board, no toolchain, no model, no rig. Everything in this repository is
honest about that, and the two `SKIP` lines are the machine representation of it.
