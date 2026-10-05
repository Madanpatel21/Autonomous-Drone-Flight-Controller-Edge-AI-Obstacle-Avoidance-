# 04_sensor_fusion — estimation and sensor fusion

| What | Where |
|---|---|
| Estimator validation: attitude, altitude, position, noise, drift | [`02_firmware/02_estimation/STATE_ESTIMATION_VALIDATION.md`](../../02_firmware/02_estimation/STATE_ESTIMATION_VALIDATION.md) |
| Sensor validation / calibration / health pipeline | [`02_firmware/01_drivers/DRIVER_SPEC_TEMPLATE.md`](../../02_firmware/01_drivers/DRIVER_SPEC_TEMPLATE.md) (template) and the code below |
| Frames and conventions (**body FLU, Hamilton quaternions**) | `DEC-008` in [`00_project_control/DECISION_LOG.md`](../../00_project_control/DECISION_LOG.md) |
| Simulation-side sensor models and injection | [`07_simulation/SIMULATION_VALIDATION.md`](../../07_simulation/SIMULATION_VALIDATION.md) |

Code: `02_firmware/flight_controller/estimation/{est_attitude.c,est_alt.c}`,
`drivers/{sensor_hub.c,validation.c,calibration.c,health.c}`.

## What is verified, and against what

The SIM tests feed known-truth sensor data through the real pipeline, so the
evidence is "the filter behaves as specified on a simulated sensor", not "the
filter is accurate in flight". The distinction is recorded per requirement in
[`08_testing/VERIFICATION_REPORT.md`](../../08_testing/VERIFICATION_REPORT.md):

* `EST-001` **PARTIAL** — implemented is a complementary filter with gyro-bias
  estimation. The requirement text asks for a quaternion EKF with magnetometer
  correction; there is no magnetometer in the design. The deviation is named, not
  reinterpreted.
* `EST-003` **PARTIAL** — optical flow is a `NOT_READY` stub, so horizontal
  position is GNSS-only.
* Attitude/altitude fusion, divergence re-anchoring and 60 s altitude drift are
  **MET (SIM)** with measured numbers in the validation record.

## Read next

* Control loop that consumes these estimates: [`../05_flight_control/README.md`](../05_flight_control/README.md)
* Navigation that consumes position: [`../06_navigation/README.md`](../06_navigation/README.md)
