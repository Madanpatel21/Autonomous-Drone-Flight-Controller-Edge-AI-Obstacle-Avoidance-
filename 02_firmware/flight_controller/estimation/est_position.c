#include "est_position.h"
#include "hal_interfaces.h"
#include <math.h>
#include <string.h>

/* Meters-per-degree at the equator (simple flat-earth model adequate for the
 * prototype flight area; documented approximation, NOT a geodetic solution). */
#define M_PER_DEG_LAT (111320.0f)
#define GNSS_TIMEOUT_S (1.0f)
#define POS_JUMP_GATE_M (25.0f)     /* innovation gate (EST-004) */

typedef struct {
    est_xy_t pos;          /* home-relative meters */
    fc_vec3_t vel;         /* x=N, y=E, z=down (m/s) */
    double lat0, lon0;
    bool  have_origin;
    uint64_t last_gnss_us;
    bool  healthy;
    int   rejected;
    int   accepted;
} pos_t;

static pos_t p;

void est_position_init(void)
{
    memset(&p, 0, sizeof(p));
    p.healthy = false;
}

void est_position_gnss(const fc_gnss_sample_t *g)
{
    if (!g || !g->valid || !g->fix_valid) return;

    if (!p.have_origin) {
        p.lat0 = g->lat_deg; p.lon0 = g->lon_deg;
        p.have_origin = true;
        p.pos.x_m = 0.0f; p.pos.y_m = 0.0f;
        p.vel.x = g->vn_ms; p.vel.y = g->ve_ms; p.vel.z = g->vd_ms;
        p.last_gnss_us = g->timestamp_us;
        p.healthy = true;
        return;
    }

    float nx = (float)((g->lat_deg - p.lat0) * M_PER_DEG_LAT);
    float ny = (float)((g->lon_deg - p.lon0) * M_PER_DEG_LAT);

    /* innovation gating (EST-004): reject teleport-class jumps */
    float dx = nx - p.pos.x_m, dy = ny - p.pos.y_m;
    if (sqrtf(dx*dx + dy*dy) > POS_JUMP_GATE_M) {
        p.rejected++;
        return;
    }
    p.accepted++;

    /* complementary correction (GNSS is authoritative low-frequency) */
    p.pos.x_m += 0.6f * dx;
    p.pos.y_m += 0.6f * dy;

    p.vel.x = 0.7f * p.vel.x + 0.3f * g->vn_ms;
    p.vel.y = 0.7f * p.vel.y + 0.3f * g->ve_ms;
    p.vel.z = 0.7f * p.vel.z + 0.3f * g->vd_ms;

    p.last_gnss_us = g->timestamp_us;
    p.healthy = true;
}

void est_position_predict(float dt_s)
{
    if (dt_s <= 0.0f || dt_s > 0.5f) return;
    p.pos.x_m += p.vel.x * dt_s;
    p.pos.y_m += p.vel.y * dt_s;
}

est_xy_t est_position_get(void)
{
    est_xy_t out = { p.pos.x_m, p.pos.y_m };
    return out;
}

fc_vec3_t est_position_velocity(void) { return p.vel; }

bool est_position_healthy(void) { return p.healthy; }

float est_position_last_gnss_age_s(void)
{
    if (p.last_gnss_us == 0) return 1e9f;
    uint64_t now = hal_time_us();
    if (now < p.last_gnss_us) return 0.0f;
    return (float)(now - p.last_gnss_us) * 1e-6f;
}

void est_position_tick(void)
{
    float age = est_position_last_gnss_age_s();
    if (age > GNSS_TIMEOUT_S) {
        p.healthy = false;   /* GNSS loss -> position hold degrades (SYS-004 path) */
    }
}
