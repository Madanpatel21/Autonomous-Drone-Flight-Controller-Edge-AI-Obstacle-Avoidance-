#ifndef RC_PROTOCOL_H
#define RC_PROTOCOL_H
#include "fc_types.h"

/* RC link protocols (ICD-04, COM-001). Parsers are pure functions: they turn
 * a raw frame into the normalized fc_rc_frame_t the application consumes
 * (channels 1000..2000, frames_valid, failsafe_active). Timing, CRC and
 * normalization follow the public CRSF and SBUS specifications.
 *
 * CRSF (primary, 420 kbaud 8N1): sync 0xC8 | len | type | payload | crc16.
 *   type 0x08 CHANNELS: 16 × 11-bit packed 3-bytes pairs, u16 us.
 *   type 0xC8 FLIGHT_MODE: 1 byte, bit0 = arm switch.
 *   Normalization: 885..1795 u16 us -> 1000..2000 (documented linear map).
 * SBUS (fallback, 100 kbaud 8N1 inverted): 25 bytes: 0x0F start | 16 × 11-bit
 *   channels | flags (bit2 = failsafe set) | 0x00 end byte. The 25-byte mode has
 *   NO checksum byte (26-byte mode does) — integrity relies on start/end byte
 *   validation plus the SYS-003 500 ms silence timeout. Normalization:
 *   885..1795 u16 us -> 1000..2000 (same map as CRSF).
 *
 * A frame with a bad CRC/sync/length, a short buffer, or a protocol-level
 * failsafe flag yields frames_valid=false and NEVER raises valid: the
 * existing SYS-003 timeout (500 ms of silence) then owns the RC-loss
 * failsafe. */

#define RC_CRSF_SYNC        0xC8u
#define RC_CRSF_TYPE_CHANNELS 0x08u
#define RC_CRSF_TYPE_FLIGHT  0xC8u
#define RC_CRSF_US_MIN      885u
#define RC_CRSF_US_MAX      1795u
#define RC_SBUS_FRAME_LEN   25u
#define RC_SBUS_START       0x0Fu
#define RC_SBUS_END         0x00u
#define RC_FLIGHT_MODE_TIMEOUT_US 200000u  /* arm-switch state freshness */

/* Parse one CRSF frame (caller has already read the 4+len bytes). */
bool rc_crsf_parse(const uint8_t *buf, size_t len, uint64_t now_us,
                   fc_rc_frame_t *out);

/* Parse one 25-byte SBUS frame. */
bool rc_sbus_parse(const uint8_t *buf, size_t len, uint64_t now_us,
                   fc_rc_frame_t *out);

/* Normalize a raw 11-bit/16-bit channel value to 1000..2000. */
uint16_t rc_normalize_us(uint16_t raw_us);

#endif /* RC_PROTOCOL_H */
