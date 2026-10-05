#include "sim_model.h"
#include "icd02_frame.h"
#include <math.h>
#include <string.h>

/* Deterministic virtual world (Phase 22 extraction from hal_sim.c).
 * All state, physics, sensor error model and fault injection live here; no HAL
 * symbol is referenced, so the HIL rig process can link this file as the far
 * side of the HIL link. Behaviour is unchanged from the Phase 12-21 SIM
 * (same constants, same call order, same PRNG sequence): the SIM evidence base
 * carries over unchanged. */

typedef struct {
    uint64_t t_us;
    /* fault injection flags */
    bool f_imu_dropout;
    bool f_imu_stuck;
    bool f_rc_loss;
    bool f_battery_low;
    /* virtual vehicle — minimal rigid-body physics (SIM-003, Phase 12):
     * FLU frame. Attitude integrates body rates from motor moments.
     * Values are sim-class estimates for control validation only, NOT
     * identified from a real vehicle. */
    float q[4];           /* attitude quaternion (w,x,y,z), body->world */
    float w[3];           /* body rates rad/s */
    float accel[3];       /* accel output (specific force + gravity reaction) */
    /* scripted RC */
    uint16_t rc[8];
    /* last motor commands for physics */
    float motor[4];
    /* vertical dynamics (z-up, ground at z=0) */
    float vz;             /* m/s up-positive */
    float z;              /* m above ground */
    /* horizontal dynamics (Phase 21, SIM-003 completion): world-frame x/y with
     * linear drag and a constant wind disturbance. Sim-class parameters, NOT
     * identified from a real vehicle. */
    float x, y;           /* m, world frame (x east, y north) */
    float vx, vy;         /* m/s */
    float wind_x, wind_y; /* m/s disturbance */
    /* sensor error model (Phase 15, SIM-001) */
    uint32_t rng;
    bool  noise_enabled;
    float gyro_bias[3];       /* rad/s, constant per session (calibration target) */
    bool  f_gnss_loss;
    bool  f_ai_loss;
    float battery_used_mah;
    /* virtual obstacle world (Phase 17, SIM-003-class): one obstacle ahead
     * (body +x) closing at 0.5 m/s, floored at 1.5 m so long batch runs stay
     * bounded. Drives forward ToF (id 1) and the companion AI stream; ai_loss
     * kills only the companion stream, never the obstacle itself. */
    float obst_world_x;    /* world position of the virtual obstacle (m) */
    bool  obstacle_active; /* false: static obstacle (isolates other tests) */
    uint8_t ai_seq;             /* companion TX sequence (frames carry SEQ) */
    uint64_t last_fault_us;     /* when a fault was last ACTIVATED (HIL metric) */
    /* scheduled (time-triggered) fault injection: scenarios inject faults
     * IN FLIGHT, after arm + takeoff, so the fail-safe path is exercised.
     * A fault set at t=0 would keep the FC from ever arming (found in the
     * Phase 15 scenario sweep: rc_loss produced zero nav transitions). */
    struct { const char *name; uint64_t at_us; } sched[8];
    int   n_sched;
} sim_t;

static sim_t s;

/* deterministic PRNG (xorshift32) — same seed => identical trajectories (SIM-002) */
static uint32_t sim_rand_u32(void)
{
    uint32_t x = s.rng ? s.rng : 0x12345678u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s.rng = x;
    return x;
}

static float sim_rand_unit(void)   /* [0,1) */
{
    return (float)(sim_rand_u32() >> 8) / 16777216.0f;
}

/* cheap pseudo-gaussian: sum of 4 uniforms, zero mean, unit-ish variance */
static float sim_rand_gauss(void)
{
    float v = 0.0f;
    for (int i = 0; i < 4; i++) v += sim_rand_unit();
    return (v - 2.0f) * 1.732f;
}

void sim_model_reset(void)
{
    memset(&s, 0, sizeof(s));
    s.q[0] = 1.0f;
    s.rc[2] = 1500;
    s.rng = 0xC0FFEEu;
    s.noise_enabled = true;    /* nominal scenario includes sensor noise */
    s.obst_world_x = 12.0f;
    s.obstacle_active = true;
    /* sim-class gyro bias (typical MEMS after factory cal): 0.005 rad/s */
    s.gyro_bias[0] = 0.005f; s.gyro_bias[1] = -0.004f; s.gyro_bias[2] = 0.003f;
}

uint64_t sim_model_time_us(void) { return s.t_us; }
void sim_model_advance_to(uint64_t t_us) { if (t_us > s.t_us) s.t_us = t_us; }

void sim_model_set_seed(uint32_t seed) { s.rng = seed; }
void sim_model_set_noise(bool enabled) { s.noise_enabled = enabled; }

void sim_model_set_attitude(float w, float x, float y, float z)
{
    float n = sqrtf(w*w + x*x + y*y + z*z);
    if (n > 1e-8f) { w/=n; x/=n; y/=n; z/=n; }
    s.q[0] = w; s.q[1] = x; s.q[2] = y; s.q[3] = z;
}

void sim_model_get_attitude(float q[4]) { q[0]=s.q[0]; q[1]=s.q[1]; q[2]=s.q[2]; q[3]=s.q[3]; }

void sim_model_get_vertical(float *z_m, float *vz_ms)
{
    if (z_m) *z_m = s.z;
    if (vz_ms) *vz_ms = s.vz;
}

void sim_model_set_vertical(float z_m, float vz_ms)
{
    s.z = z_m;
    s.vz = vz_ms;
}

void sim_model_get_horizontal(float *x_m, float *y_m)
{
    if (x_m) *x_m = s.x;
    if (y_m) *y_m = s.y;
}

void sim_model_get_horizontal_vel(float *vx_ms, float *vy_ms)
{
    if (vx_ms) *vx_ms = s.vx;
    if (vy_ms) *vy_ms = s.vy;
}

void sim_model_set_horizontal(float x_m, float y_m, float vx_ms, float vy_ms)
{
    s.x = x_m; s.y = y_m; s.vx = vx_ms; s.vy = vy_ms;
}

void sim_model_set_wind(float wx_ms, float wy_ms)
{
    s.wind_x = wx_ms;
    s.wind_y = wy_ms;
}

/* Sim truth for the virtual obstacle: range from the VEHICLE (the obstacle is
 * at a fixed world position while the vehicle flies toward it). */
float sim_model_obstacle_range_m(void)
{
    return s.obst_world_x - s.x;
}

/* Freeze the obstacle in place (tests isolating tracking/drag/wind). */
void sim_model_set_obstacle_active(bool active)
{
    s.obstacle_active = active;
}

void sim_model_set_motor(const float motor[4])
{
    if (!motor) return;
    for (int i = 0; i < 4; i++) {
        s.motor[i] = (motor[i] < 0.0f) ? 0.0f : (motor[i] > 1.0f) ? 1.0f : motor[i];
    }
}

void sim_model_get_motor(float motor[4])
{
    if (!motor) return;
    for (int i = 0; i < 4; i++) motor[i] = s.motor[i];
}

float sim_model_battery_used_mah(void) { return s.battery_used_mah; }

uint64_t sim_model_last_fault_us(void) { return s.last_fault_us; }

#define SIM_MASS 0.65f                 /* kg, sim-class placeholder */
#define SIM_TW_RATIO 3.0f              /* thrust-to-weight at full throttle (sim-class) */
#define SIM_MAX_THRUST (SIM_MASS * 9.81f * SIM_TW_RATIO / 4.0f)  /* N per motor at 1.0 */
/* hover: 4 * (1/3) * SIM_MAX_THRUST = m*g  ->  normalised hover = 1/TW = 0.333 */
#define SIM_IXX 0.0045f                /* kg m^2 placeholder */
#define SIM_IYY 0.0045f
#define SIM_IZZ 0.0085f
#define SIM_ARM 0.115f                 /* m moment arm */
#define SIM_KM 0.05f                   /* yaw torque per normalized motor */
#define SIM_DRAG_PER_S 0.9f            /* linear horizontal drag (1/s), sim-class */

static void sim_physics_step(float dt)
{
    /* per-motor thrust; total specific force in body z */
    float t0 = s.motor[0] * SIM_MAX_THRUST;
    float t1 = s.motor[1] * SIM_MAX_THRUST;
    float t2 = s.motor[2] * SIM_MAX_THRUST;
    float t3 = s.motor[3] * SIM_MAX_THRUST;
    float fz = (t0 + t1 + t2 + t3) / SIM_MASS;   /* body z specific force */

    /* torques from mixer geometry (matches mixer_quadx signs, DEC-008) */
    float tau_x = ((-t0) + t1 + t2 - t3) * SIM_ARM;  /* roll */
    float tau_y = ((-t0) - t1 + t2 + t3) * SIM_ARM;  /* pitch */
    float tau_z = (-t0) + t1 - t2 + t3;              /* yaw, km folded in */
    tau_z *= SIM_KM;

    /* rotational dynamics (body frame, diag inertia) */
    s.w[0] += (tau_x / SIM_IXX) * dt;
    s.w[1] += (tau_y / SIM_IYY) * dt;
    s.w[2] += (tau_z / SIM_IZZ) * dt;

    /* mild aero damping so open loop doesn't spin up unbounded */
    s.w[0] *= 0.995f; s.w[1] *= 0.995f; s.w[2] *= 0.995f;

    /* quaternion integration */
    float qw = s.q[0], qx = s.q[1], qy = s.q[2], qz = s.q[3];
    float dq[4] = {
        0.5f * (-qx*s.w[0] - qy*s.w[1] - qz*s.w[2]),
        0.5f * ( qw*s.w[0] + qy*s.w[2] - qz*s.w[1]),
        0.5f * ( qw*s.w[1] - qx*s.w[2] + qz*s.w[0]),
        0.5f * ( qw*s.w[2] + qx*s.w[1] - qy*s.w[0])
    };
    s.q[0] += dq[0]*dt; s.q[1] += dq[1]*dt; s.q[2] += dq[2]*dt; s.q[3] += dq[3]*dt;
    float n = sqrtf(s.q[0]*s.q[0] + s.q[1]*s.q[1] + s.q[2]*s.q[2] + s.q[3]*s.q[3]);
    if (n > 1e-8f) {
        s.q[0]/=n; s.q[1]/=n; s.q[2]/=n; s.q[3]/=n;
    }

    /* Accel model — SIM-CLASS APPROXIMATION (not identified from a real vehicle):
     * near-hover FCs interpret the accelerometer as gravity direction (a_world≈0
     * assumption) plus the thrust delta along body z. This gives the estimator
     * realistic tilt information at hover-class thrust levels.
     *   accel_body = R^T(9.81·ẑ_world) + (fz − 9.81)·ẑ_body
     * Level hover → (0,0,+9.81). Tilted → x/y carry sin(tilt)·9.81. */
    qw = s.q[0]; qx = s.q[1]; qy = s.q[2]; qz = s.q[3];
    /* gravity in body frame = R^T(q)·ẑ_world (Hamilton, body->world q):
     * (2(xz−wy), 2(yz+wx), 1−2(x²+y²)) — matches est_attitude gravity_body() */
    float gxb =  2.0f*(qx*qz - qw*qy) * 9.81f;
    float gyb =  2.0f*(qy*qz + qw*qx) * 9.81f;
    float gzb = (1.0f - 2.0f*(qx*qx + qy*qy)) * 9.81f;

    /* vertical dynamics: the thrust vector must be counted ONCE. R(q)·ẑ_body =
     * (2(xz+wy), 2(yz−wx), 1−2(x²+y²)); the horizontal model below uses the
     * x/y components, so the vertical component here must use the z factor.
     * Without it, thrust was counted twice under tilt (full fz vertically AND
     * fz·sin(tilt) laterally): a sustained tilt then accelerated the vehicle
     * upward like a rocket (measured 9.2 m climb) and fooled est_alt. */
    float up_z = 1.0f - 2.0f * (qx * qx + qy * qy);
    float a_up = fz * up_z - 9.81f;

    /* ground reaction (missing in the airborne-only model): while on the
     * ground with insufficient thrust, the vehicle is supported and the
     * accelerometer reads +g as at rest. Without this the estimator
     * interprets ground time as free fall (found in end-to-end sim). */
    float support = 0.0f;
    if (s.z <= 0.0f && a_up <= 0.0f) support = -a_up;

    s.accel[0] = gxb;
    s.accel[1] = gyb;
    if (support > 0.0f) {
        /* resting on the ground: a stationary vehicle reads exactly 1 g in
         * BODY axes whatever its attitude, which is what est_attitude's
         * leveling expects. The old z-only term read g·cos(tilt) here and
         * left the estimator integrating a phantom vertical acceleration. */
        s.accel[2] = gzb;
    } else {
        /* airborne near-hover approximation (DEC-009): the vehicle reads 1 g
         * plus the specific force along the THRUST direction. Dividing the
         * force delta by up_z keeps a tilted hover at exactly 1 g; without it
         * a vehicle holding altitude at 25 deg reported +0.4..0.7 m/s of climb
         * and est_alt ran to 8.8 m while the vehicle stayed at 0.7 m. */
        float inv_up_z = 1.0f / (up_z > 0.5f ? up_z : 0.5f);
        s.accel[2] = 9.81f + (fz * up_z - 9.81f) * inv_up_z;
    }

    s.vz += a_up * dt;
    s.z  += s.vz * dt;
    if (s.z < 0.0f) { s.z = 0.0f; if (s.vz < 0.0f) s.vz = 0.0f; }   /* ground */

    /* --- horizontal dynamics (Phase 21) ---
     * World acceleration from the ACTUAL attitude: a_x = fz*(R(q)·ẑ)_x etc.
     * (the earlier near-hover approximation is still what the accelerometer
     * model above reports, matching the estimator; here the true rigid-body
     * response is modelled so the position loop is flown, not faked).
     * R(q)·ẑ_body (Hamilton, body->world) = (2(xz+wy), 2(yz−wx), 1−2(x²+y²)). */
    qw = s.q[0]; qx = s.q[1]; qy = s.q[2]; qz = s.q[3];
    float up_x = 2.0f * (qx * qz + qw * qy);
    float up_y = 2.0f * (qy * qz - qw * qx);

    float a_x = fz * up_x;
    float a_y = fz * up_y;
    /* linear drag on air-relative velocity + constant wind disturbance */
    a_x += -SIM_DRAG_PER_S * (s.vx - s.wind_x);
    a_y += -SIM_DRAG_PER_S * (s.vy - s.wind_y);

    s.vx += a_x * dt;
    s.vy += a_y * dt;
    s.x  += s.vx * dt;
    s.y  += s.vy * dt;
}

void sim_model_step(uint64_t dt_us)
{
    float dt = (float)dt_us * 1e-6f;
    s.t_us += dt_us;

    sim_physics_step(dt);

    /* activate any due scheduled faults (time-triggered injection) */
    for (int i = 0; i < s.n_sched; i++) {
        if (s.sched[i].name && s.t_us >= s.sched[i].at_us) {
            sim_model_fault(s.sched[i].name, true);
            s.sched[i].name = NULL;
        }
    }

    /* battery discharge model (sim-class): ~10 A at hover, 4S 4500 mAh */
    if (s.t_us > 2000000u) {
        float ma = 10000.0f * dt / 3600.0f;   /* mAh per step */
        s.battery_used_mah += ma;
    }

    /* deterministic RC: center sticks, arming channel high from t>2s */
    for (int i = 0; i < 8; i++) s.rc[i] = 1500;
    s.rc[2] = 1500;               /* throttle mid */
    s.rc[4] = (s.t_us > 2000000u) ? 1800 : 1000;  /* aux arm switch */

    /* obstacle approach (deterministic, no RNG): the obstacle ADVANCES toward
     * the flight area at 0.5 m/s, so range closes both from the environment and
     * from the vehicle flying toward it. A contact floor at 0.3 m keeps the
     * world position physical (the vehicle cannot pass through it).
     * Tests that isolate horizontal tracking disable the obstacle instead of
     * having it wander into the hover point. */
    if (s.obstacle_active) {
        s.obst_world_x -= 0.5f * dt;
        if (s.obst_world_x < s.x + 0.3f) s.obst_world_x = s.x + 0.3f;
    }

    if (s.f_battery_low) {
        /* battery drains below RTL threshold after 10 s */
    }
}

/* Schedule a fault to activate at an absolute sim time (us). */
void sim_model_schedule_fault(const char *name, uint64_t at_us)
{
    if (!name || s.n_sched >= 8) return;
    s.sched[s.n_sched].name  = name;
    s.sched[s.n_sched].at_us = at_us;
    s.n_sched++;
}

void sim_model_fault(const char *name, bool active)
{
    if (!name) return;
    if (active) s.last_fault_us = s.t_us;
    if      (!strcmp(name, "imu_dropout"))  s.f_imu_dropout  = active;
    else if (!strcmp(name, "imu_stuck"))    s.f_imu_stuck    = active;
    else if (!strcmp(name, "rc_loss"))      s.f_rc_loss      = active;
    else if (!strcmp(name, "battery_low"))  s.f_battery_low  = active;
    else if (!strcmp(name, "gnss_loss"))    s.f_gnss_loss    = active;
    else if (!strcmp(name, "ai_loss"))      s.f_ai_loss      = active;
}

/* Scenario presets (FC_SIM_SCENARIO env or argv in main_sim.c; HIL rig CLI).
 * Faults are scheduled IN FLIGHT (see sched[] rationale above): arm at 2 s,
 * takeoff completes ~5 s, so injections at 6-10 s exercise the airborne
 * fail-safe / degradation paths rather than blocking arming. */
void sim_model_scenario(const char *name)
{
    if (!name || !strcmp(name, "nominal")) return;
    if      (!strcmp(name, "imu_dropout")) sim_model_schedule_fault("imu_dropout", 6000000u);
    else if (!strcmp(name, "rc_loss"))     sim_model_schedule_fault("rc_loss",     8000000u);
    /* Phase 24 cross-domain scenario: RC link AND IMU die together. Single-fault
     * scenarios cannot reach the case that matters most, where the reported
     * failsafe (RC_LOSS, which implies a flyable RTL) is not what the vehicle
     * can actually do (motor stop, no attitude authority). Staggered by 200 ms
     * so both timers expire in flight. */
    else if (!strcmp(name, "imu_rc_loss")) {
        sim_model_schedule_fault("imu_dropout", 6000000u);
        sim_model_schedule_fault("rc_loss",     6200000u);
    }
    else if (!strcmp(name, "battery_low")) sim_model_schedule_fault("battery_low", 10000000u);
    else if (!strcmp(name, "gnss_loss"))   sim_model_schedule_fault("gnss_loss",   6000000u);
    else if (!strcmp(name, "ai_loss"))     sim_model_schedule_fault("ai_loss",     6000000u);
    else if (!strcmp(name, "noiseless"))   s.noise_enabled = false;
}

hal_status_t sim_model_read_imu(fc_imu_sample_t *out)
{
    if (!out) return HAL_ERROR;
    if (s.f_imu_dropout) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = s.t_us;
    out->valid = true;
    out->accel_m_s2.x = s.accel[0];
    out->accel_m_s2.y = s.accel[1];
    out->accel_m_s2.z = s.accel[2];
    out->gyro_rad_s.x = s.w[0];
    out->gyro_rad_s.y = s.w[1];
    out->gyro_rad_s.z = s.w[2];

    if (s.noise_enabled) {
        /* SIM-001 noise classes (sim-class magnitudes, documented):
         * gyro 0.01 rad/s RMS, accel 0.10 m/s^2 RMS, plus constant gyro bias */
        out->accel_m_s2.x += 0.10f * sim_rand_gauss();
        out->accel_m_s2.y += 0.10f * sim_rand_gauss();
        out->accel_m_s2.z += 0.10f * sim_rand_gauss();
        out->gyro_rad_s.x += s.gyro_bias[0] + 0.01f * sim_rand_gauss();
        out->gyro_rad_s.y += s.gyro_bias[1] + 0.01f * sim_rand_gauss();
        out->gyro_rad_s.z += s.gyro_bias[2] + 0.01f * sim_rand_gauss();
    }
    return HAL_OK;
}

hal_status_t sim_model_read_baro(fc_baro_sample_t *out)
{
    if (!out) return HAL_ERROR;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = s.t_us;
    /* ISA pressure at current sim altitude (inverse of estimator formula) */
    float z = s.z;
    float ratio = 1.0f - z / 44330.0f;
    if (ratio < 0.05f) ratio = 0.05f;
    float p = 101325.0f * powf(ratio, 5.25588f);
    if (s.noise_enabled) p += 5.0f * sim_rand_gauss();   /* sim-class 5 Pa RMS (~0.4 m) */
    out->pressure_pa = p;
    out->temperature_c = 20.0f;
    out->valid = true;
    return HAL_OK;
}

hal_status_t sim_model_read_battery(fc_battery_sample_t *out)
{
    if (!out) return HAL_ERROR;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = s.t_us;
    float cell = s.f_battery_low ? 3.05f : 3.8f;
    for (int i = 0; i < 4; i++) out->cell_v[i] = cell;
    out->pack_v = cell * 4.0f;
    out->current_a = 10.0f;
    out->valid = true;
    return HAL_OK;
}

hal_status_t sim_model_read_rc(fc_rc_frame_t *out)
{
    if (!out) return HAL_ERROR;
    if (s.f_rc_loss) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = s.t_us;
    for (int i = 0; i < 8; i++) out->channels[i] = s.rc[i];
    out->frames_valid = true;
    out->valid = true;
    return HAL_OK;
}

hal_status_t sim_model_read_gnss(fc_gnss_sample_t *out)
{
    if (!out) return HAL_ERROR;
    if (s.f_gnss_loss) return HAL_NOT_READY;
    if (s.t_us < 3000000u) return HAL_NOT_READY;   /* 3 s to first fix (sim-class) */

    memset(out, 0, sizeof(*out));
    out->timestamp_us = s.t_us;
    /* virtual position/velocity: the vehicle's simulated world position is
     * reported as a local-tangent lat/lon (flat-earth model matching
     * est_position.c: 111320 m/deg) with sim-class GNSS noise. Marked SIMULATED. */
    out->lat_deg = (double)s.x / 111320.0;
    out->lon_deg = (double)s.y / 111320.0;
    out->alt_m = s.z;
    out->vn_ms = s.vx; out->ve_ms = s.vy; out->vd_ms = -s.vz;
    if (s.noise_enabled) {
        out->vn_ms += 0.10f * sim_rand_gauss();   /* sim-class GNSS velocity noise */
        out->ve_ms += 0.10f * sim_rand_gauss();
        out->alt_m += 0.50f * sim_rand_gauss();
    }
    out->sats = 12;
    out->fix_valid = true;
    out->valid = true;
    return HAL_OK;
}

hal_status_t sim_model_read_tof(int id, fc_tof_sample_t *out)
{
    if (!out) return HAL_ERROR;
    memset(out, 0, sizeof(*out));

    if (id == 0) {
        /* downward range: altitude above ground + noise (TF-Luna class) */
        out->timestamp_us = s.t_us;
        out->range_m = s.z + (s.noise_enabled ? 0.01f * sim_rand_gauss() : 0.0f);
        out->valid = true;
        return HAL_OK;
    }
    if (id == 1) {
        /* forward range: virtual obstacle world (Phase 17). ±2 cm sim-class
         * noise, TF-Luna class range limit 12 m. */
        out->timestamp_us = s.t_us;
        out->range_m = s.obst_world_x - s.x;
        if (s.noise_enabled) out->range_m += 0.02f * sim_rand_gauss();
        if (out->range_m > 12.0f) out->range_m = 12.0f;   /* beyond spec = max-range */
        out->valid = true;
        return HAL_OK;
    }
    return HAL_NOT_READY;
}

hal_status_t sim_model_read_ai(fc_ai_obstacle_set_t *out)
{
    if (!out) return HAL_ERROR;
    if (s.f_ai_loss) return HAL_NOT_READY;    /* companion dead (stream stops) */
    if ((s.t_us % 40000u) != 0u) return HAL_NOT_READY;   /* 25 Hz publish slot */

    memset(out, 0, sizeof(*out));
    out->timestamp_us = s.t_us;
    out->count = 1;
    out->det[0].pos_m.x = s.obst_world_x - s.x;
    out->det[0].pos_m.y = 0.0f;
    out->det[0].pos_m.z = 0.0f;
    out->det[0].vel_m_s.x = -0.5f;
    out->det[0].radius_m = 0.25f;
    out->det[0].confidence = 0.90f;
    out->det[0].class_id = 1;
    if (s.noise_enabled) {
        out->det[0].pos_m.x += 0.05f * sim_rand_gauss();
        out->det[0].pos_m.y += 0.05f * sim_rand_gauss();
    }
    return HAL_OK;
}

/* Byte-level virtual companion UART (Phase 19, DEC-003/015): emits REAL ICD-02
 * frames (HEARTBEAT 1 Hz, OBSTACLE_SET 25 Hz, HEALTH 1 Hz) so the FC-side
 * companion link exercises serialize -> ring -> CRC -> parse exactly as it will
 * over USART6 + DMA. ai_loss stops the stream entirely (companion dead); the
 * forward ToF keeps measuring the obstacle. On the HIL link these same bytes
 * cross the transport, so the companion path is exercised over a real byte
 * channel instead of a function call. */
size_t sim_model_companion_uart(uint8_t *out, size_t cap, size_t *n_out)
{
    if (!out || !n_out) return 0;
    *n_out = 0;
    if (s.f_ai_loss) return 0;

    uint8_t payload[ICD02_MAX_PAYLOAD];
    size_t used = 0;

    /* HEARTBEAT at 1 Hz */
    if ((s.t_us % 1000000u) == 0u) {
        uint32_t uptime = (uint32_t)(s.t_us / 1000000u);
        uint8_t st = 2u;   /* CL_AI_RUNNING */
        payload[0] = (uint8_t)(uptime & 0xFFu);
        payload[1] = (uint8_t)((uptime >> 8) & 0xFFu);
        payload[2] = (uint8_t)((uptime >> 16) & 0xFFu);
        payload[3] = (uint8_t)((uptime >> 24) & 0xFFu);
        payload[4] = st;
        payload[5] = 0x01u;   /* model_id lo */
        payload[6] = 0x00u;   /* model_id hi */
        size_t l = 0;
        if (icd02_encode(ICD02_TYPE_HEARTBEAT, s.ai_seq++, payload, 7u,
                         out + used, cap - used, &l)) used += l;
    }
    /* OBSTACLE_SET at 25 Hz */
    if ((s.t_us % 40000u) == 0u) {
        fc_ai_obstacle_set_t set;
        memset(&set, 0, sizeof(set));
        set.count = 1;
        set.det[0].pos_m.x = s.obst_world_x - s.x;
        set.det[0].vel_m_s.x = -0.5f;
        set.det[0].radius_m = 0.25f;
        set.det[0].confidence = 0.90f;
        set.det[0].class_id = 1;
        if (s.noise_enabled) {
            set.det[0].pos_m.x += 0.05f * sim_rand_gauss();
            set.det[0].pos_m.y += 0.05f * sim_rand_gauss();
        }
        uint8_t plen = icd02_encode_obstacle_set(payload, sizeof(payload), &set);
        if (plen > 0u) {
            size_t l = 0;
            if (icd02_encode(ICD02_TYPE_OBSTACLE_SET, s.ai_seq++, payload, plen,
                             out + used, cap - used, &l)) used += l;
        }
    }
    /* HEALTH at 1 Hz (offset so it does not collide with the heartbeat slot) */
    if ((s.t_us % 1000000u) == 500000u) {
        int16_t temp = 420;    /* 42.0 degC sim-class */
        payload[0] = 35u;       /* cpu % */
        payload[1] = 48u;       /* mem % */
        payload[2] = (uint8_t)((uint16_t)temp & 0xFFu);
        payload[3] = (uint8_t)(((uint16_t)temp >> 8) & 0xFFu);
        payload[4] = 1u;        /* camera ok */
        payload[5] = 6u;        /* inference_ms lo */
        payload[6] = 0u;        /* inference_ms hi */
        size_t l = 0;
        if (icd02_encode(ICD02_TYPE_HEALTH, s.ai_seq++, payload, 7u,
                         out + used, cap - used, &l)) used += l;
    }

    *n_out = used;
    return used;
}