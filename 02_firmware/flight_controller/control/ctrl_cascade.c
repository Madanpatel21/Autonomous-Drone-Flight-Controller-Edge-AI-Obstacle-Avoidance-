#include "ctrl_cascade.h"
#include "mixer_quadx.h"
#include "motor_output.h"
#include "est_attitude.h"
#include "est_alt.h"
#include "hal_interfaces.h"
#include <math.h>
#include <string.h>

/* CTRL-001..006. All gains are placeholder-initialization values from classic
 * 5-inch quad practice; they are NOT tuned/verified numbers and must be
 * identified via sim sweep (Phase 12) before any hardware use. */

typedef struct {
    float kp, ki, kd;
    float i_term;
    float i_max;
    float out_max;
    float prev_meas;
} pid_t;

typedef struct {
    pid_t rate_roll, rate_pitch, rate_yaw;
    pid_t att_roll, att_pitch, att_yaw;
    pid_t alt_pid, vz_pid;      /* altitude hold (Phase 14) */
    fc_vec3_t rate_sp;    /* rad/s */
    fc_quat_t att_sp;     /* attitude setpoint */
    float motor[4];
    float hover_throttle;   /* armed collective (Phase 12: from altitude loop) */
    float alt_sp;           /* m, up-positive */
    bool alt_hold;
    bool armed;
} ctrl_t;

static ctrl_t c;

static float clampf(float v, float lim) { return (v > lim) ? lim : (v < -lim) ? -lim : v; }

static void pid_init(pid_t *p, float kp, float ki, float kd, float i_max, float out_max)
{
    p->kp = kp; p->ki = ki; p->kd = kd;
    p->i_term = 0.0f; p->i_max = i_max; p->out_max = out_max; p->prev_meas = 0.0f;
}

void ctrl_cascade_init(void)
{
    memset(&c, 0, sizeof(c));
    c.att_sp.w = 1.0f;
    c.armed = false;

    /* UNVERIFIED initial gains — Phase 12 sweep required (see header comment) */
    pid_init(&c.rate_roll,  0.135f, 0.05f, 0.003f, 0.3f, 1.0f);
    pid_init(&c.rate_pitch, 0.135f, 0.05f, 0.003f, 0.3f, 1.0f);
    pid_init(&c.rate_yaw,   0.20f,  0.03f, 0.0f,   0.3f, 1.0f);
    pid_init(&c.att_roll,   4.0f,   0.0f,  0.0f,   0.0f, 3.0f);
    pid_init(&c.att_pitch,  4.0f,   0.0f,  0.0f,   0.0f, 3.0f);
    pid_init(&c.att_yaw,    2.0f,   0.0f,  0.0f,   0.0f, 2.0f);

    /* UNVERIFIED initial gains — Phase 12/14/15 sim-validated, formal tuning Phase 21 */
    pid_init(&c.alt_pid, 1.2f, 0.05f, 0.0f, 0.20f, 0.80f);   /* alt err -> vz_sp */
    pid_init(&c.vz_pid,  0.40f, 0.08f, 0.0f, 0.05f, 0.10f);  /* vz err  -> throttle offset */
    c.alt_hold = false;
    c.alt_sp = 0.0f;
}



/* PID with derivative-on-measurement and anti-windup (back-calculation) (CTRL-001) */
static float pid_run(pid_t *p, float sp, float meas, float dt)
{
    float err = sp - meas;
    float d = (dt > 0.0f) ? -(meas - p->prev_meas) / dt : 0.0f;  /* derivative on measurement */
    p->prev_meas = meas;

    float unsat = p->kp * err + p->i_term + p->kd * d;
    float out = clampf(unsat, p->out_max);

    /* integrate with anti-windup: stop integrating when saturated */
    if (out == unsat) {
        p->i_term = clampf(p->i_term + p->ki * err * dt, p->i_max);
    }
    return out;
}

void ctrl_cascade_run(const fc_imu_sample_t *imu_in)
{
    fc_imu_sample_t local;
    const fc_imu_sample_t *imu = imu_in;

    if (!imu) {
        /* convenience for tests/tools: run the full 1 kHz chain
         * (read -> attitude estimate -> control). app_main() passes a
         * pre-estimated sample instead, so this path is not used there. */
        if (hal_imu_read(&local) != HAL_OK) {
            float zero[4] = {0,0,0,0};
            for (int i = 0; i < 4; i++) c.motor[i] = 0.0f;
            motor_output_apply(c.motor, hal_time_us());
            hal_actuator_write(zero);
            return;
        }
        est_attitude_update(&local);
        imu = &local;
    }

    if (!imu->valid) {
        /* No valid IMU: zero outputs (CTRL-005, SAF-003). c.motor is zeroed as
         * well so telemetry/blackbox reflect the actual actuator command —
         * Phase 15: leaving it stale reported pre-failure motor values. */
        float zero[4] = {0,0,0,0};
        for (int i = 0; i < 4; i++) c.motor[i] = 0.0f;
        motor_output_apply(c.motor, hal_time_us());
        hal_actuator_write(zero);
        return;
    }

    est_alt_imu_feed(imu);
    fc_vec3_t rates = est_attitude_rates_rad_s();

    /* rate loop at 1 kHz (CTRL-002). Altitude/position loops integrate Phase 12+. */
    float u_roll  = pid_run(&c.rate_roll,  c.rate_sp.x, rates.x, 0.001f);
    float u_pitch = pid_run(&c.rate_pitch, c.rate_sp.y, rates.y, 0.001f);
    float u_yaw   = pid_run(&c.rate_yaw,   c.rate_sp.z, rates.z, 0.001f);

    /* collective: 0 when disarmed (CTRL-005); hover_throttle when armed */
    float collective = c.armed ? c.hover_throttle : 0.0f;

    mixer_quadx_run(collective, u_roll, u_pitch, u_yaw, c.motor);

    /* arming/stale interlock gates the actuator path (SAF-003/005) */
    motor_output_apply(c.motor, hal_time_us());
    hal_actuator_write(c.motor);
}

void ctrl_attitude_loop(void)
{
    /* 250 Hz attitude loop (CTRL-002): quaternion error -> rate setpoints.
     * q_err = qs⁻¹ ⊗ q_est (rotation from setpoint to estimate, body frame);
     * to drive the error to zero the vehicle must rotate OPPOSITE the error
     * vector, hence meas=error, sp=0 (negative feedback). Verified in sim
     * closed loop (TEST-CTRL-HOV). */
    fc_quat_t q = est_attitude_quat();
    fc_quat_t qs = c.att_sp;

    float ew =  qs.w*q.w + qs.x*q.x + qs.y*q.y + qs.z*q.z;
    float ex =  qs.w*q.x - qs.x*q.w - qs.y*q.z + qs.z*q.y;
    float ey =  qs.w*q.y + qs.x*q.z - qs.y*q.w - qs.z*q.x;
    float ez =  qs.w*q.z - qs.x*q.y + qs.y*q.x - qs.z*q.w;
    if (ew < 0) { ex = -ex; ey = -ey; ez = -ez; }

    const float dt_att = 0.004f;
    c.rate_sp.x = pid_run(&c.att_roll,  0.0f, ex, dt_att);
    c.rate_sp.y = pid_run(&c.att_pitch, 0.0f, ey, dt_att);
    c.rate_sp.z = pid_run(&c.att_yaw,   0.0f, ez, dt_att);
}

void ctrl_set_rate_sp(const fc_vec3_t *rate_sp) { if (rate_sp) c.rate_sp = *rate_sp; }
void ctrl_set_armed(bool armed)
{
    if (armed && !c.armed) {
        /* fresh arm: reset integrators (CTRL-001) */
        c.rate_roll.i_term = 0; c.rate_pitch.i_term = 0; c.rate_yaw.i_term = 0;
    }
    c.armed = armed;
    motor_output_set_armed(armed);   /* keep output-stage interlock in sync */
}
void ctrl_set_hover_throttle(float t)
{
    c.hover_throttle = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
}

void ctrl_set_altitude_sp(float altitude_m)
{
    c.alt_sp = altitude_m;
    c.alt_hold = true;
}

void ctrl_altitude_hold(bool enable)
{
    c.alt_hold = enable;
    if (enable) { c.alt_pid.i_term = 0.0f; c.vz_pid.i_term = 0.0f; }
}

void ctrl_altitude_loop(void)
{
    if (!c.alt_hold || !c.armed) return;

    float alt = est_alt_get_m();
    float vz  = est_alt_vz_ms();
    const float dt = 0.01f;   /* 100 Hz */

    float vz_sp = pid_run(&c.alt_pid, c.alt_sp, alt, dt);      /* clamped +-0.8 m/s */
    float offset = pid_run(&c.vz_pid, vz_sp, vz, dt);          /* clamped +-0.30 */

    /* hover base = 1/thrust-to-weight; sim-class T/W=3 -> 0.333 (Phase 15).
     * Real vehicle value comes from thrust calibration (Phase 21, H-gated). */
    c.hover_throttle = (1.0f / 3.0f) + offset;
    if (c.hover_throttle < 0.0f) c.hover_throttle = 0.0f;
    if (c.hover_throttle > 1.0f) c.hover_throttle = 1.0f;
}
void ctrl_set_attitude_sp(const fc_quat_t *q_sp)
{
    if (!q_sp) return;
    c.att_sp = *q_sp;
    float n = sqrtf(c.att_sp.w*c.att_sp.w + c.att_sp.x*c.att_sp.x +
                    c.att_sp.y*c.att_sp.y + c.att_sp.z*c.att_sp.z);
    if (n > 1e-8f) { c.att_sp.w/=n; c.att_sp.x/=n; c.att_sp.y/=n; c.att_sp.z/=n; }
}
const float* ctrl_get_motor_outputs(void) { return c.motor; }
