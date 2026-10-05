#ifndef ICD02_FRAME_H
#define ICD02_FRAME_H
#include "fc_types.h"

/* ICD-02 companion frame codec (DEC-003/DEC-015), hardware-independent.
 *
 * Frame: SYNC(0xA5 0x5A) | LEN | TYPE | SEQ | PAYLOAD[LEN] | CRC16-CCITT(2)
 * over [LEN, TYPE, SEQ, PAYLOAD]. LEN <= 256.
 *
 * Types: 0x01 HEARTBEAT (comp->FC, 1 Hz, uptime_s u32 | ai_state u8 |
 * model_id u16), 0x02 OBSTACLE_SET (10-50 Hz, see below), 0x03 HEALTH
 * (1 Hz, cpu u8 | mem u8 | temp i16 x0.1C | camera_ok u8 | inference_ms u16),
 * 0x10 FC_STATE (FC->comp, 50 Hz), 0x11 FC_CONFIG_ACK (FC->comp, on change).
 *
 * OBSTACLE_SET payload v1: count u8 | radius_cm[n] | conf_pct[n] | class_id[n] |
 * per detection pos xyz i16 cm + vel xyz i16 cm/s (12 B). Total 1 + 15n.
 *
 * Lives in common/ so BOTH the flight-controller link task AND the SIM
 * virtual companion build/parse frames through one codec (no duplication,
 * no backend->application dependency). */

#define ICD02_SYNC0        0xA5u
#define ICD02_SYNC1        0x5Au
#define ICD02_MAX_PAYLOAD  256u
#define ICD02_HEADER_LEN   5u
#define ICD02_MAX_FRAME    (ICD02_HEADER_LEN + ICD02_MAX_PAYLOAD + 2u)

#define ICD02_TYPE_HEARTBEAT     0x01u
#define ICD02_TYPE_OBSTACLE_SET  0x02u
#define ICD02_TYPE_HEALTH        0x03u
#define ICD02_TYPE_FC_STATE      0x10u
#define ICD02_TYPE_FC_CONFIG_ACK 0x11u

typedef struct {
    uint8_t type;
    uint8_t seq;
    uint8_t len;
    uint8_t payload[ICD02_MAX_PAYLOAD];
} icd02_frame_t;

bool icd02_encode(uint8_t type, uint8_t seq, const uint8_t *payload,
                  uint8_t len, uint8_t *out, size_t out_cap, size_t *out_len);

/* Parse one frame at the head of buf. On success *consumed = frame length.
 * On failure *consumed = safe drop count (>=1) so a stream can resync. */
bool icd02_parse(const uint8_t *buf, size_t len, icd02_frame_t *out,
                 size_t *consumed);

/* cap is size_t on purpose: ICD02_MAX_PAYLOAD is 256, which would truncate
 * to 0 in a uint8_t parameter and silently disable every encode. Returns the
 * payload length (0 on error). */
uint8_t icd02_encode_obstacle_set(uint8_t *payload, size_t cap,
                                  const fc_ai_obstacle_set_t *set);
bool    icd02_decode_obstacle_set(const uint8_t *payload, uint8_t len,
                                  fc_ai_obstacle_set_t *out);

#endif /* ICD02_FRAME_H */
