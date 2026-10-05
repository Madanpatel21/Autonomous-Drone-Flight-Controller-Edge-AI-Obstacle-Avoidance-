# Communication & Telemetry — Phase 19 Evidence Record

Status: COMPLETE (SIM). SIM-only evidence: protocol codecs and the FC-side link
task are executed and tested against the deterministic virtual companion; the
physical UART/RC/telemetry electrical path is H-gated (USART6/USART3, CRSF
radio link) and NOT executed — no board, no radio hardware.

## 1. Inputs / assumptions

- ICD-02 (companion), ICD-03 (MAVLink v2 ground station), ICD-04 (RC: CRSF
  primary, SBUS fallback), COM-001..004, DEC-003 (framed companion protocol).
- Frame codec lives in `common/protocols/` so the FC link task and the SIM
  virtual companion share one implementation (no duplicated codec, no
  backend → application dependency, DEC-001 boundary preserved).
- Companion companion-side health fields (cpu/mem/temp/camera/inference_ms)
  are sim-class values; real companion telemetry is Phase 19+ hardware/companion
  work and is not claimed here.

## 2. Work performed

- `common/protocols/icd02_frame.{c,h}` — ICD-02 codec (DEC-015):
  `SYNC(0xA5 0x5A) | LEN | TYPE | SEQ | PAYLOAD | CRC16-CCITT over [LEN,TYPE,SEQ,PAYLOAD]`,
  resynchronizing parser, OBSTACLE_SET payload v1 codec
  (`count | radius_cm[n] | conf_pct[n] | class[n] | per detection pos i16 cm x3 + vel i16 cm/s x3`).
- `flight_controller/communication/companion_link/companion_link.{c,h}` — link
  task: 1024-byte RX ring, frame dispatch (HEARTBEAT/OBSTACLE_SET/HEALTH,
  unknown types counted and ignored), sequence tracking (forward gaps counted
  as loss, late frames as reorder), ring-overflow counter, heartbeat health
  window (1 s, SYS-004), and the FC→companion 0x10 FC_STATE builder (44-byte
  payload: timestamp, attitude quaternion, NED velocity, altitude, mode,
  failsafe, perception mode, avoidance mode).
- `common/protocols/mavlink2.{c,h}` — MAVLink v2 subset (HEARTBEAT, SYS_STATUS,
  ATTITUDE, LOCAL_POSITION_NED) with per-message payload length and CRC_EXTRA,
  the protocol's CRC-16/MCRF4XX algorithm (distinct from the project's CCITT),
  resynchronizing parser, and rejection of unknown ids/length mismatches.
- `communication/rc/rc_protocol.{c,h}` — CRSF frame parser (CRC-verified,
  spec 11-bit two-channel-per-3-bytes packing, FLIGHT_MODE arm switch,
  885–1795 µs → 1000–2000 normalization) and SBUS 25-byte parser (start/end
  byte validation, flags bit2 = link failsafe).
- `communication/telemetry/telemetry.{c,h}` — versioned binary status record
  (`MAGIC 'T' | SCHEMA_VER | TYPE | LEN | SEQ | TIMESTAMP_US | payload | CRC16`,
  52-byte payload incl. failsafe state — COM-004: safety state is always on the
  wire) with parse-time version/CRC rejection, plus MAVLink v2 emitters for
  the GS message set.
- SIM: `hal_companion_uart_read()` emits REAL ICD-02 frames (HEARTBEAT 1 Hz,
  OBSTACLE_SET 25 Hz, HEALTH 1 Hz, `ai_loss` stops the stream) so the FC path
  exercises serialize → ring → CRC → parse → `perception_feed_ai` exactly as
  it will over USART6 + DMA. App: 100 Hz link RX task added.
- App 50 Hz: link-parse replaces the previous direct AI feed (Phase 17 hook
  `hal_companion_ai_read` remains available for unit tests only).

## 3. Files created / modified

- Created: `common/protocols/{icd02_frame,mavlink2}.{c,h}`,
  `communication/companion_link/companion_link.{c,h}`,
  `communication/rc/rc_protocol.{c,h}`, `communication/telemetry/telemetry.{c,h}`,
  this record.
- Modified: `hal/hal_interfaces.h` (+`hal_companion_uart_read`),
  `hal/sim/hal_sim.c` (virtual companion emits frames), `app_main.c` (100 Hz
  link RX), `tests/test_main.c` (+9 cases), DECISION_LOG (DEC-015), this file,
  TRACEABILITY_MATRIX, MASTER_VERIFICATION_PLAN, TEST_GENERATION, REGRESSION.md,
  PROJECT_STATUS.

## 4. Interfaces affected

- `hal_interfaces.h`: +`hal_companion_uart_read()` (SIM byte source; STM32
  replaces it with USART6 + DMA RX — no application change).
- `perception_fusion.h`: unchanged; the link task now calls
  `perception_feed_ai` (previously the app fed it directly).
- New consumer contract: `companion_healthy()` is the SYS-04 health source
  (advisory; never escalates a failsafe by itself — DEC-006/007).
- Ground station: consumes `telemetry_emit_mavlink` output over ICD-03; the
  decode/UI side is Phase 20.

## 5. Verification performed

- Unit + app tests (test_main.c, 9 new cases, 766/766 total, exit 0):
  - TEST-COM-CRSF: valid frame → normalized channels (885 µs→1000,
    1795 µs→2000, mid→1500); 1-bit corruption, truncation and null-output
    rejected.
  - TEST-COM-SBUS: valid frame, flags bit2 → `failsafe_active`, bad end byte
    and short buffer rejected.
  - TEST-COM-ICD02: encode→parse round-trip (type/seq/len/payload), CRC
    corruption rejected with a safe drop count, garbage-prefix resync, small
    buffer encode rejected.
  - TEST-COM-OBSTACLE-SET: 2-detection round-trip incl. i16 cm quantization
    (5.50 m, −1.25 m, conf 0.87, radius 0.30, class), truncated/over-count/
    undersized payloads rejected without partial application.
  - TEST-COM-LINK: heartbeat → `companion_healthy()` + ai_state + model_id;
    OBSTACLE_SET → perception geometry (6.00 m ±0.02 m quantization);
    HEALTH decode (42.0 °C, 6 ms); unknown type counted; sequence gap
    counted; corrupted frame counted and not applied; heartbeat timeout
    → unhealthy with NO failsafe change.
  - TEST-COM-TELEMETRY: status record round-trip (all fields), sequence
    advance, explicit schema version in the record, bad magic / wrong schema
    version / corrupted CRC / truncation all rejected.
  - TEST-COM-MAVLINK: HEARTBEAT + SYS_STATUS emit/parse round-trip with
    frame sizes and sysid/compid; voltage_battery decode; CRC corruption
    rejected (consumed=1 resync); unknown id and length-mismatch rejected.
  - TEST-COM-FCSTATE: 44-byte payload, monotonic TX sequence, mode/failsafe/
    perception/avoidance fields present.
  - TEST-COM-BYTEPATH (app-level): 8 s run — >100 frames OK, zero CRC errors,
    zero unknown types, zero sequence gaps, zero ring overflow, companion
    healthy, perception fed.
- Regression runner: exit 0 — build, 766/766, determinism double-run diff = 0,
  7 scenario sequence checks with deadline_misses=0, 2 H-gated SKIPs.

**Defects found and fixed at cause during this phase (regression-locked):**

1. `icd02_encode_obstacle_set` took `uint8_t cap`; passing
   `ICD02_MAX_PAYLOAD` (256) truncated to 0, so every OBSTACLE_SET encode
   silently returned 0 and no AI geometry ever reached the link. Signature is
   now `size_t` with a documented warning.
2. CRSF channel unpacking used one channel per 3 bytes instead of the spec's
   two channels per 3 bytes, and treated `len` as excluding the type byte —
   every parsed channel was garbage. Both the parser and the test encoder now
   follow the spec layout (len includes the type byte).
3. MAVLink `MAVLINK_HEADER_LEN` was 10 instead of 8, leaving two junk bytes
   after every frame's CRC — the trailing CRC byte was outside the declared
   length so corruption there was accepted.
4. HEALTH dispatch required ≥8 payload bytes but the field layout is 7; every
   HEALTH frame was counted and dropped. Also `companion_healthy()` measured
   the heartbeat window against the last *frame* time, so a silent companion
   never expired; it now uses the last poll time.

## 6. Acceptance criteria

| Requirement | Criterion | Status |
|---|---|---|
| COM-001 | RC ≥4 channels, protocol failsafe, invalid-frame handling | **MET (SIM)** — CRSF + SBUS parse/CRC/normalization/failsafe-flag; real radio link H-gated |
| COM-002 | MAVLink v2 telemetry to GS, degraded link tolerated | **MET (SIM)** — encode/parse round-trip, resync, unknown-id rejection; GS UI Phase 20 |
| COM-003 | Companion link: framed, CRC, sequence, heartbeat ≤1 Hz, versioned schema | **MET (SIM)** — ICD-02 codec, seq gap/reorder counters, 1 s heartbeat health, schema version in telemetry record |
| COM-004 | FC master of safety data; GS cannot clear arming gates | **PARTIAL (SIM)** — failsafe state is always in the status record and FC_STATE frame; no GS command path exists yet (Phase 20 adds commands, which must not arm) |
| SYS-004 | Companion loss fallback ≤1 s | **SIM-VERIFIED** (FC side) — `companion_healthy()` expires at 1 s, advisory-only, no failsafe escalation |

## 7. Risks / TBDs

1. No physical link executed: CRSF timing/CRC against a real radio, SBUS
   fallback timing, and UART error rates (framing, overrun, break) are
   untested — H-gated (Phase 08 bring-up, HIL-4).
2. MAVLink v2 here is a four-message subset with no signing and no parameter
   protocol; a real GS (QGroundControl) expects more. Phase 20 must register
   the needed message set and keep the parser registry authoritative.
3. Companion timestamps are FC-clock-domain on the wire; a real companion's
   capture latency is not modelled (DEC-013), so staleness thresholds are
   validated against a simulated zero-latency source.
4. `companion_healthy()` is advisory by decision (DEC-007): the FC keeps flying
   on FC-only sensors. SYS-004's "fallback mode" (POS_HOLD request) is wired
   in `failsafe_mode_request()` but never requested by the link yet — decide
   in Phase 20/24 whether a companion-loss hint should request POS_HOLD.

## 8. Next dependency

Phase 20 — Ground Station (`15_prompts/20_20_ground_station.prompt.md`): a
host-side GS that decodes the ICD-02/MAVLink streams, shows attitude/altitude/
battery/perception/avoidance/failsafe state, and issues bounded commands that
cannot arm (COM-004).
