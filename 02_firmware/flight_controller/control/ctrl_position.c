#include "ctrl_position.h"
#include "ctrl_cascade.h"
#include "est_alt.h"
#include <math.h>
#include <string.h>

/* Position/velocity controller implementation (Phase 21, DEC-017).
 *
 *   err      = pos_sp - pos_est
 *   vel_sp   = kp_pos * err              (or the avoidance override)
 *   acc_sp   = kp_vel * (vel_sp - v_est)
 *   tilt     = atan(acc_sp / g)          (pitch for x, -roll for y, DEC-008)
 *   attitude SP = tilt quaternion with the CURRENT yaw preserved
 *
 * Every stage is clamped to the FC-owned airframe limits (AF_*), with the
 * clamps applied before the attitude setpoint is published, so the airframe
 * limit is the last word. Deterministic: no RNG, no allocation, no hidden
 * state beyond the integral-free P loops. */

#define KP_POS 1.20f    /* 1/s    — Phase 21 sim sweep (SIM_TUNING_RECORD.md) */
#define KI_POS 0.35f    /* 1/s^2  — Phase 21: rejects a steady wind */
#define KP_VEL 2.50f    /* 1/s    — Phase 21 sim sweep */
#define GRAV   9.81f

typedef struct {
    float sp_x, sp_y;
    float i_x, i_y;            /* position-loop integral (wind rejection) */
    fc_vec3_t vel_override;
    bool override_active;
    fc_vec3_t vel_sp;
    fc_vec3_t acc_sp;
    float tilt_rad;
    float cur_pitch, cur_roll; /* slew-limited commanded tilt */
    bool  tilt_init;
} pos_t;

static pos_t p;

static float clampf(float v, float lim)
{
    return (v > lim) ? lim : ((v < -lim) ? -lim : v);
}

static float clamp_norm2(float *x, float *y, float lim)
{
    float n = sqrtf(*x * *x + *y * *y);
    if (n > lim && n > 1e-6f) {
        float k = lim / n;
        *x *= k;
        *y *= k;
        n = lim;
    }
    return n;
}

/* yaw of a body->world quaternion (Hamilton, DEC-008) */
static float yaw_of(const fc_quat_t *q)
{
    return atan2f(2.0f * (q->w * q->z + q->x * q->y),
                  1.0f - 2.0f * (q->y * q->y + q->z * q->z));
}

void ctrl_position_init(void)
{
    memset(&p, 0, sizeof(p));
}

/* Integral action is required, not optional: with P-only control a steady
 * wind produces a steady-state drift (measured 0.40 m/s -> 3.4 m over 12 s in
 * TEST-SIM-WIND). The integrator is clamped to 80% of the velocity limit and
 * frozen (anti-windup) while the avoidance override owns the request. */
#define I_CLAMP (AF_VEL_MAX_M_S * 0.8f)
#define AIRBORNE_MIN_M   0.30f   /* below this: no horizontal authority */
#define AIRBORNE_FULL_M  1.00f   /* above this: full horizontal authority */
#define TILT_SLEW_RAD_S   1.05f   /* 60 deg/s command slew (see below) */

void ctrl_position_setpoint_xy(float x_m, float y_m)
{
    p.sp_x = x_m;
    p.sp_y = y_m;
}

void ctrl_position_vel_override(const fc_vec3_t *v_world_m_s, bool active)
{
    if (active && v_world_m_s) p.vel_override = *v_world_m_s;
    p.override_active = active;
}

fc_vec3_t ctrl_position_vel_sp(void) { return p.vel_sp; }
fc_vec3_t ctrl_position_acc_sp(void) { return p.acc_sp; }
float     ctrl_position_tilt_rad(void) { return p.tilt_rad; }

void ctrl_position_loop(float dt_s, const fc_quat_t *att_q)
{
    (void)dt_s;   /* P loops are dt-independent by construction */
    est_xy_t e = est_position_get();
    fc_vec3_t v = est_position_velocity();

    /* --- velocity request ---
     * Airborne gate: commanding full tilt while the vehicle is on or near the
     * ground drove it back into the ground (measured: true altitude 0.79 ->
     * 0.12 m while the estimator climbed to 1.9 m, desynchronizing est_alt).
     * The horizontal request now scales in between AIRBORNE_MIN/FULL_M. */
    float alt = est_alt_get_m();
    float gate = (alt - AIRBORNE_MIN_M) / (AIRBORNE_FULL_M - AIRBORNE_MIN_M);
    if (gate < 0.0f) gate = 0.0f;
    if (gate > 1.0f) gate = 1.0f;

    float vx, vy;
    if (p.override_active) {
        /* avoidance (Phase 18) owns the horizontal request while active; its
         * own SAF-040 clamp already bounds this, the AF clamp is repeated
         * here as the last word. The integrator is frozen while overridden. */
        vx = p.vel_override.x;
        vy = p.vel_override.y;
    } else {
        float ex = p.sp_x - e.x_m;
        float ey = p.sp_y - e.y_m;
        if (gate > 0.0f) {
            p.i_x = clampf(p.i_x + KI_POS * ex * dt_s, I_CLAMP);
            p.i_y = clampf(p.i_y + KI_POS * ey * dt_s, I_CLAMP);
        }
        vx = (KP_POS * ex + p.i_x) * gate;
        vy = (KP_POS * ey + p.i_y) * gate;
    }
    clamp_norm2(&vx, &vy, AF_VEL_MAX_M_S);

    /* --- acceleration request (velocity error) --- */
    float ax = KP_VEL * (vx - v.x);
    float ay = KP_VEL * (vy - v.y);
    clamp_norm2(&ax, &ay, AF_ACC_MAX_M_S2);   /* magnitude limit, not per-axis */

    p.vel_sp = (fc_vec3_t){ vx, vy, 0.0f };
    p.acc_sp = (fc_vec3_t){ ax, ay, 0.0f };

    /* --- tilt setpoint: pitch tilts thrust toward +x, roll toward -y (FLU) ---
     * Slew limited (60 deg/s): an instantaneous step to a large tilt is both
     * unrealistically aggressive for the airframe and what dragged the vehicle
     * into the ground at takeoff (Phase 21 finding). */
    float theta_pitch = atan2f(ax, GRAV);
    float theta_roll  = -atan2f(ay, GRAV);
    float tmax = AF_TILT_MAX_RAD;
    if (theta_pitch >  tmax) theta_pitch =  tmax;
    if (theta_pitch < -tmax) theta_pitch = -tmax;
    if (theta_roll  >  tmax) theta_roll  =  tmax;
    if (theta_roll  < -tmax) theta_roll  = -tmax;

    if (!p.tilt_init) {
        p.cur_pitch = theta_pitch;
        p.cur_roll  = theta_roll;
        p.tilt_init = true;
    }
    float dmax = TILT_SLEW_RAD_S * ((dt_s > 0.0f) ? dt_s : 0.02f);
    p.cur_pitch += clampf(theta_pitch - p.cur_pitch, dmax);
    p.cur_roll  += clampf(theta_roll  - p.cur_roll,  dmax);
    theta_pitch = p.cur_pitch;
    theta_roll  = p.cur_roll;
    p.tilt_rad = sqrtf(theta_roll * theta_roll + theta_pitch * theta_pitch);

    /* attitude quaternion: z-y-x Euler with the CURRENT yaw preserved */
    float cy = cosf(yaw_of(att_q) * 0.5f), sy = sinf(yaw_of(att_q) * 0.5f);
    float cp = cosf(theta_pitch * 0.5f), sp_ = sinf(theta_pitch * 0.5f);
    float cr = cosf(theta_roll * 0.5f),  sr = sinf(theta_roll * 0.5f);
    fc_quat_t q_sp;
    q_sp.w = cr * cp * cy + sr * sp_ * sy;
    q_sp.x = sr * cp * cy - cr * sp_ * sy;
    q_sp.y = cr * sp_ * cy + sr * cp * sy;
    q_sp.z = cr * cp * sy - sr * sp_ * cy;
    float n = sqrtf(q_sp.w*q_sp.w + q_sp.x*q_sp.x + q_sp.y*q_sp.y + q_sp.z*q_sp.z);
    if (n > 1e-6f) { q_sp.w /= n; q_sp.x /= n; q_sp.y /= n; q_sp.z /= n; }

    ctrl_set_attitude_sp(&q_sp);
}
