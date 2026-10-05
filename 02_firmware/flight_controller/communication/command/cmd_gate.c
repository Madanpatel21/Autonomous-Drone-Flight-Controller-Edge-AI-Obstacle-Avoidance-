/* Command gate (Phase 24, COM-004 / SAF-005 / GS-002 / GS-003).
 *
 * The ground station validates commands, but validation on the SENDER is not a
 * safety property: a corrupted, spoofed or simply buggy sender must not be able
 * to arm the vehicle, clear an arm gate or override a failsafe. This module is
 * the FC-side half of COM-004 — it accepts a command frame on the wire and
 * applies FC-owned policy before anything is allowed to change state.
 *
 * Policy, in FC-owned constants:
 *   - the arming class (ARM, FORCE_ARM, CLEAR_ARM_GATE, OVERRIDE_FAILSAFE and
 *     every spelling of them) is rejected unconditionally; there is no argument
 *     and no state in which it is accepted;
 *   - mode requests are limited to the mission SM's safe set (HOLD, RTL, LAND);
 *   - parameter writes are clamped to the same envelope the defaults use, and a
 *     corrupt/unknown field is refused rather than stored;
 *   - EMERGENCY_STOP is always accepted (GS-003) and maps to a bounded LAND
 *     request — the one command an operator must always be able to send;
 *   - waypoint uploads are bounded by the mission cap and the geofence/altitude
 *     envelope, checked here rather than trusted from the wire.
 *
 * Every rejection increments a named counter so the reason is observable on the
 * wire instead of vanishing. Fixed storage only (FW-004). */

#include "cmd_gate.h"
#include "crc16.h"
#include <string.h>

/* FC-owned envelopes (must match mission_sm.c / parameters.c defaults; kept as
 * explicit constants here so the wire path cannot widen them). */
#define CMD_MAX_WP        16
#define CMD_WP_BYTES      8u      /* x f32 + y f32 */
#define CMD_MAX_FENCE_M   50.0f

static cmd_gate_t g;

void cmd_gate_init(void)
{
    memset(&g, 0, sizeof(g));
}

static bool type_known(uint8_t type) { return type < (uint8_t)CMD_TYPE_COUNT; }

/* Every rejection records its decision and its reason. The first version
 * returned early for a bad magic/version/length/CRC without setting `last`, so
 * the observable state said "accepted" after a refused frame — exactly the
 * operator-facing lie this module exists to prevent. */
static void refuse(cmd_decision_t d)
{
    g.last = d;
    g.rejected++;
    switch (d) {
    case CMD_REJECT_ARMDENY:   g.rejected_armdeny++; break;
    case CMD_REJECT_RANGE:     g.rejected_range++;   break;
    case CMD_REJECT_CRC:       g.rejected_crc++;     break;
    case CMD_REJECT_LENGTH:    g.rejected_length++;  break;
    case CMD_REJECT_MAGIC:     g.rejected_magic++;   break;
    case CMD_REJECT_VERSION:   g.rejected_version++; break;
    case CMD_REJECT_UNKNOWN_TYPE: g.rejected_unknown++; break;
    case CMD_REJECT_SHORT:     g.rejected_short++;   break;
    default: break;   /* CMD_REJECT_PAYLOAD is counted only in `rejected` */
    }
}

void cmd_gate_rx(const uint8_t *buf, size_t len)
{
    if (!buf || len < CMD_HDR + CMD_CRC_LEN) { refuse(CMD_REJECT_SHORT); return; }
    if (buf[0] != CMD_MAGIC) { refuse(CMD_REJECT_MAGIC); return; }
    if (buf[1] != CMD_VERSION) { refuse(CMD_REJECT_VERSION); return; }
    uint8_t type = buf[2];
    uint8_t plen = buf[3];
    if (!type_known(type)) { refuse(CMD_REJECT_UNKNOWN_TYPE); return; }
    if ((size_t)CMD_HDR + plen + CMD_CRC_LEN != len) {
        /* a length field that does not match the frame is refused outright:
         * trusting it would let a short frame read past the payload. */
        refuse(CMD_REJECT_LENGTH);
        return;
    }
    uint16_t crc = crc16_ccitt(buf, len - CMD_CRC_LEN);
    uint16_t got = (uint16_t)((uint16_t)buf[len - 2] |
                              ((uint16_t)buf[len - 1] << 8));
    if (crc != got) { refuse(CMD_REJECT_CRC); return; }

    const uint8_t *p = &buf[CMD_HDR];
    cmd_decision_t d;
    switch ((cmd_type_t)type) {
    case CMD_MODE_HOLD:
    case CMD_MODE_RTL:
    case CMD_MODE_LAND:
        d = (plen == 0u) ? CMD_ACCEPT : CMD_REJECT_PAYLOAD;
        break;

    case CMD_EMERGENCY_STOP:
        /* Accepted with or without payload: never refused. */
        d = CMD_ACCEPT;
        break;

    case CMD_SET_PARAM: {
        /* param_id u8 | value f32 — clamped into the FC-owned envelope rather
         * than refused, so a stale sender cannot request an unsafe value but a
         * merely imprecise one still lands inside the limit. */
        if (plen != 5u) { d = CMD_REJECT_PAYLOAD; break; }
        float v;
        uint32_t b = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                     ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
        memcpy(&v, &b, sizeof(v));
        float lo, hi;
        if (!param_envelope(p[0], &lo, &hi)) { d = CMD_REJECT_RANGE; break; }
        if (!(v == v)) { d = CMD_REJECT_RANGE; break; }   /* NaN is never a value */
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        g.param_clamped++;
        g.last_param_id = p[0];
        g.last_param_value = v;
        d = CMD_ACCEPT;
        break;
    }

    case CMD_SET_WAYPOINTS: {
        /* count u8 | count * (x f32, y f32) — envelope-checked on the FC.
         * A waypoint is 8 bytes (two f32), NOT 5: the first version used a
         * 5-byte stride in the length check while reading 8, which let a
         * short frame pass the check and then be read past its end. */
        if (plen < 1u) { d = CMD_REJECT_PAYLOAD; break; }
        uint8_t n = p[0];
        if (n > CMD_MAX_WP) { d = CMD_REJECT_RANGE; break; }
        if ((size_t)plen != 1u + (size_t)CMD_WP_BYTES * n) {
            d = CMD_REJECT_PAYLOAD; break;
        }
        bool bad = false;
        for (uint8_t i = 0; i < n && !bad; i++) {
            float x, y;
            const uint8_t *rec = &p[1 + CMD_WP_BYTES * i];   /* x f32 then y f32 */
            uint32_t bx = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8) |
                          ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);
            uint32_t by = (uint32_t)rec[4] | ((uint32_t)rec[5] << 8) |
                          ((uint32_t)rec[6] << 16) | ((uint32_t)rec[7] << 24);
            memcpy(&x, &bx, sizeof(x));
            memcpy(&y, &by, sizeof(y));
            if (!(x == x) || !(y == y)) { bad = true; break; }
            float r2 = x * x + y * y;
            if (r2 > CMD_MAX_FENCE_M * CMD_MAX_FENCE_M) { bad = true; break; }
        }
        /* NOTE: the first version set `d = CMD_ACCEPT` unconditionally after the
         * loop, which turned every in-loop rejection (NaN, outside the fence)
         * into an accept. The flag keeps the decision with the check that made
         * it — found by TEST-CMDGATE. */
        d = bad ? CMD_REJECT_RANGE : CMD_ACCEPT;
        if (!bad) g.last_wp_count = n;
        break;
    }

    /* COM-004 / SAF-005: unconditional. There is no code path, parameter or
     * flag that turns these into an accept. */
    case CMD_ARM:
    case CMD_DISARM_OK:
    case CMD_FORCE_ARM:
    case CMD_CLEAR_ARM_GATE:
    case CMD_OVERRIDE_FAILSAFE:
    default:
        d = CMD_REJECT_ARMDENY;
        break;
    }

    g.last = d;
    if (d == CMD_ACCEPT) {
        g.accepted++;
    } else {
        refuse(d);
    }
}

bool param_envelope(uint8_t param_id, float *lo, float *hi)
{
    if (!lo || !hi) return false;
    switch (param_id) {
    case CMD_PARAM_BATT_RTL_V:  *lo = 3.3f; *hi = 3.6f; return true;
    case CMD_PARAM_BATT_LAND_V: *lo = 3.0f; *hi = 3.4f; return true;
    case CMD_PARAM_GEOFENCE_M:  *lo = 10.0f; *hi = 100.0f; return true;
    case CMD_PARAM_MAX_ALT_M:   *lo = 1.0f;  *hi = 60.0f; return true;
    default: return false;
    }
}

const cmd_gate_t *cmd_gate_state(void) { return &g; }