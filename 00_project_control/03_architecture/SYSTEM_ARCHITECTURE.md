# System Architecture

Status: APPROVED 2026-10-03 (Phase 03).

## System context

```text
   Ground station (MAVLink v2, telemetry radio)
        |                                      RC transmitter (CRSF)
        v                                            |
   [Telemetry UART]                            [RC UART]
        |                                            |
        |          +-----------------+               |
        +----------- FC (STM32, custom PCB) ----------+
                   |  sensors: IMU SPI, baro, GNSS, ToF, flow, battery ADC
                   |  actuators: DShot600 ×4
                   +-----------------+------------------+
                                     | UART ≥1 Mbaud (ICD-02)
                                     v
                   Companion computer (Edge-AI perception)
                                     | camera
                                     v
                                  environment
```

## Safety boundary (DEC-007, SAF-040/041)

- FC owns: attitude/altitude/position control, arming, failsafe, motor outputs, geofence.
- Companion owns: perception only. Publishes obstacle sets (bounded, confidence-tagged, timestamped).
- FC consumes AI output only inside bounded avoidance limits; perception staleness/conflict → FC-only fallback (SYS-004).

## Architecture documents

| Document | Content |
|---|---|
| 02_firmware/00_architecture/FIRMWARE_ARCHITECTURE.md | layering, scheduler, task table |
| 01_hardware/00_system/CLOCK_AND_TIMING_BUDGET.md | CPU/latency/sensor/memory budgets |
| 00_project_control/INTERFACE_CONTROL_DOCUMENT.md | all frozen interfaces |
| 00_project_control/MASTER_INTERFACE_DIAGRAM.md | data-flow diagram |

## Verification of this phase

Acceptance "no critical architectural contradiction": cross-checked SYS/HW/FW/SAF/AI requirements vs task table vs ICD — consistent (latency chain sums within SYS-002; task rates cover sensor staleness limits; failsafe monitors fit 100 Hz slot).
