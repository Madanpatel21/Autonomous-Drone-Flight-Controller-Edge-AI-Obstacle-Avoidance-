#ifndef CTRL_CASCADE_H
#define CTRL_CASCADE_H
#include "fc_types.h"

/* Cascade controller: rate (1 kHz) + attitude (250 Hz) (CTRL-001/002).
 * PID with derivative-on-measurement, anti-windup, output saturation. */

void ctrl_cascade_init(void);
void ctrl_cascade_run(const fc_imu_sample_t *imu);  /* rate loop, every tick */
void ctrl_attitude_loop(void);                      /* outer loop, 250 Hz */

/* setpoints for testing/mission layer */
void ctrl_set_rate_sp(const fc_vec3_t *rate_sp);
void ctrl_set_attitude_sp(const fc_quat_t *q_sp);
void ctrl_set_armed(bool armed);            /* arm gate: disarmed -> motors 0 (CTRL-005) */
void ctrl_set_hover_throttle(float t);      /* collective at hover, 0..1 */

/* Altitude hold (Phase 14, CTRL-002): alt setpoint [m] -> vertical velocity
 * PID -> collective offset around hover. Run at 100 Hz. */
void ctrl_set_altitude_sp(float altitude_m);   /* enables altitude hold */
void ctrl_altitude_hold(bool enable);
void ctrl_altitude_loop(void);
const float* ctrl_get_motor_outputs(void); /* [4], 0..1 */

#endif
