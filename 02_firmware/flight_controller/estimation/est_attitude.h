#ifndef EST_ATTITUDE_H
#define EST_ATTITUDE_H
#include "fc_types.h"

/* Quaternion attitude estimator (gyro propagation + accel correction),
 * gyro bias estimation, chi-square innovation gating (EST-001/004). */

void est_attitude_init(void);
void est_attitude_update(const fc_imu_sample_t *imu);

/* outputs (NED->body orientation; yaw unwrapped) */
fc_quat_t   est_attitude_quat(void);
fc_vec3_t   est_attitude_rates_rad_s(void);  /* filtered body rates */
fc_vec3_t   est_attitude_bias(void);         /* estimated gyro bias */
float       est_attitude_convergence(void);  /* 0..1 health metric */

#endif
