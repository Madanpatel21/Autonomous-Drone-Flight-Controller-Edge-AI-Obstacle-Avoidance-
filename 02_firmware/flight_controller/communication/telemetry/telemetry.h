#ifndef TELEMETRY_H
#define TELEMETRY_H
#include "fc_types.h"

/* FC telemetry (COM-002/COM-004, ICD-03). Two outputs:
 *
 *  1. Companion/ground binary status record: MAGIC 'T'(0x54) | SCHEMA_VER |
 *     record type | LEN | SEQ | TIMESTAMP_US(u64) | payload | CRC16. The
 *     schema version is explicit so a Phase-20 ground station can refuse
 *     mismatched records instead of mis-reading fields (COM-003 versioning
 *     applied to telemetry).
 *  2. MAVLink v2 frames (mavlink2.h) for the ground station: HEARTBEAT,
 *     ATTITUDE, LOCAL_POSITION_NED, SYS_STATUS — emitted, not parsed here;
 *     the GS owns the decode side (Phase 20).
 *
 * COM-004: failsafe state is always part of the status payload, so the GS
 * can never be the only place safety state exists.
 *
 * Schema v2 (Phase 24) adds `action` (fc_safety_action_t). v1 reported only the
 * failsafe NAME, which meant a motor-stop caused by a dead IMU was published as
 * "RC_LOSS -> returning to land" — the operator was told the vehicle was flying
 * itself home while it had no control authority at all. The action is the fact
 * an operator actually needs, and the GS raises a CRITICAL when it is
 * MOTOR_STOP. */

#define TELEM_MAGIC        0x54u   /* 'T' */
#define TELEM_SCHEMA_VER   2u
#define TELEM_REC_STATUS   1u

typedef struct {
    uint64_t timestamp_us;
    fc_quat_t attitude;
    fc_vec3_t vel_ned_ms;
    float altitude_m;
    float min_cell_v;
    float current_a;
    uint8_t mode;              /* fc_mode_t */
    uint8_t failsafe;          /* fc_failsafe_t */
    uint8_t perception_mode;   /* perception_mode_t */
    uint8_t avoidance_mode;    /* avoid_mode_t */
    uint8_t companion_healthy; /* bool */
    uint8_t action;            /* fc_safety_action_t (schema v2) */
} telemetry_snapshot_t;

/* Build the binary status record. Returns the frame length, or 0 on
 * insufficient buffer. */
size_t telemetry_build_status(const telemetry_snapshot_t *s, uint8_t *out,
                              size_t cap, uint8_t *seq_inout);

/* Parse a status record (round-trip + version check). */
bool telemetry_parse_status(const uint8_t *buf, size_t len,
                            telemetry_snapshot_t *out);

/* Emit one MAVLink v2 frame into out. Returns frame length or 0. */
size_t telemetry_emit_mavlink(uint8_t msgid, const telemetry_snapshot_t *s,
                              uint8_t *seq_inout, uint8_t *out, size_t cap);

#endif /* TELEMETRY_H */
