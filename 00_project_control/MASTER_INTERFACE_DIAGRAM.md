# Master Interface Diagram

Status: APPROVED 2026-10-03 (Phase 03).

```text
                            GROUND STATION
                                   | MAVLink v2 (telemetry radio)
                                   v
   RC TX ──CRSF──┐          +------------------+
                 |          |  FLIGHT CONTROLLER (STM32F4/H7, custom PCB)  |
                 v          |  +------------------------+                  |
              RC RX ───────►|  | scheduler + failsafe   |                  |
                            |  +-----------+------------+                  |
   IMU 1kHz ──SPI+DMA──────►|  | sensors  | estimation  | control | mixer |──►DShot600──► ESCs x4 ──► motors
   BARO ──────SPI/I2C──────►|  +------------------------+                  |
   GNSS 10Hz ─UART─────────►|   battery monitor ◄──ADC── shunt+divider     |
   TOF x2 ────I2C/UART─────►|   watchdog(IWDG) ◄── rate_control deadline   |
   FLOW ──────SPI──────────►|   blackbox logging (flash)                   |
                            +---------------+---------------+--------------+
                                            | UART ≥1 Mbaud (ICD-02)
                                            v
                            +------------------------------+
                            | COMPANION (Edge-AI)          |
                            |  camera ──► inference ──►    |
                            |  obstacle sets + heartbeat   |
                            +------------------------------+
```

Data-flow precedence for actuation: RC link → mode manager → cascade controllers → mixer → DShot. Perception path never connects to actuators except through bounded avoidance setpoints (SAF-040).
