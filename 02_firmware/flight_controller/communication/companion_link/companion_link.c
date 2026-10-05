#include "companion_link.h"
#include "crc16.h"
#include "perception_fusion.h"
#include <string.h>

/* Companion link task implementation (Phase 19, COM-003). Deterministic: no
 * RNG, no allocation, bounded ring buffer; the frame codec itself lives in
 * common/protocols/icd02_frame.c (shared with the SIM virtual companion). */

#define CL_RING_SIZE 1024u   /* power of two */

typedef struct {
    uint8_t  ring[CL_RING_SIZE];
    uint16_t head, tail;
    companion_stats_t st;
    bool     have_seq;
    uint8_t  tx_seq;
    uint64_t now_us;       /* last poll time: health windows are measured
                            * against it, not against the last frame (a dead
                            * link stops producing frames entirely) */
} link_t;

static link_t L;

static void ring_push(uint8_t b)
{
    uint16_t next = (uint16_t)((L.head + 1u) % CL_RING_SIZE);
    if (next == L.tail) { L.st.ring_overflow++; return; }   /* bounded drop */
    L.ring[L.head] = b;
    L.head = next;
}

static bool ring_pop(uint8_t *b)
{
    if (L.tail == L.head) return false;
    *b = L.ring[L.tail];
    L.tail = (uint16_t)((L.tail + 1u) % CL_RING_SIZE);
    return true;
}

void companion_link_init(void)
{
    memset(&L, 0, sizeof(L));
    L.st.ai_state = CL_AI_UNKNOWN;
}

void companion_link_rx(const uint8_t *buf, size_t n)
{
    if (!buf) return;
    for (size_t i = 0; i < n; i++) ring_push(buf[i]);
}

static void handle_frame(const icd02_frame_t *f, uint64_t now_us)
{
    L.st.frames_ok++;
    L.st.last_frame_us = now_us;

    /* sequence tracking: forward gaps counted as loss, late frames as reorder */
    uint8_t seq = f->seq;
    if (L.have_seq) {
        uint8_t expected = (uint8_t)(L.st.last_seq + 1u);
        uint8_t gap = (uint8_t)(seq - expected);
        if (gap != 0u) {
            if (gap < 128u) L.st.seq_gaps += gap;
            else L.st.seq_reorder++;
        }
    }
    L.st.last_seq = seq;
    L.have_seq = true;

    switch (f->type) {
    case CL_TYPE_HEARTBEAT:
        /* payload v1: uptime_s u32 | ai_state u8 | model_id u16 (7 B) */
        if (f->len >= 7u) {
            L.st.last_hb_us = now_us;
            L.st.ai_state = (f->payload[4] > (uint8_t)CL_AI_ERROR) ?
                            CL_AI_UNKNOWN : (companion_ai_state_t)f->payload[4];
            L.st.model_id = (uint16_t)((uint16_t)f->payload[5] |
                                       ((uint16_t)f->payload[6] << 8));
        }
        break;

    case CL_TYPE_OBSTACLE_SET: {
        fc_ai_obstacle_set_t set;
        if (!icd02_decode_obstacle_set(f->payload, f->len, &set)) {
            L.st.oversize++;    /* malformed set: counted, never applied */
            break;
        }
        set.timestamp_us = now_us;      /* FC clock domain (DEC-013) */
        perception_feed_ai(&set);
        break;
    }

    case CL_TYPE_HEALTH:
        /* cpu u8 | mem u8 | temp i16 x0.1C | camera_ok u8 | inference_ms u16
         * = 7 bytes */
        if (f->len >= 7u) {
            L.st.health_valid = true;
            L.st.cpu_pct  = f->payload[0];
            L.st.mem_pct  = f->payload[1];
            L.st.temp_c10 = (int16_t)((uint16_t)f->payload[2] |
                                      ((uint16_t)f->payload[3] << 8));
            L.st.camera_ok = (f->payload[4] != 0u);
            L.st.inference_ms = (uint16_t)((uint16_t)f->payload[5] |
                                           ((uint16_t)f->payload[6] << 8));
        }
        break;

    default:
        L.st.unknown_types++;          /* forward compatibility: ignore */
        break;
    }
}

void companion_link_poll(uint64_t now_us)
{
    L.now_us = now_us;
    uint8_t chunk[ICD02_MAX_FRAME];

    while (ring_pop(&chunk[0])) {
        size_t n = 1;
        while (n < ICD02_MAX_FRAME && ring_pop(&chunk[n])) n++;

        icd02_frame_t fr;
        size_t consumed = 0;
        if (icd02_parse(chunk, n, &fr, &consumed)) {
            handle_frame(&fr, now_us);
            for (size_t i = consumed; i < n; i++) ring_push(chunk[i]);
            continue;
        }
        if (consumed == 0u) {
            /* incomplete frame: keep the bytes for the next poll (bounded
             * ring re-push preserves order; no allocation) */
            for (size_t i = 0; i < n; i++) ring_push(chunk[i]);
            break;
        }
        for (size_t i = consumed; i < n; i++) ring_push(chunk[i]);
        L.st.crc_errors++;            /* any parse rejection counted here */
        break;                        /* resync on the next poll */
    }
}

bool companion_healthy(void)
{
    /* measured against the last poll time: a silent companion produces no
     * frames at all, so "last frame" alone can never expire */
    return L.st.last_hb_us != 0u &&
           L.now_us >= L.st.last_hb_us &&
           (L.now_us - L.st.last_hb_us) <= COMPANION_HEARTBEAT_TIMEOUT_US;
}

const companion_stats_t *companion_stats(void) { return &L.st; }
companion_ai_state_t companion_ai_state(void) { return L.st.ai_state; }

bool companion_link_build_fc_state(uint64_t now_us, const fc_quat_t *att,
                                   const fc_vec3_t *vel_ned_ms, uint8_t mode,
                                   uint8_t failsafe, uint8_t perception_mode,
                                   uint8_t avoidance_mode, float altitude_m,
                                   uint8_t *out, size_t out_cap,
                                   size_t *out_len)
{
    if (!out || !att || !vel_ned_ms) return false;
    /* payload: timestamp_us u64 | att q f32 x4 | vel NED f32 x3 | alt f32 |
     * mode u8 | failsafe u8 | perception u8 | avoidance u8 (44 B) */
    uint8_t p[44];
    memset(p, 0, sizeof(p));
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)((now_us >> (8 * i)) & 0xFFu);
    }
    float q[4]   = { att->w, att->x, att->y, att->z };
    float tail[4] = { vel_ned_ms->x, vel_ned_ms->y, vel_ned_ms->z, altitude_m };
    for (int i = 0; i < 4; i++) {
        uint32_t b;
        __builtin_memcpy(&b, &q[i], 4);
        p[8 + (size_t)i * 4]  = (uint8_t)(b & 0xFFu);
        p[9 + (size_t)i * 4]  = (uint8_t)((b >> 8) & 0xFFu);
        p[10 + (size_t)i * 4] = (uint8_t)((b >> 16) & 0xFFu);
        p[11 + (size_t)i * 4] = (uint8_t)((b >> 24) & 0xFFu);
    }
    for (int i = 0; i < 4; i++) {
        uint32_t b;
        __builtin_memcpy(&b, &tail[i], 4);
        p[24 + (size_t)i * 4]  = (uint8_t)(b & 0xFFu);
        p[25 + (size_t)i * 4]  = (uint8_t)((b >> 8) & 0xFFu);
        p[26 + (size_t)i * 4] = (uint8_t)((b >> 16) & 0xFFu);
        p[27 + (size_t)i * 4] = (uint8_t)((b >> 24) & 0xFFu);
    }
    p[40] = mode;
    p[41] = failsafe;
    p[42] = perception_mode;
    p[43] = avoidance_mode;
    return icd02_encode(CL_TYPE_FC_STATE, L.tx_seq++, p, (uint8_t)sizeof(p),
                        out, out_cap, out_len);
}
