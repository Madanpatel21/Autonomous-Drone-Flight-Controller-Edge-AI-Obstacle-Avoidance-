#include "mavlink2.h"
#include <string.h>

/* Registered message set (Phase 19 subset of ICD-03). */
typedef struct { uint8_t id; uint8_t len; uint8_t crc_extra; } mav_msg_def_t;

static const mav_msg_def_t k_msgs[] = {
    { MAVLINK_MSG_HEARTBEAT,          9u,  50u },
    { MAVLINK_MSG_SYS_STATUS,       31u, 124u },
    { MAVLINK_MSG_ATTITUDE,         28u,  39u },
    { MAVLINK_MSG_LOCAL_POSITION_NED, 28u, 185u },
};

static const mav_msg_def_t *def_of(uint8_t msgid)
{
    for (size_t i = 0; i < sizeof(k_msgs) / sizeof(k_msgs[0]); i++) {
        if (k_msgs[i].id == msgid) return &k_msgs[i];
    }
    return NULL;
}

uint8_t mavlink_msg_len(uint8_t msgid)
{
    const mav_msg_def_t *d = def_of(msgid);
    return d ? d->len : 0u;
}

uint8_t mavlink_msg_crc_extra(uint8_t msgid)
{
    const mav_msg_def_t *d = def_of(msgid);
    return d ? d->crc_extra : 0u;
}

bool mavlink_msg_known(uint8_t msgid) { return def_of(msgid) != NULL; }

/* CRC-16/MCRF4XX: reflected 0x1021 polynomial, init 0xFFFF, no final xor.
 * This is the algorithm in the MAVLink protocol definition (not the CCITT
 * used elsewhere in this project for parameter blobs and ICD-02 frames). */
static const uint16_t k_crc_tab[16] = {
    0x0000, 0x1081, 0x2102, 0x3183, 0x4204, 0x5285, 0x6306, 0x7387,
    0x8408, 0x9489, 0xa50a, 0xb58b, 0xc60c, 0xd68d, 0xe70e, 0xf78f
};

uint16_t mavlink_crc_accumulate(uint8_t data, uint16_t crc)
{
    uint8_t tmp = data ^ (uint8_t)(crc & 0xFFu);
    tmp ^= (uint8_t)(tmp << 4);
    return (uint16_t)((crc >> 8) ^ (uint16_t)(tmp << 8) ^ k_crc_tab[tmp >> 4]);
}

uint16_t mavlink_crc_calculate(const uint8_t *buf, size_t len, uint16_t seed)
{
    uint16_t crc = seed;
    for (size_t i = 0; i < len; i++) crc = mavlink_crc_accumulate(buf[i], crc);
    return crc;
}

bool mavlink_encode(uint8_t msgid, uint8_t sysid, uint8_t compid, uint8_t seq,
                    const uint8_t *payload, uint8_t len,
                    uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (!out || !out_len) return false;
    const mav_msg_def_t *d = def_of(msgid);
    if (!d || len != d->len) return false;
    if (out_cap < (size_t)len + MAVLINK_HEADER_LEN + 2u) return false;

    out[0] = MAVLINK_STX;
    out[1] = len;
    out[2] = 0u;   /* incompat_flags: v2-only messages set 0 here */
    out[3] = 0u;   /* compat_flags */
    out[4] = seq;
    out[5] = sysid;
    out[6] = compid;
    out[7] = msgid;
    if (len && payload) memcpy(&out[8], payload, len);

    /* CRC covers LEN..PAYLOAD, seeded with the message CRC_EXTRA. */
    uint16_t crc = mavlink_crc_calculate(&out[1], (size_t)len + 7u, d->crc_extra);
    out[8 + len]     = (uint8_t)(crc & 0xFFu);
    out[8 + len + 1] = (uint8_t)(crc >> 8);
    *out_len = (size_t)len + MAVLINK_HEADER_LEN + 2u;
    return true;
}

bool mavlink_parse(const uint8_t *buf, size_t len, mavlink_msg_t *out,
                   size_t *consumed)
{
    if (!buf || !consumed) return false;
    *consumed = 0;
    if (len < MAVLINK_HEADER_LEN + 2u) return false;
    if (buf[0] != MAVLINK_STX) { *consumed = 1u; return false; }

    uint8_t plen = buf[1];
    size_t  frame = (size_t)plen + MAVLINK_HEADER_LEN + 2u;
    if (len < frame) return false;            /* incomplete: wait for more */

    const mav_msg_def_t *d = def_of(buf[7]);
    if (!d || plen != d->len) { *consumed = 1u; return false; }

    uint16_t crc = mavlink_crc_calculate(&buf[1], (size_t)plen + 7u, d->crc_extra);
    if ((uint8_t)(crc & 0xFFu) != buf[8 + plen] ||
        (uint8_t)(crc >> 8)   != buf[9 + plen]) {
        *consumed = 1u;                       /* resync on next STX */
        return false;
    }

    if (out) {
        out->msgid  = buf[7];
        out->sysid  = buf[5];
        out->compid = buf[6];
        out->seq    = buf[4];
        out->len    = plen;
        if (plen) memcpy(out->payload, &buf[8], plen);
    }
    *consumed = frame;
    return true;
}
