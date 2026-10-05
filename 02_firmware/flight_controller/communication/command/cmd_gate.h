#ifndef CMD_GATE_H
#define CMD_GATE_H
#include "fc_types.h"
#include <stddef.h>
#include <string.h>

/* FC-side command gate (Phase 24, COM-004/SAF-005).
 *
 * Sender-side validation (the Phase 20 ground station) is not a safety
 * property. This is the receiver-side gate: it is the last place a command can
 * be refused, and the arming class is refused unconditionally there.
 *
 * Frame: MAGIC | VER | TYPE | LEN | payload[LEN] | CRC16-CCITT      (CRC little-endian)
 * A LEN that does not match the received frame length is refused before the
 * payload is read, so a truncated frame cannot make the gate read past it. */

#define CMD_MAGIC        0x43u   /* 'C' */
#define CMD_VERSION      1u
#define CMD_HDR          4u
#define CMD_CRC_LEN      2u
#define CMD_MAX_FRAME    160u   /* fits a full 16-waypoint upload (1 + 16*8) */

typedef enum {
    CMD_MODE_HOLD = 0,
    CMD_MODE_RTL,
    CMD_MODE_LAND,
    CMD_EMERGENCY_STOP,   /* always accepted -> bounded LAND (GS-003) */
    CMD_SET_PARAM,
    CMD_SET_WAYPOINTS,
    CMD_ARM,              /* --- rejected unconditionally below --- */
    CMD_DISARM_OK,
    CMD_FORCE_ARM,
    CMD_CLEAR_ARM_GATE,
    CMD_OVERRIDE_FAILSAFE,
    CMD_TYPE_COUNT
} cmd_type_t;

typedef enum {
    CMD_ACCEPT = 0,
    CMD_REJECT_ARMDENY,     /* COM-004: the arming class has no accept path */
    CMD_REJECT_PAYLOAD,     /* wrong length for this type */
    CMD_REJECT_RANGE,       /* outside the FC-owned envelope, or unknown id */
    CMD_REJECT_CRC,
    CMD_REJECT_LENGTH,      /* LEN disagrees with the frame length */
    CMD_REJECT_MAGIC,
    CMD_REJECT_VERSION,
    CMD_REJECT_UNKNOWN_TYPE,
    CMD_REJECT_SHORT        /* shorter than a header + CRC */
} cmd_decision_t;

/* Parameter ids the FC accepts from the wire, with FC-owned ranges. */
#define CMD_PARAM_BATT_RTL_V   1u
#define CMD_PARAM_BATT_LAND_V  2u
#define CMD_PARAM_GEOFENCE_M   3u
#define CMD_PARAM_MAX_ALT_M    4u

typedef struct {
    uint32_t accepted;
    uint32_t rejected;
    uint32_t rejected_armdeny;      /* the number that must never be zero */
    uint32_t rejected_crc;
    uint32_t rejected_length;
    uint32_t rejected_magic;
    uint32_t rejected_version;
    uint32_t rejected_unknown;
    uint32_t rejected_range;
    uint32_t rejected_short;
    uint32_t param_clamped;         /* values clamped into the envelope */
    uint8_t  last_param_id;
    float    last_param_value;
    uint8_t  last_wp_count;
    cmd_decision_t last;
} cmd_gate_t;

void cmd_gate_init(void);
void cmd_gate_rx(const uint8_t *buf, size_t len);
bool param_envelope(uint8_t param_id, float *lo, float *hi);
const cmd_gate_t *cmd_gate_state(void);

#endif /* CMD_GATE_H */