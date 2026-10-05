#ifndef CALIBRATION_H
#define CALIBRATION_H
#include "fc_types.h"

/* Sensor calibration (SEN-003): gyro bias at rest, accel 6-face scale/offset.
 * Stored in flash with CRC; applied transparently in the data path. */

typedef struct {
    float bias[3];       /* rad/s */
    bool  valid;
} cal_gyro_t;

typedef struct {
    float offset[3];     /* m/s^2 */
    float scale[3];      /* nominal 1.0 */
    bool  valid;
} cal_accel_t;

void        cal_init(void);                        /* load from flash */
const cal_gyro_t*  cal_gyro(void);
const cal_accel_t* cal_accel(void);

/* compute gyro bias from N静止 samples collected at rest */
bool cal_gyro_compute(const fc_imu_sample_t *samples, uint32_t n);
/* apply bias-correction to a raw sample (in place) */
void cal_gyro_apply(fc_imu_sample_t *s);

/* 6-face accel cal: pass min/max per axis (from known-orientation rest data) */
bool cal_accel_compute(const float minmax[3][2]);
void cal_accel_apply(fc_imu_sample_t *s);

fc_status_t cal_store(void);                        /* persist with CRC */

#endif
