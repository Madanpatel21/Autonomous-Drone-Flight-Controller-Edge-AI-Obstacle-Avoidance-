# PCB Bring-Up Execution Record

Status (2026-10-03, Phase 08): procedure COMPLETE; all physical measurements NOT EXECUTED — no physical board exists. No measurement in this record is simulated or estimated; every value will be entered only after real execution (contract rule 9).

## Stage 1 — Inspection (power OFF)
| # | Check | Method | Result |
|---|---|---|---|
| 1.1 | Visual: solder bridges, polarity, component placement | microscope photos | NOT EXECUTED |
| 1.2 | Net continuity vs netlist | DMM/bed-of-nails | NOT EXECUTED |
| 1.3 | Power-rail shorts to GND | DMM resistance | NOT EXECUTED |

## Stage 2 — Current-limited power-up
| # | Check | Method | Pass criterion |
|---|---|---|---|
| 2.1 | 3.3 V rail @100 mA limit | bench PSU | rail 3.14–3.46 V, I < 100 mA |
| 2.2 | 5 V rail @200 mA limit | bench PSU | rail 4.75–5.25 V |
| 2.3 | Full pack power | 4S source | rails in spec, thermals OK |

## Stage 3 — MCU / clocks / debug
| # | Check | Method |
|---|---|---|
| 3.1 | SWD attach | ST-LINK probe |
| 3.2 | HSE/PLL 216 MHz | MCO output / debugger |
| 3.3 | Firmware flash + blink | programmer |

## Stage 4 — Buses & sensors
| # | Check | Pass criterion |
|---|---|---|
| 4.1 | IMU SPI WHO_AM_I | matches DS-000347 value |
| 4.2 | Baro I2C chip ID | matches DS002 |
| 4.3 | GNSS UART NMEA/UBX | valid frames |
| 4.4 | ToF UART/I2C ranges | plausible distances vs tape measure |
| 4.5 | RC CRSF frames | link statistics OK |
| 4.6 | Companion UART | ICD-02 heartbeat |

## Stage 5 — Actuators (MOTORS DISCONNECTED first)
| # | Check | Pass criterion |
|---|---|---|
| 5.1 | DShot600 waveform | scope: correct frame timing |
| 5.2 | ESC telemetry | arm/disarm behavior |
| 5.3 | Motor spin test (props OFF, tethered) | controlled RPM |

## Stage 6 — Full integration
Bench hover readiness per MASTER_VERIFICATION_PLAN.md; FRR gate (08_testing/FLIGHT_TEST_READINESS_REVIEW.md) before any flight.

**Gate rule:** each stage must pass (with recorded measurements/photos) before the next. No stage may be skipped silently.
