# 03_firmware — firmware documentation

| What | Where |
|---|---|
| Application architecture, task/priority table | [`02_firmware/00_architecture/FIRMWARE_ARCHITECTURE.md`](../../02_firmware/00_architecture/FIRMWARE_ARCHITECTURE.md), [`TASK_AND_PRIORITY_TABLE.md`](../../02_firmware/00_architecture/TASK_AND_PRIORITY_TABLE.md) |
| HAL contract (what a backend must implement) | [`02_firmware/hal/interface/HAL_CONTRACT.md`](../../02_firmware/hal/interface/HAL_CONTRACT.md) |
| Backend status (sim / hil / stm32 skeleton) | [`02_firmware/hal/README.md`](../../02_firmware/hal/README.md), [`hal/stm32/README.md`](../../02_firmware/hal/stm32/README.md) |
| Estimation evidence | [`02_firmware/02_estimation/STATE_ESTIMATION_VALIDATION.md`](../../02_firmware/02_estimation/STATE_ESTIMATION_VALIDATION.md) |
| Control evidence | [`02_firmware/03_control/CONTROL_VALIDATION.md`](../../02_firmware/03_control/CONTROL_VALIDATION.md) |
| How to build and run it | [`../NEW_ENGINEER_WALKTHROUGH.md`](../NEW_ENGINEER_WALKTHROUGH.md) |

## Where the code actually is

The numbered directories (`00_architecture`, `01_drivers`, …, `05_failsafe`) hold
**documents**; the compilable application lives in parallel trees. If you are
looking for behaviour, read these:

```
02_firmware/flight_controller/     application: drivers, estimation, control,
                                   navigation, failsafe, communication, app_main.c
02_firmware/common/                shared code: protocols, utilities, types
02_firmware/hal/                   HAL contract + sim/hil/stm32 backends
02_firmware/tests/test_main.c      the unit + integration suite
```

One rule the regression enforces: **no algorithm file may include a HAL backend
header** (`FW-001`), and no dynamic allocation is allowed in the control path
(`FW-004`). `08_testing/audit_requirements.py` re-checks both on every run, so a
violation fails CI rather than being caught in review.

## Honest status

* `sim` and `hil` backends build and run here; the **`stm32` backend has never
  been compiled** (no `arm-none-eabi-gcc`) and is a skeleton.
* Control gains are **sim-class placeholders** pending bench identification
  (`DEC-009`, `DEC-017`) — they are not flight-tuned values.
* Failsafe behaviour is action-based (`failsafe_action()`, `DEC-021`): the action
  says what the vehicle can still do, the failsafe says why. Read
  [`00_project_control/04_safety/SAFETY_CASE.md`](../../00_project_control/04_safety/SAFETY_CASE.md)
  before changing anything on that path.
