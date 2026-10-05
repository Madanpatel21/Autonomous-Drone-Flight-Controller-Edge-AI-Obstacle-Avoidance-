#ifndef MAVLINK2_H
#define MAVLINK2_H

/* Minimal MAVLink v2 framing (ICD-03, COM-002). Implemented from the public
 * protocol definition; no external dependency so the FC stays self-contained.
 *
 * Frame: STX(0xFD) | LEN | INCOMPAT | COMPAT | SEQ | SYSID | COMPID | MSGID |
 *        PAYLOAD[LEN] | CRC16(extra) | CRC16(LEN..PAYLOAD, seed=CRC_EXTRA)
 *
 * Only the four messages the GS actually consumes are registered, each with
 * its official payload length and CRC_EXTRA value (HEARTBEAT 9/50,
 * SYS_STATUS 31/124, ATTITUDE 28/39, LOCAL_POSITION_NED 28/185). Unknown
 * message ids are rejected by the parser registry (documented limitation:
 * the FC does not advertise a full dialect in this phase; Phase 20 adds the
 * message set the ground station needs). */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MAVLINK_STX            0xFDu
#define MAVLINK_MAX_PAYLOAD    255u
#define MAVLINK_HEADER_LEN     8u    /* STX LEN INCOMPAT COMPAT SEQ SYSID COMPID MSGID */
#define MAVLINK_FRAME_MAX      (MAVLINK_HEADER_LEN + MAVLINK_MAX_PAYLOAD + 2u)

#define MAVLINK_MSG_HEARTBEAT            0u
#define MAVLINK_MSG_SYS_STATUS           1u
#define MAVLINK_MSG_ATTITUDE             30u
#define MAVLINK_MSG_LOCAL_POSITION_NED   32u

#define MAVLINK_SYS_ID                   1u
#define MAVLINK_COMP_ID_FC               1u

/* payload length + CRC_EXTRA of a registered message (0/0 = unknown) */
uint8_t  mavlink_msg_len(uint8_t msgid);
uint8_t  mavlink_msg_crc_extra(uint8_t msgid);
bool     mavlink_msg_known(uint8_t msgid);

/* MAVLink CRC-16/MCRF4XX (X.25-style, init 0xFFFF, no final xor). */
uint16_t mavlink_crc_accumulate(uint8_t data, uint16_t crc);
uint16_t mavlink_crc_calculate(const uint8_t *buf, size_t len, uint16_t seed);

/* Build one frame. Returns false if the message is unknown, the length does
 * not match the registered payload length, or the buffer is too small. */
bool mavlink_encode(uint8_t msgid, uint8_t sysid, uint8_t compid, uint8_t seq,
                    const uint8_t *payload, uint8_t len,
                    uint8_t *out, size_t out_cap, size_t *out_len);

typedef struct {
    uint8_t msgid, sysid, compid, seq, len;
    uint8_t payload[MAVLINK_MAX_PAYLOAD];
} mavlink_msg_t;

/* Parse one frame from the front of buf. On success *consumed = frame length.
 * On a CRC/length/unknown-id failure the parser resynchronizes on the next
 * STX byte and returns false with *consumed set to the number of bytes it is
 * safe to drop, so a stream stays recoverable. */
bool mavlink_parse(const uint8_t *buf, size_t len, mavlink_msg_t *out,
                   size_t *consumed);

/* ---- payload helpers (little-endian, field order per protocol) ---- */

static inline void mav_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void mav_put_i16(uint8_t *p, int16_t v)
{
    p[0] = (uint8_t)(uint16_t)v; p[1] = (uint8_t)((uint16_t)v >> 8);
}
static inline uint32_t mav_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline float mav_get_f32(const uint8_t *p) /* IEEE754 LE, memcpy-free */
{
    uint32_t b = mav_get_u32(p);
    float f;
    __builtin_memcpy(&f, &b, sizeof(f));
    return f;
}

#endif /* MAVLINK2_H */
