#ifndef MOTOR_OUTPUT_H
#define MOTOR_OUTPUT_H
#include "fc_types.h"

/* Motor output stage (Phase 13, HW-006, SAF-003/004/005):
 *   mixer outputs [0..1] -> arm interlock -> timeout check -> DShot frames.
 * Interlocks: disarmed => stop frames; command timeout => stop frames. */

void     motor_output_init(void);
void     motor_output_set_armed(bool armed);
bool     motor_output_is_armed(void);

/* called every rate-loop tick with the mixer outputs */
fc_status_t motor_output_update(const float motor[4], uint64_t now_us);

/* Gate the actuator path: applies arm/stale rules in place on motor[4].
 * Returns true if nonzero output was permitted; motor[] is zeroed otherwise.
 * This is what the control loop calls before the HAL actuator write. */
bool motor_output_apply(float motor[4], uint64_t now_us);

/* latest 16-bit DShot frames (telemetry request off by default) */
void     motor_output_frames(uint16_t frames[4]);

/* call from a known-rate task (e.g. failsafe monitor) for stale-command stop */
void     motor_output_poll(uint64_t now_us);

/* true if a command timeout forced stop frames (SAF-003 monitor) */
bool     motor_output_timed_out(void);

#endif
