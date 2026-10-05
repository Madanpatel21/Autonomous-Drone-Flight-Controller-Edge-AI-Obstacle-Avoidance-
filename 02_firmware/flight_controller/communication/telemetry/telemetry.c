#include "telemetry.h"
#include "crc16.h"
#include "mavlink2.h"
#include <string.h>

/* Telemetry implementation (Phase 19, COM-002/COM-004). Deterministic, bounded,
 * no allocation. Binary status record v2 (Phase 24 added `action`):
 *   MAGIC | VER | TYPE | LEN | SEQ | TIMESTAMP_US u64 | payload | CRC16
 * payload: attitude q w,x,y,z f32 | vel NED f32 x3 | alt f32 | min cell f32 |
 *          current f32 | mode u8 | failsafe u8 | perception u8 | avoidance u8 |
 *          companion_ok u8 | action u8                                (53 B) */

#define TELEM_STATUS_PAYLOAD 53u
#define TELEM_HDR            6u      /* MAGIC VER TYPE LEN SEQ (+1 pad reserved) */
#define TELEM_FRAME_MAX      (TELEM_HDR + 8u + TELEM_STATUS_PAYLOAD + 2u)

static void put_f32(uint8_t *p, float f)
{
    uint32_t b;
    __builtin_memcpy(&b, &f, 4);
    p[0] = (uint8_t)(b & 0xFFu);
    p[1] = (uint8_t)((b >> 8) & 0xFFu);
    p[2] = (uint8_t)((b >> 16) & 0xFFu);
    p[3] = (uint8_t)((b >> 24) & 0xFFu);
}

static float get_f32(const uint8_t *p)
{
    uint32_t b = mav_get_u32(p);
    float f;
    __builtin_memcpy(&f, &b, 4);
    return f;
}

size_t telemetry_build_status(const telemetry_snapshot_t *s, uint8_t *out,
                              size_t cap, uint8_t *seq_inout)
{
    if (!s || !out) return 0;
    if (cap < TELEM_FRAME_MAX) return 0;

    memset(out, 0, TELEM_FRAME_MAX);
    out[0] = TELEM_MAGIC;
    out[1] = TELEM_SCHEMA_VER;
    out[2] = TELEM_REC_STATUS;
    out[3] = (uint8_t)TELEM_STATUS_PAYLOAD;
    out[4] = seq_inout ? *seq_inout : 0u;
    /* out[5] reserved (0) — keeps the header word-aligned */
    for (int i = 0; i < 8; i++) {
        out[6 + i] = (uint8_t)((s->timestamp_us >> (8 * i)) & 0xFFu);
    }

    uint8_t *p = &out[14];
    put_f32(&p[0],  s->attitude.w);
    put_f32(&p[4],  s->attitude.x);
    put_f32(&p[8],  s->attitude.y);
    put_f32(&p[12], s->attitude.z);
    put_f32(&p[16], s->vel_ned_ms.x);
    put_f32(&p[20], s->vel_ned_ms.y);
    put_f32(&p[24], s->vel_ned_ms.z);
    put_f32(&p[28], s->altitude_m);
    put_f32(&p[32], s->min_cell_v);
    put_f32(&p[36], s->current_a);
    p[40] = s->mode;
    p[41] = s->failsafe;
    p[42] = s->perception_mode;
    p[43] = s->avoidance_mode;
    p[44] = s->companion_healthy ? 1u : 0u;
    p[45] = s->action;

    size_t total = TELEM_HDR + 8u + TELEM_STATUS_PAYLOAD;
    uint16_t crc = crc16_ccitt(out, total);   /* covers header+timestamp+payload */
    out[total]     = (uint8_t)(crc & 0xFFu);
    out[total + 1] = (uint8_t)(crc >> 8);
    total += 2u;

    if (seq_inout) *seq_inout = (uint8_t)(*seq_inout + 1u);
    return total;
}

bool telemetry_parse_status(const uint8_t *buf, size_t len,
                            telemetry_snapshot_t *out)
{
    if (!buf || !out || len < TELEM_HDR + 8u) return false;
    if (buf[0] != TELEM_MAGIC) return false;
    if (buf[1] != TELEM_SCHEMA_VER) return false;   /* refuse unknown schema */
    if (buf[2] != TELEM_REC_STATUS) return false;
    if (buf[3] != TELEM_STATUS_PAYLOAD) return false;
    size_t total = TELEM_HDR + 8u + TELEM_STATUS_PAYLOAD;
    if (len < total + 2u) return false;

    uint16_t crc = crc16_ccitt(buf, total);
    uint16_t got = (uint16_t)((uint16_t)buf[total] |
                              ((uint16_t)buf[total + 1] << 8));
    if (crc != got) return false;

    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 8; i++) {
        out->timestamp_us |= (uint64_t)buf[6 + i] << (8 * i);
    }
    const uint8_t *p = &buf[14];
    out->attitude.w = get_f32(&p[0]);
    out->attitude.x = get_f32(&p[4]);
    out->attitude.y = get_f32(&p[8]);
    out->attitude.z = get_f32(&p[12]);
    out->vel_ned_ms.x = get_f32(&p[16]);
    out->vel_ned_ms.y = get_f32(&p[20]);
    out->vel_ned_ms.z = get_f32(&p[24]);
    out->altitude_m   = get_f32(&p[28]);
    out->min_cell_v   = get_f32(&p[32]);
    out->current_a    = get_f32(&p[36]);
    out->mode            = p[40];
    out->failsafe        = p[41];
    out->perception_mode = p[42];
    out->avoidance_mode  = p[43];
    out->companion_healthy = p[44] != 0u;
    out->action            = p[45];
    return true;
}

size_t telemetry_emit_mavlink(uint8_t msgid, const telemetry_snapshot_t *s,
                              uint8_t *seq_inout, uint8_t *out, size_t cap)
{
    if (!s || !out) return 0;
    uint8_t seq = seq_inout ? *seq_inout : 0u;
    uint8_t pl[MAVLINK_MAX_PAYLOAD];
    size_t len = 0;

    switch (msgid) {
    case MAVLINK_MSG_HEARTBEAT: {
        /* custom_mode u32 | type u8 | autopilot u8 | base_mode u8 |
         * system_status u8 | mavlink_version u8 */
        mav_put_u32(&pl[0], 0);
        pl[4] = 2u;    /* MAV_TYPE_QUADROTOR */
        pl[5] = 3u;    /* MAV_AUTOPILOT_ARDUPILOTME */
        pl[6] = (uint8_t)(1u << 7);              /* base_mode: custom_mode set */
        /* v2 (Phase 24): a failsafe whose action is a motor stop is not a
         * recoverable state, so any failsafe OR any safety action publishes
         * MAV_STATE_CRITICAL rather than merely "failsafe set". */
        pl[7] = (uint8_t)((s->failsafe || s->action) ? 4u : 0u); /* MAV_STATE_CRITICAL */
        pl[8] = 3u;    /* MAVLINK_VERSION */
        len = 9u;
        break;
    }
    case MAVLINK_MSG_ATTITUDE: {
        /* time_boot_ms u32 | roll | pitch | yaw | rollspeed | pitchspeed |
         * yawspeed (7 f32). Euler angles are placeholders (0) in this phase:
         * the quaternion is the authoritative attitude record; Phase 20 adds
         * the flight-mode->Euler conversion at the GS decode side. */
        mav_put_u32(&pl[0], (uint32_t)(s->timestamp_us / 1000u));
        put_f32(&pl[4],  0.0f);
        put_f32(&pl[8],  0.0f);
        put_f32(&pl[12], 0.0f);
        put_f32(&pl[16], 0.0f);
        put_f32(&pl[20], 0.0f);
        put_f32(&pl[24], 0.0f);
        len = 28u;
        break;
    }
    case MAVLINK_MSG_LOCAL_POSITION_NED: {
        /* time_boot_ms u32 | x y z vx vy vz (7 f32) */
        mav_put_u32(&pl[0], (uint32_t)(s->timestamp_us / 1000u));
        put_f32(&pl[4],  0.0f);
        put_f32(&pl[8],  0.0f);
        put_f32(&pl[12], -s->altitude_m);     /* NED: down-positive */
        put_f32(&pl[16], s->vel_ned_ms.x);
        put_f32(&pl[20], s->vel_ned_ms.y);
        put_f32(&pl[24], s->vel_ned_ms.z);
        len = 28u;
        break;
    }
    case MAVLINK_MSG_SYS_STATUS: {
        /* sensors_present/enabled/health u32 x3 | load u16 | voltage_battery
         * u16 (cV) | current_battery i16 (dA) | battery_remaining i8 |
         * drop_rate_comm u16 | errors_comm u16 | errors_count1..4 u16 = 31 B.
         * Only the fields this FC owns are filled (documented in ICD-03). */
        memset(pl, 0, 31u);
        int32_t cv = (int32_t)(s->min_cell_v * 100.0f);
        if (cv < 0) cv = 0;
        if (cv > 65535) cv = 65535;
        int32_t da = (int32_t)(s->current_a * 100.0f);
        if (da < -32768) da = -32768;
        if (da > 32767) da = 32767;
        pl[14] = (uint8_t)(cv & 0xFF);      /* voltage_battery u16 LE @14 */
        pl[15] = (uint8_t)((cv >> 8) & 0xFF);
        pl[16] = (uint8_t)(da & 0xFF);      /* current_battery i16 LE @16 */
        pl[17] = (uint8_t)((da >> 8) & 0xFF);
        pl[18] = 100;                       /* battery_remaining: FC owns no fuel gauge */
        len = 31u;
        break;
    }
    default:
        return 0;
    }

    size_t out_len = 0;
    if (!mavlink_encode(msgid, MAVLINK_SYS_ID, MAVLINK_COMP_ID_FC, seq,
                        pl, (uint8_t)len, out, cap, &out_len)) {
        return 0;
    }
    if (seq_inout) *seq_inout = (uint8_t)(seq + 1u);
    return out_len;
}
