#ifndef EST_POSITION_H
#define EST_POSITION_H
#include "fc_types.h"

/* Horizontal position/velocity estimator (EST-003, Phase 15 subset).
 * Inputs: GNSS position/velocity (10 Hz), vertical velocity via est_alt.
 * Model: velocity low-pass + position integration with GNSS correction and
 * innovation gating (EST-004). Optical-flow/baro-aided modes extend this in
 * later phases; design slot documented in STATE_ESTIMATION_VALIDATION.md. */

typedef struct { float x_m; float y_m; } est_xy_t;

void       est_position_init(void);
void       est_position_gnss(const fc_gnss_sample_t *g);   /* 10 Hz update */
void       est_position_predict(float dt_s);               /* from velocity */
est_xy_t   est_position_get(void);
fc_vec3_t  est_position_velocity(void);                    /* NED-ish (x=N, y=E) */bool       est_position_healthy(void);                    /* false after GNSS loss */
float      est_position_last_gnss_age_s(void);
void       est_position_tick(void);                       /* age/health check, 50 Hz */

#endif
