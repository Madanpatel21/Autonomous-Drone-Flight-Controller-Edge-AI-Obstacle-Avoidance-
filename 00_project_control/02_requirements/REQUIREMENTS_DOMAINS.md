# Requirements — All Domains (authoritative baseline)

Status: APPROVED (2026-10-03). Each requirement: unique ID, single "shall", acceptance criterion, verification method (A/I/R/T/H per REQUIREMENT_ID_SCHEME.md).

## System (SYS)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| SYS-001 | FC shall stabilize attitude at ≥500 Hz effective rate using IMU data ≤2 ms old. | SIM: rate loop holds attitude within ±2° with injected noise; log shows scheduler period p99 ≤2 ms. | T (+H later) |
| SYS-002 | FC shall keep total control latency (IMU sample → motor command) ≤5 ms p99. | Logged timestamps; p99 ≤5 ms in SIL. | T |
| SYS-003 | FC shall detect RC link loss within ≤500 ms and enter failsafe. | SIM fault injection: RC stop → failsafe entry ≤500 ms. | T |
| SYS-004 | FC shall detect loss of valid perception ≤1 s and fall back to FC-only modes. | Companion heartbeat timeout ≤1 s triggers fallback state. | T |
| SYS-005 | FC shall operate from a 4S Li-ion/LiPo pack with brownout-safe behavior down to per-cell 3.0 V. | Load analysis + SIM battery model; low-battery failsafe thresholds per SAF-030/031. | A |
| SYS-006 | FC shall survive power interruption with retained config/calibration. | Flash parameter write/read cycle test. | T (SIM), H later |
| SYS-007 | Vehicle mass ≤2.0 kg takeoff, 5–6" prop, quad-X; endurance ≥10 min hover. | Mass budget + motor/prop static-thrust calc. | A (+H later) |

## Hardware (HW)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| HW-001 | MCU shall be STM32F/H-series with FPU, ≥168 MHz, ≥1 MB flash, ≥192 KB RAM, DShot-capable timers. | Component baseline (DEC-002). | R |
| HW-002 | IMU shall provide gyro+accel ≥1 kHz (accel ≥400 Hz) on SPI. | Datasheet review; driver meets sample rate in SIM timing test. | R/T |
| HW-003 | Barometer shall provide pressure ≤10 Hz with noise enabling ±0.5 m hover hold. | Datasheet review + estimator covariance budget. | A |
| HW-004 | GNSS shall support ≥10 Hz solution, GPS+GLONASS/Galileo. | Datasheet review. | R |
| HW-005 | Range sensing: ToF forward + downward, ≥4 m range, ≥50 Hz. | Datasheet review. | R |
| HW-006 | ESC interface: DShot600 primary, PWM fallback. | Driver supports both; protocol per BLHeli_32/AM32 reference. | T (SIM) |
| HW-007 | Battery monitoring: per-cell voltage via ADC divider or stack monitor, current via shunt/hall. | Simulated measurement error model defined; HW accuracy verified later. | T |
| HW-008 | Companion computer connects via UART (≥1 Mbaud) + camera over dedicated interface; power-cyclable by FC. | ICD (DEC-003). | R |
| HW-009 | All connectors keyed/polarized; power path protected (TVS + fuse + reverse protection). | Schematic review checklist. | R (hardware-gated) |
| HW-010 | Custom PCB 4-layer with IMU isolation and IMU-in-plane-of-prop-rotation placement. | PCB constraint doc. | R (hardware-gated) |

## Firmware (FW)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| FW-001 | Single shared flight-control application builds for SIM and STM32 via HAL; no algorithm file includes backend headers. | Include audit script: 0 violations. | T |
| FW-002 | Scheduler: fixed-rate tasks with priority + deadline monitoring; watchdog-backed. | Timing test logs p99/p99.9. | T |
| FW-003 | All drivers return typed status; bus faults increment health counters, never block control. | Unit test: bus fault → sensor marked unhealthy, control continues. | T |
| FW-004 | Deterministic behavior: no dynamic heap in control path; fixed allocation. | Static review + code audit. | R |
| FW-005 | Parameters stored in flash with CRC; safe defaults on corruption. | Unit test: corrupt → defaults load. | T |
| FW-006 | Logging (blackbox) does not exceed bus/CPU budget; drops logging before control under load. | Stress test: overload → control p99 unaffected. | T |
| FW-007 | Bootloader: application CRC check before jump; recovery via pin/boot. | Unit test on SIM mock flash. | T |

## Sensors (SEN)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| SEN-001 | Every sensor: driver→calibration→validation→filtering→timestamp→health pipeline. | Code audit + unit tests per stage. | T |
| SEN-002 | Validation rejects range violations, NaN, impossible jumps, stale data (configurable timeouts). | Unit tests each rejection class. | T |
| SEN-003 | IMU calibration: gyro bias at rest, accel 6-face; stored with CRC, applied transparently. | Unit test on synthetic data; residual bias < threshold. | T |
| SEN-004 | Sensor health: timeout/range/error counters; unhealthy sensors flagged to estimator+failsafe. | Unit test: forced timeout → unhealthy flag ≤1 cycle + failsafe escalation. | T |
| SEN-005 | Data path never blocks: worst-case driver execution bounded and measured. | SIM timing harness. | T |

## Estimation (EST)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| EST-001 | Attitude: quaternion EKF (gyro propagation, accel + mag correction), gyro bias estimated. | SIM convergence <5 s from ±30° initial error; RMSE <2°. | T |
| EST-002 | Altitude/vertical velocity: baro + accel fusion with bias estimation. | SIM: hover drift <±0.5 m over 60 s. | T |
| EST-003 | Position/velocity: GNSS + optical flow + IMU prediction, with innovation gating. | SIM: steady-state NE error <1.5 m with 10 Hz GNSS. | T |
| EST-004 | Outlier rejection: innovation chi-square gating before fusion update. | Unit test: injected outlier → rejected, no estimate jump >3σ. | T |
| EST-055 | Estimator health: divergence/failure detection → failsafe notification. | SIM: forced divergence → EST_UNHEALTHY event. | T |
| EST-006 | All estimator outputs carry timestamps + health flags; consumers check them. | Code audit. | R |

## Control (CTRL)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| CTRL-001 | Cascade: position → velocity → attitude → rate; PIDs with derivative-on-measurement, anti-windup, output saturation. | Unit tests each property. | T |
| CTRL-002 | Rate loop ≥500 Hz; attitude ≥250 Hz; position ≥50 Hz; deadlines monitored. | Scheduler logs. | T |
| CTRL-003 | Mixer: quad-X with per-motor saturation minimization (priority: collective, then stability axes). | Unit test: at saturation, no sign flip / zero collective error. | T |
| CTRL-004 | Arming requires: healthy IMU+est, RC live, mode valid, level attitude, no critical failsafe. | State machine unit tests. | T |
| CTRL-005 | Control output zero on disarm (motor stop). | Unit test. | T |
| CTRL-006 | Parameter changes take effect at loop boundaries, not mid-loop. | Unit test. | T |

## Navigation (NAV)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| NAV-001 | Mission: waypoints with poshold arrival tolerance, auto RTL on mission end or failsafe. | SIM mission run: all waypoints reached ±2 m; RTL lands. | T |
| NAV-002 | Obstacle avoidance: lateral velocity limit + stop/avoid from fused perception; AI input is advisory to FC. | SIM avoidance scenario with virtual obstacles. | T |
| NAV-003 | Takeoff/landing state machines with altitude guards. | SIM tests. | T |
| NAV-004 | Geofence: ceiling/floor/radius; violation → RTL or land. | SIM: fence breach → RTL. | T |

## Communication (COM)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| COM-001 | RC input: ≥4 channels, failsafe detect per protocol spec (e.g. CRSF/SBUS), invalid-frame handling. | SIM protocol unit tests. | T |
| COM-002 | Telemetry: MAVLink v2 telemetry to GS; degraded link tolerated. | Unit tests: encode/decode round-trip; dropped-packet stats. | T |
| COM-003 | Companion link: framed binary protocol with CRC, sequence numbers, heartbeat ≤1 Hz, versioned schema. | Unit tests: CRC error, out-of-order, version mismatch. | T |
| COM-004 | FC is master of safety data: FC telemetry includes failsafe state; GS commands cannot clear arming gates. | Unit test: GS can't arm through link. | T |

## Ground station (GS)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| GS-001 | GS displays attitude, position, battery, mode, failsafe, waypoints. | SIM feed review. | T |
| GS-002 | GS provides parameter read/write with confirmation. | Unit test. | T |
| GS-003 | Emergency stop action always available at GS and RC. | Unit test on command path. | T |

## Simulation (SIM)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| SIM-001 | Virtual IMU/baro/GNSS/ToF/flow/battery/RC/motors with noise, bias, drift, latency, dropout, failure injection. | Unit tests each fault class. | T |
| SIM-002 | Deterministic sim runs (fixed seed) reproduce bit-identical telemetry. | Double-run diff = zero. | T |
| SIM-003 | Physics: rigid-body quad with motor/thrust model adequate for control validation. | Open-loop thrust/mass sanity; closed-loop hover convergence. | T |
| SIM-004 | Fault scenarios scripted + replayable from log for regression. | Scenario files run in CI. | T |

## Safety (SAF)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| SAF-001 | Failsafe hierarchy: RC loss > IMU failure > battery > geofence > companion loss. | State machine review + unit tests. | R/T |
| SAF-002 | All failsafe actions bounded: hover-in-place, RTL, descent-land, emergency-stop-motors. | Unit tests each action. | T |
| SAF-003 | No single sensor/estimator failure causes uncommanded motor surge. | Fault injection battery. | T |
| SAF-004 | Watchdog: control task deadline miss resets MCU; SIM logs instead. | SIM test + HW design. | T/H |
| SAF-005 | Arming/disarming interlocks: throttle-zero + stable + healthy. | Unit tests. | T |
| SAF-030 | Low-battery threshold: first warning at 3.5 V/cell, action at 3.4 V/cell (RTL), critical at 3.1 V/cell (land). | Unit test on thresholds. | T |
| SAF-031 | Battery failsafe cannot be disabled below critical. | Unit test. | T |
| SAF-040 | AI output can only be applied within pre-bounded velocity/accel limits; hard limits are FC-owned constants. | Unit test: AI command beyond limit → clamped. | T |
| SAF-041 | AI/estimator conflict: FC reverts to FC-only sensors when AI geometry is inconsistent with FC estimate. | SIM test. | T |

## Edge-AI (AI)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| AI-001 | Perception shall publish obstacle detections (position/velocity/geometry/confidence) at ≥10 Hz with bounded latency ≤100 ms (sensor→message). | Interface test on recorded data. | T |
| AI-002 | AI frame shall never command attitude/rate directly; only avoidance vectors/velocity setpoints. | Code review. | R |
| AI-003 | Model size/latency budget: ≤10 ms/frame on companion GPU/CPU; documented benchmark. | Benchmark script + report. | T |
| AI-004 | AI failure modes: no inference, stale frame, low confidence → classified and published as health state. | Unit tests. | T |
| AI-005 | Training/validation dataset with documented statistics; no fabricated metrics — model versions recorded with eval results. | Dataset manifest + model card. | R/T |

## Perception fusion (PERC, Phase 17)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| PERC-001 | FC shall maintain a bounded (≤16) obstacle picture fused from AI sets + FC ToF + vehicle state, each entry with confidence, source tag and FC-domain timestamp. | Unit tests on structure/bounds. | T |
| PERC-002 | Stale (AI >200 ms per ICD-02, ToF >100 ms) or invalid sets (zero time, count>16, confidence <0.5, out-of-band range) shall be excluded from the picture — stale geometry never stays valid. | Unit tests each invalidity class. | T |
| PERC-003 | Degraded modes NONE/TOF_ONLY/AI_ONLY/FUSED; loss of every perception source (companion dead >1 s) shall NOT escalate a failsafe (DEC-006/007: advisory-only); FC-only sensors carry the picture when the companion dies. | Unit + app-level ai_loss scenario. | T |
| PERC-004 | AI geometry inconsistent with FC ranging in the ±20° forward cone (ToF closer by >1 m) shall be clamped to the FC range with 50% confidence downgrade (SAF-041, DEC-013); ToF-only returns inside 8 m shall publish when AI misses the cone. | Unit test on conflict + synthesis. | T |

## Obstacle avoidance (AVD, Phase 18)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| AVD-001 | Collision risk shall be estimated from the perception picture: nearest obstacle inside a ±45° forward cone drives a range-based brake curve (SLOW below 4 m) and a hard stop (forward command = 0 below 2 m). | Unit tests on mode boundaries. | T |
| AVD-002 | Every published avoidance velocity component shall be clamped to FC-owned limits (fwd ≤2.0 m/s, lateral ≤1.0 m/s, vertical = 0) as the LAST step before publish (SAF-040, DEC-014). | Unit test with adversarial inputs. | T |
| AVD-003 | With an invalid/empty perception picture or a threat beyond the brake envelope, avoidance shall be inactive with a zero command (deterministic degraded behavior); avoidance state shall never escalate a failsafe (DEC-007). | Unit + app-level scenario. | T |

## Testing (TEST)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| TEST-001 | All software requirements have automated tests on SIM/SIL; regression suite runs on every change. | CI-style runner: 0 failures. | T |
| TEST-002 | Hardware-gated tests explicitly marked `H` and skipped with clear message when no hardware. | Runner output. | R/T |
| TEST-003 | Coverage: unit tests exist for math, filters, PID, fusion, mixer, protocol, state machines. | Test report. | T |
| TEST-004 | Traceability: every requirement maps to ≥1 test/evidence row in TRACEABILITY_MATRIX. | Matrix audit. | R |

## Manufacturing (MFG) / Operations (OPS)
| ID | Requirement | Acceptance | Ver |
|---|---|---|---|
| MFG-001 | Release package: gerbers, BOM, pick&place, firmware binary, parameter default file, docs; version-matched. | Manifest audit. | R |
| OPS-001 | Flight test requires: FRR sign-off, motor-off bench pass, geofence, RC failsafe verified. | Checklist evidence. | R |

## Traceability
Requirement → design → implementation → test → evidence maintained in `00_project_control/01_governance/TRACEABILITY_MATRIX.md`.
