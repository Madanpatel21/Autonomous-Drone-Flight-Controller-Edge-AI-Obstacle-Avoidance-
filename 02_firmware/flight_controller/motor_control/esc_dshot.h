#ifndef ESC_DSHOT_H
#define ESC_DSHOT_H
#include <stdint.h>
#include <stdbool.h>

/* DShot frame encoding (HW-006, Phase 13).
 * Frame: [11-bit throttle][1-bit telemetry request][4-bit CRC] = 16 bits.
 * Throttle semantics: 0 = disarmed/stop; 1..47 reserved commands; 48..2047 thrust.
 * Verified source: betaflight.com/docs/development/API/Dshot (frame structure,
 * DShot600 timing 1.67 us/bit, 26.72 us/frame) + BLHeli/Bluejay CRC convention. */

#define DSHOT_CMD_MOTOR_STOP   0u
#define DSHOT_MIN_THROTTLE     48u
#define DSHOT_MAX_THROTTLE     2047u
#define DSHOT_TELEMETRY_BIT    1u

/* CRC-4 per BLHeli/Bluejay: (v ^ (v>>4) ^ (v>>8)) & 0xF over the 12-bit payload */
uint16_t dshot_crc4(uint16_t payload12);

/* Build the full 16-bit frame for an 11-bit throttle value. */
uint16_t dshot_build_frame(uint16_t throttle, bool telemetry_request);

/* Map a normalized motor command 0..1 to DShot throttle.
 * 0 -> DSHOT_CMD_MOTOR_STOP (0); (0..1] -> 48..2047 linearly. */
uint16_t dshot_throttle_from_unit(float unit);

/* Decoder side of the same frame (an ESC or an ESC-signal analyser). Returns
 * false when the CRC-4 does not match, which is exactly the check a real ESC
 * performs before accepting a command. Used by the HIL rig (Phase 22) to
 * recover thrust from the frames that cross the link, and by the capture
 * analyser. */
bool dshot_decode_frame(uint16_t frame, uint16_t *throttle, bool *telemetry_request);

/* Inverse mapping (throttle -> normalized thrust) for the rig/analyser.
 * 0..DSHOT_MIN_THROTTLE -> 0.0 (stop / reserved commands). */
float dshot_unit_from_throttle(uint16_t throttle);

#endif
