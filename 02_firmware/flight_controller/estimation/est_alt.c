#include "est_alt.h"
#include <math.h>
#include <string.h>

/* Altitude/vertical-velocity complementary estimator (EST-002).
 *
 * predict @1 kHz : az = accel.z − g (near-level assumption, documented);
 *                  vz += az·dt ; alt += vz·dt
 * correct @50 Hz : err = baro_alt − alt
 *                  alt += K_ALT·err        (≈25 s⁻¹ strong position anchor)
 *                  vz  += K_VZ ·err        (≈2.5 s⁻¹ weak velocity anchor)
 *
 * HISTORY (Phase 15): an earlier version used gains (0.35, 0.25 per baro
 * sample) plus a baro-bias state. The bias state absorbed the position error,
 * leaving vz unanchored: long-duration sim developed a slow positive-feedback
 * climb (1.5 m → 30 m over ~100 s). The row above is the corrected, verified
 * structure; the bias state is intentionally removed (no drift source left).
 * Sensor noise rejection: K_VZ small enough that 5 Pa baro noise (≈0.4 m)
 * contributes ~0.02 m/s velocity noise, which the control loop tolerates. */

#define G (9.80665f)
#define P0 (101325.0f)
#define K_ALT (0.50f)     /* per 50 Hz sample -> 25/s  */
#define K_VZ  (0.02f)     /* per 50 Hz sample -> 1.0/s */
#define VZ_DEADBAND (0.30f)  /* m: baro noise (~0.4 m RMS) must not random-walk vz */
#define GROUND_WINDOW_SAMPLES (50)   /* 1 s of baro samples at 50 Hz */

typedef struct {
    float alt_m;
    float vz_ms;          /* up-positive */
    float ground_pa;      /* baro reference: mean of the first ground window */
    bool  have_baro;
    int   ground_samples; /* samples folded into the ground reference so far */
    float ground_sum_pa;
    uint64_t last_imu_us;
} alt_t;

static alt_t a;

static float pressure_to_alt(float p)
{
    return 44330.0f * (1.0f - powf(p / P0, 0.19029496f));
}

void est_alt_init(void)
{
    memset(&a, 0, sizeof(a));
}

void est_alt_imu_feed(const fc_imu_sample_t *imu)
{
    if (!imu || !imu->valid) return;

    float dt = 0.001f;
    if (a.last_imu_us != 0 && imu->timestamp_us > a.last_imu_us) {
        dt = (float)(imu->timestamp_us - a.last_imu_us) * 1e-6f;
    }
    a.last_imu_us = imu->timestamp_us;
    if (dt <= 0.0f || dt > 0.05f) dt = 0.001f;

    float az = imu->accel_m_s2.z - G;

    a.vz_ms += az * dt;
    a.alt_m += a.vz_ms * dt;

    /* sanity bounds: this is a control-grade estimator, not a barometric altimeter */
    if (a.vz_ms > 5.0f) a.vz_ms = 5.0f;
    if (a.vz_ms < -5.0f) a.vz_ms = -5.0f;

    if (!a.have_baro) {
        /* no barometric reference yet: hold at ground */
        if (a.alt_m > 0.0f) a.alt_m = 0.0f;
        if (a.vz_ms > 0.0f) a.vz_ms = 0.0f;
    }
}

void est_alt_update(const fc_baro_sample_t *b)
{
    if (!b || !b->valid) return;

    if (!a.have_baro) {
        a.ground_pa = b->pressure_pa;   /* ground reference at first sample */
        a.ground_sum_pa = b->pressure_pa;
        a.ground_samples = 1;
        a.alt_m = 0.0f;
        a.vz_ms = 0.0f;
        a.have_baro = true;
        return;
    }

    /* Ground reference is the MEAN of the first GROUND_WINDOW_SAMPLES samples
     * taken while the vehicle is at rest, not a single noisy reading: one
     * 5 Pa sample carries ~0.4 m of noise, which otherwise becomes a
     * PERMANENT altitude offset (Phase 16: the 60 s stationary test measured
     * -0.56 m traced to exactly this). The estimator stays live during the
     * window (no pinning - takeoff continues normally); folding simply stops
     * once the vehicle leaves the ground (alt beyond noise band). */
    if (a.ground_samples < GROUND_WINDOW_SAMPLES && fabsf(a.alt_m) < 1.0f) {
        a.ground_sum_pa += b->pressure_pa;
        a.ground_samples++;
        a.ground_pa = a.ground_sum_pa / (float)a.ground_samples;
    }

    /* divergence guard (EST-055 class): if predict ran away, re-anchor */
    float alt_raw = pressure_to_alt(b->pressure_pa);
    float alt_ground = pressure_to_alt(a.ground_pa);
    float baro_alt = alt_raw - alt_ground;
    float err = baro_alt - a.alt_m;

    if (fabsf(err) > 50.0f) {
        a.alt_m = baro_alt;
        a.vz_ms = 0.0f;
        return;
    }

    a.alt_m += K_ALT * err;
    /* vz correction gated by a deadband: without it the 5 Pa baro noise
     * random-walks the velocity state (~-0.56 m altitude drift over 60 s,
     * found by the Phase 16 60 s stationary test). Real drift/track dynamics
     * exceed the deadband and still anchor vz. */
    if (fabsf(err) > VZ_DEADBAND) {
        a.vz_ms += K_VZ * err;
    }
}

float est_alt_get_m(void) { return a.alt_m; }
float est_alt_vz_ms(void) { return a.vz_ms; }
