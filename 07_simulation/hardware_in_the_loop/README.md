# hardware_in_the_loop

Hardware-in-the-loop: the FC's real firmware with the vehicle, the sensor
environment and virtual time owned by a host rig.

- `HIL_DESIGN.md` — Phase 22 record: architecture, timing model, evidence and the
  split between what is executed on the host (HIL-0a..0f) and what still needs
  a board (HIL-1..6).
- `hil_rig.c` — the host rig (built as `build-hil/hil_rig.exe`).
- `dshot_analyzer.py` — independent DShot600 capture analyser.

## Run it (host loopback)

```bash
# build the HIL target (FC_TARGET=hil) alongside the normal sim target
cmake -S 02_firmware -B 02_firmware/build-hil -DFC_TARGET=hil
cmake --build 02_firmware/build-hil -j 4

# terminal 1 - the rig owns the world and waits for the FC
02_firmware/build-hil/hil_rig.exe --port 45700 --ticks 6000 \
    --scenario nominal --capture /tmp/dshot.bin

# terminal 2 - the real application behind the HIL HAL
02_firmware/build-hil/fc_hil.exe --port 45700 --ticks 6000

# analyse what the "ESC" saw
python 07_simulation/hardware_in_the_loop/dshot_analyzer.py /tmp/dshot.bin
```

Both sides print machine-greppable metric lines (`hil_fc: ...`, `rig: ...`) and
the rig keeps serving control queries for a short grace period after the last
tick. The regression runner (`08_testing/regression_tests/run_regression.sh`)
automates six of these runs — see HIL_DESIGN.md §5 for the recorded results.

**This is not hardware.** A byte link is not an ESC, a SPI bus or a UART; see
HIL_DESIGN.md §7 for what that does and does not prove.