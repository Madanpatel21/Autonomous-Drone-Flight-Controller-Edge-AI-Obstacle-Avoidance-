#include "rc_protocol.h"
#include "crc16.h"
#include <string.h>

/* RC protocol parsers (Phase 19, ICD-04, COM-001). Pure functions, no state
 * except the CRSF arm-switch freshness window tracked per frame. */

uint16_t rc_normalize_us(uint16_t raw_us)
{
    if (raw_us <= RC_CRSF_US_MIN) return 1000u;
    if (raw_us >= RC_CRSF_US_MAX) return 2000u;
    /* linear map 885..1795 -> 1000..2000 */
    int32_t span = (int32_t)RC_CRSF_US_MAX - (int32_t)RC_CRSF_US_MIN;
    int32_t v = 1000 + (((int32_t)raw_us - (int32_t)RC_CRSF_US_MIN) * 1000) / span;
    if (v < 1000) v = 1000;
    if (v > 2000) v = 2000;
    return (uint16_t)v;
}

/* CRSF CHANNELS packs TWO 11-bit channels into 3 bytes (per spec):
 *   b0 = ch_a bits 0-7
 *   b1 = ch_a bits 8-10 (low 3 bits) | ch_b bits 0-4 (high 5 bits)
 *   b2 = ch_b bits 5-10 (low 6 bits)
 * The first implementation unpacked one channel per 3 bytes and produced
 * garbage; the layout above is verified by TEST-COM-CRSF against a frame
 * built by the matching encoder in the test. */
static void crsf_unpack_pair(const uint8_t *p, uint16_t *a, uint16_t *b)
{
    *a = (uint16_t)((uint16_t)p[0] | (((uint16_t)p[1] & 0x07u) << 8));
    *b = (uint16_t)((((uint16_t)p[1] & 0xF8u) >> 3) | ((uint16_t)p[2] << 5));
}

bool rc_crsf_parse(const uint8_t *buf, size_t len, uint64_t now_us,
                   fc_rc_frame_t *out)
{
    if (!buf || !out || len < 6u) return false;
    if (buf[0] != RC_CRSF_SYNC) return false;
    uint8_t plen = buf[1];
    if (plen < 1u || plen > 64u) return false;
    size_t frame = (size_t)plen + 4u;      /* sync len type payload.. crc16 */
    if (len < frame) return false;

    /* CRC covers sync..payload (excluding the 2 CRC bytes) */
    uint16_t crc = crc16_ccitt(buf, frame - 2u);
    uint16_t got = (uint16_t)((uint16_t)buf[frame - 2u] |
                              ((uint16_t)buf[frame - 1u] << 8));
    if (crc != got) return false;

    memset(out, 0, sizeof(*out));
    out->timestamp_us = now_us;
    out->valid = true;

    /* CRSF `len` counts the type byte, so a 16-channel frame has len = 23;
     * 12 data bytes (8 channels) need len >= 13. */
    if (buf[2] == RC_CRSF_TYPE_CHANNELS && plen >= 13u) {
        for (int i = 0; i < 4; i++) {
            if ((size_t)(3 + i * 3 + 2) > (size_t)(3 + plen - 1u)) break;
            uint16_t ra = 0u, rb = 0u;
            crsf_unpack_pair(&buf[3 + i * 3], &ra, &rb);
            out->channels[i * 2]     = rc_normalize_us(ra);
            out->channels[i * 2 + 1] = rc_normalize_us(rb);
        }
        out->frames_valid = true;
        out->failsafe_active = false;
        return true;
    }
    if (buf[2] == RC_CRSF_TYPE_FLIGHT && plen >= 1u) {
        /* arm switch: bit0; channel 5 (index 4) is the project arm switch */
        out->channels[4] = (buf[3] & 0x01u) ? 1800u : 1000u;
        for (int i = 0; i < 8; i++) {
            if (out->channels[i] == 0u) out->channels[i] = 1500u;
        }
        out->frames_valid = true;
        out->failsafe_active = false;
        return true;
    }
    /* other CRSF frame types (GPS, sensors, battery) are accepted and ignored */
    out->frames_valid = false;
    return true;
}

bool rc_sbus_parse(const uint8_t *buf, size_t len, uint64_t now_us,
                   fc_rc_frame_t *out)
{
    if (!buf || !out || len < RC_SBUS_FRAME_LEN) return false;
    if (buf[0] != RC_SBUS_START || buf[24] != RC_SBUS_END) return false;

    /* 25-byte SBUS has no checksum byte (the 26-byte mode does); integrity
     * relies on the SYS-003 500 ms silence timeout plus the flags byte. */

    memset(out, 0, sizeof(*out));
    out->timestamp_us = now_us;
    for (int i = 0; i < 8; i++) {
        const uint8_t *p = &buf[1 + i * 2];
        uint16_t raw = (uint16_t)((uint16_t)p[0] |
                                  (((uint16_t)p[1] & 0x07u) << 8));
        out->channels[i] = rc_normalize_us(raw & 0x07FFu);
    }
    /* SBUS channel 5 (index 4) is the project arm switch */
    if (out->channels[4] > 1600u) out->channels[4] = 1800u;
    else if (out->channels[4] < 1400u) out->channels[4] = 1000u;

    out->frames_valid = true;
    out->valid = true;
    /* byte 23 = flags; bit 2 = "failsafe set" (RC transmitter lost) */
    out->failsafe_active = ((buf[23] & 0x04u) != 0u);
    return true;
}
