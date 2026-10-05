#ifndef EST_ALT_H
#define EST_ALT_H
#include "fc_types.h"

/* Baro + vertical accel fusion with bias estimation (EST-002). */
void   est_alt_init(void);
void   est_alt_update(const fc_baro_sample_t *baro);
void   est_alt_imu_feed(const fc_imu_sample_t *imu);

/* outputs, downward-positive agl */
float est_alt_get_m(void);
float est_alt_vz_ms(void);

#endif
