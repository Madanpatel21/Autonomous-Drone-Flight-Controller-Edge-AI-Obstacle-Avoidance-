# Master Hardware Architecture

Define the final selected components only after interface, availability, power, timing, environmental, and lifecycle analysis.

## Major Domains
- Flight controller MCU
- IMU
- Barometer
- GNSS
- Optional magnetometer
- Optical flow
- ToF/LiDAR
- RC receiver
- Telemetry
- ESC outputs
- Battery voltage/current measurement
- Companion computer
- Camera
- Debug/programming

## Design Rule
Keep high-rate control and safety-critical logic on the MCU.
