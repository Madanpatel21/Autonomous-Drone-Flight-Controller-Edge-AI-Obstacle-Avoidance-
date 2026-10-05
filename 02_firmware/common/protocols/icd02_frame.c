#include "icd02_frame.h"
#include "crc16.h"
#include <string.h>

/* ICD-02 codec implementation (Phase 19, DEC-015). Pure, bounded, no state. */

bool icd02_encode(uint8_t type, uint8_t seq, const uint8_t *payload,
                  uint8_t len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (!out || !out_len) return false;
    if (len > ICD02_MAX_PAYLOAD) return false;
    if (out_cap < (size_t)len + ICD02_HEADER_LEN + 2u) return false;

    out[0] = ICD02_SYNC0;
    out[1] = ICD02_SYNC1;
    out[2] = len;
    out[3] = type;
    out[4] = seq;
    if (len && payload) memcpy(&out[5], payload, len);
    uint16_t crc = crc16_ccitt(&out[2], (size_t)len + 3u);
    out[5 + len]     = (uint8_t)(crc & 0xFFu);
    out[6 + len]     = (uint8_t)(crc >> 8);
    *out_len = (size_t)len + ICD02_HEADER_LEN + 2u;
    return true;
}

bool icd02_parse(const uint8_t *buf, size_t len, icd02_frame_t *out,
                 size_t *consumed)
{
    if (!buf || !consumed) return false;
    *consumed = 0;
    if (len < 3u) return false;

    if (buf[0] != ICD02_SYNC0 || buf[1] != ICD02_SYNC1) {
        for (size_t i = 1; i < len; i++) {
            if (buf[i] == ICD02_SYNC0) { *consumed = i; return false; }
        }
        *consumed = 1u;
        return false;
    }

    uint8_t plen = buf[2];
    if (plen > ICD02_MAX_PAYLOAD) { *consumed = 1u; return false; }
    size_t frame = (size_t)plen + ICD02_HEADER_LEN + 2u;
    if (len < frame) return false;

    uint16_t crc = crc16_ccitt(&buf[2], (size_t)plen + 3u);
    if ((uint8_t)(crc & 0xFFu) != buf[5 + plen] ||
        (uint8_t)(crc >> 8)   != buf[6 + plen]) {
        *consumed = 1u;
        return false;
    }

    if (out) {
        out->type = buf[3];
        out->seq  = buf[4];
        out->len  = plen;
        if (plen) memcpy(out->payload, &buf[5], plen);
    }
    *consumed = frame;
    return true;
}

/* ---- OBSTACLE_SET payload v1: count | radius[n] | conf[n] | class[n] |
 *      per detection pos i16 cm x3 + vel i16 cm/s x3 (12 B) ---- */
#define ICD02_DET_BYTES 12u

static void put_i16_cm(uint8_t *p, float m)
{
    float v = m * 100.0f;
    if (v >  32767.0f) v =  32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    int16_t iv = (int16_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
    p[0] = (uint8_t)((uint16_t)iv & 0xFFu);
    p[1] = (uint8_t)(((uint16_t)iv >> 8) & 0xFFu);
}

uint8_t icd02_encode_obstacle_set(uint8_t *payload, size_t cap,
                                  const fc_ai_obstacle_set_t *set)
{
    if (!payload || !set || set->count > FC_AI_MAX_DETECTIONS) return 0;
    uint8_t n = set->count;
    size_t need = 1u + 3u * (size_t)n + ICD02_DET_BYTES * (size_t)n;
    if (cap < need) return 0;

    payload[0] = n;
    for (uint8_t i = 0; i < n; i++) {
        float rcm = set->det[i].radius_m * 100.0f;
        if (rcm < 0.0f) rcm = 0.0f;
        if (rcm > 255.0f) rcm = 255.0f;
        float cf = set->det[i].confidence * 100.0f;
        if (cf < 0.0f) cf = 0.0f;
        if (cf > 255.0f) cf = 255.0f;
        payload[1 + i]                          = (uint8_t)rcm;
        payload[1 + (size_t)n + i]              = (uint8_t)cf;
        payload[1 + 2 * (size_t)n + i]          = set->det[i].class_id;
    }
    uint8_t *p = &payload[1 + 3 * (size_t)n];
    for (uint8_t i = 0; i < n; i++) {
        const fc_ai_detection_t *d = &set->det[i];
        put_i16_cm(&p[0],  d->pos_m.x);  put_i16_cm(&p[2],  d->pos_m.y);
        put_i16_cm(&p[4],  d->pos_m.z);  put_i16_cm(&p[6],  d->vel_m_s.x);
        put_i16_cm(&p[8],  d->vel_m_s.y); put_i16_cm(&p[10], d->vel_m_s.z);
        p += ICD02_DET_BYTES;
    }
    return (uint8_t)need;
}

bool icd02_decode_obstacle_set(const uint8_t *payload, uint8_t len,
                               fc_ai_obstacle_set_t *out)
{
    if (!payload || !out || len < 1u) return false;
    uint8_t n = payload[0];
    if (n > FC_AI_MAX_DETECTIONS) return false;
    size_t need = 1u + 3u * (size_t)n + ICD02_DET_BYTES * (size_t)n;
    if ((size_t)len < need) return false;

    memset(out, 0, sizeof(*out));
    out->count = n;
    for (uint8_t i = 0; i < n; i++) {
        out->det[i].radius_m   = payload[1 + i] * 0.01f;
        out->det[i].confidence = payload[1 + (size_t)n + i] * 0.01f;
        out->det[i].class_id   = payload[1 + 2 * (size_t)n + i];
    }
    const uint8_t *p = &payload[1 + 3 * (size_t)n];
    for (uint8_t i = 0; i < n; i++) {
        int16_t v[6];
        for (int k = 0; k < 6; k++) {
            v[k] = (int16_t)((uint16_t)p[k * 2] |
                             ((uint16_t)p[k * 2 + 1] << 8));
        }
        out->det[i].pos_m.x   = v[0] * 0.01f;
        out->det[i].pos_m.y   = v[1] * 0.01f;
        out->det[i].pos_m.z   = v[2] * 0.01f;
        out->det[i].vel_m_s.x = v[3] * 0.01f;
        out->det[i].vel_m_s.y = v[4] * 0.01f;
        out->det[i].vel_m_s.z = v[5] * 0.01f;
        p += ICD02_DET_BYTES;
    }
    return true;
}
