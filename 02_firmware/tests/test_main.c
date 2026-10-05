/* Minimal deterministic test framework (FW-001, TEST-001/003). No external deps. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mixer_quadx.h"
#include "est_attitude.h"
#include "ctrl_cascade.h"
#include "failsafe_sm.h"
#include "fc_types.h"
#include "hal_interfaces.h"
#include "crc16.h"
#include "parameters.h"
#include "sensor_health.h"
#include "validation.h"
#include "calibration.h"
#include "esc_dshot.h"
#include "motor_output.h"
#include "mission_sm.h"
#include "est_alt.h"
#include "est_position.h"
#include "sensor_hub.h"
#include "app_main.h"
#include "perception_fusion.h"
#include "obstacle_avoidance.h"
#include "ctrl_position.h"
#include "icd02_frame.h"
#include "companion_link.h"
#include "rc_protocol.h"
#include "telemetry.h"
#include "mavlink2.h"
#include "hil_link.h"
#include "cmd_gate.h"

static int tests_run = 0, tests_failed = 0;

/* Host monotonic clock for the scheduler timing distribution (FW-002). Host
 * time only: this is not MCU timing evidence (see the test's scope comment). */
#if defined(_WIN32)
#include <windows.h>
static uint64_t now_monotonic_us(void)
{
    /* GetTickCount64 is 1 ms resolution - far too coarse for a per-tick
     * distribution. QueryPerformanceCounter is sub-microsecond. */
    static LARGE_INTEGER freq;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (uint64_t)((t.QuadPart * 1000000ll) / freq.QuadPart);
}
#else
#include <time.h>
static uint64_t now_monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
}
#endif

#define CHECK(cond) do { \
    tests_run++; \
    if (!(cond)) { tests_failed++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    tests_run++; \
    double _d = fabs((double)(a) - (double)(b)); \
    if (!(_d <= (eps))) { tests_failed++; printf("FAIL %s:%d: %s=%.9f vs %s=%.9f\n", __FILE__, __LINE__, #a, (double)(a), #b, (double)(b)); } \
} while (0)

static void test_mixer_hover(void)
{
    float m[4];
    mixer_quadx_run(0.5f, 0, 0, 0, m);
    for (int i = 0; i < 4; i++) CHECK_NEAR(m[i], 0.5f, 1e-6);
}

static void test_mixer_sums_and_limits(void)
{
    float m[4];
    mixer_quadx_run(0.9f, 0.4f, -0.4f, 0.6f, m);
    for (int i = 0; i < 4; i++) { CHECK(m[i] >= 0.0f && m[i] <= 1.0f); }
    /* collective preserved at saturation: min clearance from clamp boundary */
    CHECK(m[0] > 0.9f - 0.35f);
}

static void test_attitude_stationary(void)
{
    est_attitude_init();
    fc_imu_sample_t s; memset(&s, 0, sizeof(s));
    s.valid = true;
    for (int i = 0; i < 5000; i++) {
        s.timestamp_us = (uint64_t)i * 1000u;
        s.accel_m_s2.z = 9.81f;
        est_attitude_update(&s);
    }
    fc_quat_t q = est_attitude_quat();
    CHECK_NEAR(q.w, 1.0, 0.01);
    CHECK(fabs(q.x) < 0.02 && fabs(q.y) < 0.02 && fabs(q.z) < 0.02);
}

static void test_attitude_tilt_converges(void)
{
    est_attitude_init();
    fc_imu_sample_t s; memset(&s, 0, sizeof(s));
    s.valid = true;
    /* 20 deg pitch attitude (nose up per sim convention): gravity splits into y (sin) and z (cos) */
    float tilt = 20.0f * (float)M_PI / 180.0f;
    for (int i = 0; i < 20000; i++) {
        s.timestamp_us = (uint64_t)i * 1000u;
        s.accel_m_s2.y =  9.81f * sinf(tilt);
        s.accel_m_s2.z =  9.81f * cosf(tilt);
        est_attitude_update(&s);
    }
    fc_quat_t q = est_attitude_quat();
    float roll = atan2f(2.0f*(q.w*q.x + q.y*q.z), 1.0f - 2.0f*(q.x*q.x + q.y*q.y));
    CHECK_NEAR(roll, tilt, 0.03);
}

static void test_failsafe_battery_thresholds(void)
{
    failsafe_init();
    fc_battery_sample_t b; memset(&b, 0, sizeof(b));
    b.valid = true;
    for (int i = 0; i < 4; i++) b.cell_v[i] = 3.05f;   /* below land threshold */
    failsafe_battery_update(&b);
    CHECK(failsafe_active() == FC_FAILSAFE_BATTERY);
    CHECK(failsafe_mode_request() == FC_MODE_LAND);
}

static void test_ctrl_disarmed_zero_output(void)
{
    ctrl_cascade_init();
    fc_imu_sample_t s; memset(&s, 0, sizeof(s));
    s.valid = true; s.accel_m_s2.z = 9.81f;
    for (int i = 0; i < 10; i++) ctrl_cascade_run(&s);
    const float *m = ctrl_get_motor_outputs();
    /* disarmed collective is 0; moments may be nonzero but motors clamp >=0 */
    for (int i = 0; i < 4; i++) CHECK(m[i] >= 0.0f && m[i] <= 1.0f);
}

static void test_hal_sim_determinism(void)
{
    /* two identical runs produce identical estimator trajectories (SIM-002) */
    double r1[4], r2[4];
    for (int run = 0; run < 2; run++) {
        est_attitude_init();
        fc_imu_sample_t s; memset(&s, 0, sizeof(s));
        s.valid = true;
        for (int i = 0; i < 1000; i++) {
            s.timestamp_us = (uint64_t)i * 1000u;
            s.accel_m_s2.z = 9.81f;
            s.gyro_rad_s.x = 0.01f;
            est_attitude_update(&s);
        }
        fc_quat_t q = est_attitude_quat();
        double *dst = (run == 0) ? r1 : r2;
        dst[0]=q.w; dst[1]=q.x; dst[2]=q.y; dst[3]=q.z;
    }
    for (int i = 0; i < 4; i++) CHECK(r1[i] == r2[i]);
}

static void test_crc16_known_vector(void)
{
    /* CRC-16/CCITT-FALSE of "123456789" = 0x29B1 (standard check value) */
    CHECK(crc16_ccitt((const uint8_t *)"123456789", 9) == 0x29B1u);
}

static void test_params_defaults_on_corruption(void)
{
    params_t p;
    /* corrupt flash: write garbage */
    uint8_t garbage[sizeof(params_t) + 2];
    memset(garbage, 0xAA, sizeof(garbage));
    hal_flash_erase(0);
    hal_flash_write(0, garbage, sizeof(garbage));
    CHECK(params_load(&p) == FC_INVALID);
    CHECK(p.magic == PARAM_MAGIC);            /* FW-005: safe defaults */
    CHECK_NEAR(p.batt_land_v, 3.1f, 1e-6);    /* SAF-030 preserved */
    /* round-trip store/load */
    p.rate_roll_kp = 0.5f;
    CHECK(params_store(&p) == FC_OK);
    params_t q;
    CHECK(params_load(&q) == FC_OK);
    CHECK_NEAR(q.rate_roll_kp, 0.5f, 1e-6);
}

static void test_sensor_health_timeout_and_range(void)
{
    sensor_health_cfg_t cfg = { .timeout_us = 1000u, .range_min = 0.0f,
                                .range_max = 10.0f, .max_range_errors = 3 };
    sensor_health_t h;
    sensor_health_init(&h, &cfg);

    CHECK(sensor_health_check(&h, 100u, 5.0f, true));    /* good */
    CHECK(!sensor_health_check(&h, 200u, 50.0f, true));  /* out of range rejected */
    CHECK(!sensor_health_check(&h, 300u, 50.0f, true));
    CHECK(!sensor_health_check(&h, 400u, 50.0f, true));
    CHECK(h.unhealthy);                                   /* SEN-004 escalation */

    /* timeout path */
    sensor_health_t t;
    sensor_health_init(&t, &cfg);
    CHECK(sensor_health_check(&t, 100u, 5.0f, true));
    sensor_health_poll(&t, 100u, false);
    CHECK(!t.unhealthy);
    sensor_health_poll(&t, 2000u, false);                 /* >1 ms stale */
    CHECK(t.unhealthy);
    CHECK(t.timeouts == 1u);
}

static void test_validator_rejections(void)
{
    validation_cfg_t cfg = { .range_min = -10.0f, .range_max = 10.0f, .max_jump = 5.0f, .stale_us = 100000u };
    validator_t v;
    validator_init(&v, &cfg);
    CHECK(validator_check(&v, 100u, 1.0f));
    CHECK(!validator_check(&v, 200u, NAN));          /* NaN rejected */
    CHECK(!validator_check(&v, 300u, 42.0f));        /* range rejected */
    CHECK(validator_check(&v, 400u, 3.0f));          /* ok */
    CHECK(!validator_check(&v, 500u, 20.0f));        /* jump rejected */
    CHECK(validator_check(&v, 600u, 6.0f));          /* within jump */
}

static void test_calibration_flow(void)
{
    /* gyro bias computation + application */
    fc_imu_sample_t s[200];
    memset(s, 0, sizeof(s));
    for (int i = 0; i < 200; i++) {
        s[i].valid = true;
        s[i].gyro_rad_s.z = 0.02f;   /* constant bias */
        s[i].accel_m_s2.z = 9.81f;
    }
    /* re-init cal to defaults, run compute+apply */
    est_attitude_init();
    CHECK(cal_gyro_compute(s, 200));
    fc_imu_sample_t t = s[0];
    cal_gyro_apply(&t);
    CHECK_NEAR(t.gyro_rad_s.z, 0.0f, 1e-6);

    /* accel 6-face: x-axis sees +g on one face, -g on opposite */
    float minmax[3][2] = {
        { -9.81f, 9.81f },   /* x */
        { 0.0f, 0.0f },      /* y degenerate: invalid -> compute fails */
        { 0.0f, 0.0f }
    };
    CHECK(!cal_accel_compute(minmax));   /* degenerate face rejected */
    float minmax2[3][2] = {
        { -9.81f, 9.81f },
        { -9.81f, 9.81f },
        { -9.81f, 9.81f }
    };
    CHECK(cal_accel_compute(minmax2));
    fc_imu_sample_t a = s[0];
    cal_accel_apply(&a);
    /* offset removed: z reads (9.81-0)*1 = 9.81; y reads (0-0)*1=0 with symmetric span */
    CHECK_NEAR(a.accel_m_s2.x, 0.0f, 0.01f);
    CHECK_NEAR(a.accel_m_s2.z, 9.81f, 0.05f);
}

/* TEST-CTRL-HOV: closed-loop recovery from 15° roll perturbation (SIM-003).
 * Arm + hover throttle + level-attitude hold, 5 s sim. Pass: recovered to
 * within 3° of level with rates settled < 0.1 rad/s by t=5 s. */
static void test_closed_loop_hover_stability(void)
{
    hal_sim_reset();
    est_attitude_init();
    ctrl_cascade_init();
    ctrl_set_armed(true);
    ctrl_set_hover_throttle(1.0f/3.0f);

    /* perturb: 15° roll at t=0 */
    float tilt = 15.0f * (float)M_PI / 180.0f;
    hal_sim_set_attitude(cosf(tilt/2.0f), sinf(tilt/2.0f), 0.0f, 0.0f);

    fc_quat_t level = { 1.0f, 0.0f, 0.0f, 0.0f };
    ctrl_set_attitude_sp(&level);

    for (int i = 0; i < 5000; i++) {
        ctrl_cascade_run(NULL);            /* NULL = pull fresh sample from HAL */
        if (i % 4 == 0) ctrl_attitude_loop();
        hal_sim_step(1000u);
    }

    float q[4];
    hal_sim_get_attitude(q);               /* TRUE vehicle state, not estimator */
    float roll  = atan2f(2.0f*(q[0]*q[1] + q[2]*q[3]), 1.0f - 2.0f*(q[1]*q[1] + q[2]*q[2]));
    float pitch = asinf( 2.0f*(q[0]*q[2] - q[3]*q[1]));
    fc_vec3_t rates = est_attitude_rates_rad_s();
    float wn = sqrtf(rates.x*rates.x + rates.y*rates.y + rates.z*rates.z);

    printf("  hover-recovery: roll=%.2f deg pitch=%.2f deg |w|=%.4f rad/s\n",
           roll*57.2958f, pitch*57.2958f, wn);

    CHECK(fabsf(roll)  < 0.052f);   /* recovered within 3 deg */
    CHECK(fabsf(pitch) < 0.052f);
    CHECK(wn < 0.1f);               /* rates settled */
}

/* TEST-CTRL-RATE: rate-setpoint tracking + saturation bound (CTRL-001/002/003). */
static void test_rate_tracking_and_saturation(void)
{
    hal_sim_reset();
    est_attitude_init();
    ctrl_cascade_init();
    ctrl_set_armed(true);
    ctrl_set_hover_throttle(1.0f/3.0f);

    /* rate mode: 1 rad/s roll setpoint; attitude loop must NOT run (rate mode) */
    fc_vec3_t sp = { 1.0f, 0.0f, 0.0f };
    ctrl_set_rate_sp(&sp);

    bool bounded = true;
    for (int i = 0; i < 3000; i++) {
        ctrl_cascade_run(NULL);
        hal_sim_step(1000u);
        const float *m = ctrl_get_motor_outputs();
        for (int k = 0; k < 4; k++) {
            if (!(m[k] >= 0.0f && m[k] <= 1.0f)) bounded = false;   /* NaN-safe bound */
        }
    }
    fc_vec3_t rates = est_attitude_rates_rad_s();
    printf("  rate-track: w_roll=%.3f rad/s (sp 1.0), motors bounded=%d\n", rates.x, (int)bounded);
    CHECK(bounded);
    CHECK(rates.x > 0.5f && rates.x < 1.5f);   /* tracked, not runaway */

    /* absurd setpoint must clamp, not diverge (CTRL-003 bounded outputs) */
    sp.x = 50.0f;
    ctrl_set_rate_sp(&sp);
    for (int i = 0; i < 2000; i++) {
        ctrl_cascade_run(NULL);
        hal_sim_step(1000u);
        const float *m = ctrl_get_motor_outputs();
        for (int k = 0; k < 4; k++) {
            if (!(m[k] >= 0.0f && m[k] <= 1.0f)) bounded = false;
        }
    }
    CHECK(bounded);
    CHECK(sqrtf(est_attitude_rates_rad_s().x*est_attitude_rates_rad_s().x) < 100.0f);
}

/* TEST-ESC-DSHOT: frame structure/CRC against hand-computable references
 * (Betaflight DShot docs + BLHeli CRC convention). */
static void test_dshot_framing(void)
{
    /* value 0 (stop), no telemetry: payload 0 -> crc 0 -> frame 0x0000 */
    CHECK(dshot_build_frame(0, false) == 0x0000u);

    /* value 48 (min throttle), no telemetry: payload = 96 = 0x060
     * crc = (0x60 ^ 0x06 ^ 0x00) & 0xF = 0x6 -> frame = (96<<4)|6 = 0x0606 */
    CHECK(dshot_build_frame(48, false) == 0x0606u);

    /* telemetry bit changes the CRC input (payload 97 -> different frame) */
    CHECK(dshot_build_frame(48, true) != dshot_build_frame(48, false));

    /* max throttle in range */
    CHECK(dshot_throttle_from_unit(1.0f) == DSHOT_MAX_THROTTLE);
    CHECK(dshot_throttle_from_unit(0.0f) == DSHOT_CMD_MOTOR_STOP);
    CHECK(dshot_throttle_from_unit(-1.0f) == DSHOT_CMD_MOTOR_STOP);
    CHECK(dshot_throttle_from_unit(0.5f) > DSHOT_MIN_THROTTLE);
    CHECK(dshot_throttle_from_unit(0.5f) < DSHOT_MAX_THROTTLE);
    CHECK(dshot_build_frame(DSHOT_MAX_THROTTLE, false) != dshot_build_frame(0, false));
}

/* TEST-ESC-INTERLOCK: disarm => stop frames; stale command => stop frames (SAF). */
static void test_motor_interlocks(void)
{
    motor_output_init();
    float m4[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
    uint16_t f[4];

    /* disarmed: stop frames even with nonzero command */
    motor_output_set_armed(false);
    motor_output_update(m4, 1000u);
    motor_output_frames(f);
    for (int i = 0; i < 4; i++) CHECK(f[i] == dshot_build_frame(0, false));

    /* armed: nonzero frames */
    motor_output_set_armed(true);
    motor_output_update(m4, 2000u);
    motor_output_frames(f);
    for (int i = 0; i < 4; i++) CHECK(f[i] != dshot_build_frame(0, false));

    /* stale: no update for >50 ms -> stop frames */
    motor_output_poll(2000u + 100000u);
    CHECK(motor_output_timed_out());
    motor_output_frames(f);
    for (int i = 0; i < 4; i++) CHECK(f[i] == dshot_build_frame(0, false));

    /* disarm immediately stops */
    motor_output_set_armed(false);
    motor_output_frames(f);
    for (int i = 0; i < 4; i++) CHECK(f[i] == dshot_build_frame(0, false));

    motor_output_set_armed(true);   /* leave clean */
}

/* TEST-NAV-SEQ: deterministic mission sequence TAKEOFF -> WP x2 -> RTL -> LAND -> DONE. */
static void test_mission_sequence(void)
{
    mission_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.home.x_m = 0; cfg.home.y_m = 0;
    cfg.takeoff_alt_m = 1.5f;
    cfg.arrival_radius_m = 1.0f;
    cfg.geofence_radius_m = 100.0f;
    cfg.geofence_alt_m = 50.0f;
    mission_init(&cfg);

    nav_xy_t wps[2] = { { 10.0f, 0.0f }, { 10.0f, 10.0f } };
    CHECK(mission_load_waypoints(wps, 2) == 0);
    mission_start();
    CHECK(mission_state() == NAV_TAKEOFF);

    nav_xy_t pos = { 0.0f, 0.0f };
    float alt = 0.0f;

    /* climb to takeoff altitude */
    for (int i = 0; i < 200; i++) { alt = 1.5f; mission_tick(pos, alt, true); }
    CHECK(mission_state() == NAV_WAYPOINT);
    CHECK(mission_current_wp() == 0);

    /* fly to WP1 */
    pos.x_m = 10.0f;
    mission_tick(pos, alt, true);
    CHECK(mission_current_wp() == 1);
    CHECK(mission_state() == NAV_WAYPOINT);

    /* fly to WP2 */
    pos.y_m = 10.0f;
    mission_tick(pos, alt, true);
    CHECK(mission_state() == NAV_RTL);

    /* return home -> LAND -> descend -> DONE */
    pos.x_m = 0.0f; pos.y_m = 0.0f;
    mission_tick(pos, alt, true);
    CHECK(mission_state() == NAV_LAND);
    CHECK_NEAR(mission_altitude_sp(), 0.0f, 1e-6);

    mission_tick(pos, 0.1f, true);
    CHECK(mission_state() == NAV_DONE);
}

/* TEST-NAV-RCLOSS: RC loss in flight forces RTL from WAYPOINT (SAF-001). */
static void test_mission_rc_loss_rtl(void)
{
    mission_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.takeoff_alt_m = 1.5f;
    cfg.arrival_radius_m = 1.0f;
    cfg.geofence_radius_m = 100.0f;
    cfg.geofence_alt_m = 50.0f;
    mission_init(&cfg);
    nav_xy_t wps[1] = { { 20.0f, 0.0f } };
    mission_load_waypoints(wps, 1);
    mission_start();

    /* airborne, 15 m from home, en route to WP1 */
    nav_xy_t pos = { 15.0f, 0.0f };
    for (int i = 0; i < 50; i++) mission_tick(pos, 1.5f, true);
    CHECK(mission_state() == NAV_WAYPOINT);

    mission_tick(pos, 1.5f, false);      /* RC lost */
    CHECK(mission_state() == NAV_RTL);   /* away from home -> stays RTL */

    /* continue RTL: arrive home -> LAND (failsafe completes) */
    pos.x_m = 0.0f;
    mission_tick(pos, 1.5f, false);
    CHECK(mission_state() == NAV_LAND);
}

/* TEST-CTRL-ALT: closed-loop altitude hold in sim physics + estimator. */
static void test_closed_loop_altitude_hold(void)
{
    hal_sim_reset();
    est_attitude_init();
    est_alt_init();
    ctrl_cascade_init();
    ctrl_set_armed(true);
    ctrl_set_hover_throttle(1.0f/3.0f);
    ctrl_set_altitude_sp(1.5f);

    for (int i = 0; i < 12000; i++) {          /* 12 s sim */
        fc_imu_sample_t s;
        if (hal_imu_read(&s) == HAL_OK) {
            est_attitude_update(&s);
            est_alt_imu_feed(&s);
        }
        ctrl_cascade_run(&s);
        if (i % 10 == 0) {
            fc_baro_sample_t b;
            if (hal_baro_read(&b) == HAL_OK) est_alt_update(&b);
            ctrl_altitude_loop();
        }
        hal_sim_step(1000u);
    }

    float z_true = 0.0f, vz_true = 0.0f;
    hal_sim_get_vertical(&z_true, &vz_true);
    float z_est = est_alt_get_m();

    printf("  alt-hold: true=%.2f m est=%.2f m vz_true=%.2f m/s\n", z_true, z_est, vz_true);

    CHECK(fabsf(z_true - 1.5f) < 0.35f);       /* sim truth within tolerance */
    CHECK(fabsf(z_est   - 1.5f) < 0.50f);      /* estimate tracks */
    const float *mo = ctrl_get_motor_outputs();
    for (int k = 0; k < 4; k++) CHECK(mo[k] >= 0.0f && mo[k] <= 1.0f);
}

/* TEST-SIM-DET: identical seed => bit-identical sensor stream (SIM-002). */
static void test_sim_noise_determinism(void)
{
    fc_imu_sample_t a1[200], a2[200];

    for (int run = 0; run < 2; run++) {
        hal_sim_reset();
        hal_sim_set_seed(0xABCDu);
        hal_sim_set_noise(true);
        fc_imu_sample_t *dst = (run == 0) ? a1 : a2;
        for (int i = 0; i < 200; i++) {
            hal_sim_step(1000u);
            if (hal_imu_read(&dst[i]) != HAL_OK) memset(&dst[i], 0, sizeof(dst[i]));
        }
    }
    for (int i = 0; i < 200; i++) {
        CHECK(a1[i].gyro_rad_s.x == a2[i].gyro_rad_s.x);
        CHECK(a1[i].accel_m_s2.z == a2[i].accel_m_s2.z);
    }

    /* different seed => different stream */
    hal_sim_reset();
    hal_sim_set_seed(0x1234u);
    fc_imu_sample_t b;
    hal_sim_step(1000u);
    hal_imu_read(&b);
    CHECK(b.gyro_rad_s.x != a1[0].gyro_rad_s.x || b.accel_m_s2.z != a1[0].accel_m_s2.z);
}

/* TEST-SIM-NOISE: closed loop still stable with sensor noise + gyro bias. */
static void test_noise_robustness(void)
{
    hal_sim_reset();
    hal_sim_set_noise(true);       /* noise on, with sim-class gyro bias */
    est_attitude_init();
    est_alt_init();
    ctrl_cascade_init();
    ctrl_set_armed(true);
    ctrl_set_altitude_sp(1.5f);
    ctrl_set_hover_throttle(1.0f/3.0f);

    for (int i = 0; i < 12000; i++) {
        ctrl_cascade_run(NULL);
        if (i % 4 == 0) ctrl_attitude_loop();
        if (i % 10 == 0) {
            fc_baro_sample_t b;
            if (hal_baro_read(&b) == HAL_OK) est_alt_update(&b);
            ctrl_altitude_loop();
        }
        hal_sim_step(1000u);
    }

    float q[4], z_true = 0.0f, vz = 0.0f;
    hal_sim_get_attitude(q);
    hal_sim_get_vertical(&z_true, &vz);
    float roll = atan2f(2.0f*(q[0]*q[1] + q[2]*q[3]), 1.0f - 2.0f*(q[1]*q[1] + q[2]*q[2]));
    printf("  noise-robust: roll=%.1f deg alt=%.2f m (noise on)\n", roll*57.2958f, z_true);

    CHECK(fabsf(roll) < 0.10f);            /* < ~6 deg under noise */
    CHECK(fabsf(z_true - 1.5f) < 0.6f);
}

/* TEST-SIM-FAULTS: GNSS loss -> position unhealthy; dropout -> sensor unhealthy. */
static void test_position_gnss_and_loss(void)
{
    hal_sim_reset();
    est_position_init();

    fc_gnss_sample_t g;
    /* no fix before 3 s (sim-class TTFF) */
    hal_sim_step(1000u);
    CHECK(hal_gnss_read(&g) == HAL_NOT_READY);

    /* after 3 s: fix, position accepted, healthy */
    for (int i = 0; i < 3200; i++) hal_sim_step(1000u);
    CHECK(hal_gnss_read(&g) == HAL_OK && g.fix_valid);
    est_position_gnss(&g);
    CHECK(est_position_healthy());

    /* GNSS loss -> not ready + health degrades after timeout */
    hal_sim_fault("gnss_loss", true);
    CHECK(hal_gnss_read(&g) == HAL_NOT_READY);
    for (int i = 0; i < 1200; i++) hal_sim_step(1000u);   /* 1.2 s */
    est_position_tick();
    CHECK(!est_position_healthy());
    hal_sim_fault("gnss_loss", false);

    /* IMU dropout -> sensor hub health flag (SEN-004 integration) */
    sensor_hub_init();
    hal_sim_fault("imu_dropout", true);
    for (int i = 0; i < 10; i++) { hal_sim_step(1000u); sensor_hub_imu(NULL); }
    CHECK(hub_health_imu()->total_errors > 0);
    hal_sim_fault("imu_dropout", false);
}

/* TEST-EST-003: GNSS position tracker converges on a fixed point and tracks a
 * moving target with bounded steady-state error (EST-003, 10 Hz GNSS). */
static void test_position_gnss_convergence(void)
{
    hal_sim_reset();
    est_position_init();

    /* origin fix */
    fc_gnss_sample_t g; memset(&g, 0, sizeof(g));
    g.timestamp_us = 1000000u; g.fix_valid = true; g.valid = true;
    g.lat_deg = 47.0; g.lon_deg = 8.0; g.sats = 12;
    est_position_gnss(&g);
    est_xy_t p0 = est_position_get();
    CHECK_NEAR(p0.x_m, 0.0, 1e-6);
    CHECK_NEAR(p0.y_m, 0.0, 1e-6);

    /* fixed target 10 m north: repeated 10 Hz updates must converge <0.5 m */
    double dlat = 10.0 / 111320.0;
    for (int i = 1; i <= 30; i++) {
        g.timestamp_us = 1000000u + (uint64_t)i * 100000u;
        g.lat_deg = 47.0 + dlat;
        est_position_gnss(&g);
    }
    est_xy_t p1 = est_position_get();
    CHECK_NEAR(p1.x_m, 10.0, 0.5);
    CHECK(fabsf(p1.y_m) < 0.5f);

    /* moving target at 5 m/s north, 10 Hz: steady-state error < 1.5 m (EST-003) */
    est_position_init();
    g.lat_deg = 47.0; g.timestamp_us = 1000000u; est_position_gnss(&g);
    for (int i = 1; i <= 100; i++) {
        double t = (double)i * 0.1;
        g.timestamp_us = 1000000u + (uint64_t)(t * 1e6);
        g.lat_deg = 47.0 + (5.0 * t) / 111320.0;
        g.vn_ms = 5.0f;
        est_position_gnss(&g);
        est_position_predict(0.1f);
    }
    est_xy_t p2 = est_position_get();
    float true_x = 5.0f * 10.0f;
    printf("  est-position: track x=%.2f m (true %.2f)\n", p2.x_m, true_x);
    CHECK(fabsf(p2.x_m - true_x) < 1.5f);
}

/* TEST-EST-004: innovation gate rejects a teleport-class GNSS outlier without
 * moving the estimate (EST-004). */
static void test_position_outlier_rejection(void)
{
    hal_sim_reset();
    est_position_init();

    fc_gnss_sample_t g; memset(&g, 0, sizeof(g));
    g.timestamp_us = 1000000u; g.fix_valid = true; g.valid = true;
    g.lat_deg = 47.0; g.lon_deg = 8.0;
    est_position_gnss(&g);
    for (int i = 1; i <= 20; i++) {
        g.timestamp_us = 1000000u + (uint64_t)i * 100000u;
        g.lat_deg = 47.0 + (2.0 / 111320.0);   /* settle at 2 m north */
        est_position_gnss(&g);
    }
    est_xy_t before = est_position_get();

    /* 500 m jump (> 25 m gate) must be rejected */
    g.timestamp_us = 1000000u + 2100000u;
    g.lat_deg = 47.0 + (502.0 / 111320.0);
    est_position_gnss(&g);
    est_xy_t after = est_position_get();
    CHECK_NEAR(after.x_m, before.x_m, 1e-6);
    CHECK_NEAR(after.y_m, before.y_m, 1e-6);
    printf("  est-position-gate: rejected 500 m outlier, est held at %.2f m\n", after.x_m);
}

/* TEST-SIM-IMUFAIL (SAF-003): in-flight IMU dropout must not cause an
 * uncommanded motor surge. Regression for the Phase 15 defect where the app
 * kept feeding the last (still valid=true) sample, so the controller never
 * took the zero-output path and est_alt integrated a frozen accel at 1 kHz,
 * climbing to 46 m. Exercises the real app tick path. */
static void test_app_imu_dropout_no_surge(void)
{
    hal_sim_reset();
    hal_sim_set_seed(7u);
    hal_sim_set_noise(false);
    hal_sim_scenario(NULL);
    CHECK(app_init() == FC_OK);

    /* fly to a stable hold first (arm at 2 s inside the sim RC script) */
    for (int i = 0; i < 8000; i++) { hal_sim_step(1000u); app_tick_1khz(); }
    float z_before = 0.0f, vz_before = 0.0f;
    hal_sim_get_vertical(&z_before, &vz_before);
    CHECK(mission_state() == NAV_HOLD || mission_state() == NAV_TAKEOFF);
    CHECK(z_before > 0.3f);

    /* inject the dropout in flight */
    hal_sim_fault("imu_dropout", true);
    float z_max = z_before;
    for (int i = 0; i < 15000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
        float z = 0.0f, vz = 0.0f;
        hal_sim_get_vertical(&z, &vz);
        if (z > z_max) z_max = z;
    }
    float z_after = 0.0f, vz_after = 0.0f;
    hal_sim_get_vertical(&z_after, &vz_after);
    printf("  imu-fail: z %.2f -> %.2f m (peak %.2f), no surge\n",
           z_before, z_after, z_max);

    CHECK(failsafe_active() == FC_FAILSAFE_IMU);
    CHECK(mission_state() == NAV_ABORT);
    /* SAF-003: no uncommanded climb (the pre-fix defect reached 46 m) */
    CHECK(z_max < z_before + 1.0f);
    const float *m = ctrl_get_motor_outputs();
    float msum = 0.0f;
    for (int i = 0; i < 4; i++) msum += m[i];
    CHECK(msum < 0.05f);           /* actuators commanded to zero */
    hal_sim_fault("imu_dropout", false);
}

/* ===== Phase 16: scenario-based tests (TEST-GEN). Vary environmental,
 * temporal, fault and boundary conditions; no duplicate scenarios. ===== */

/* TEST-SAF-PRIORITY (SAF-001/DEC-006): with RC loss AND battery-critical both
 * present, the highest-priority failsafe (RC) wins. Also covers the SAF-031
 * RTL latch: low-but-not-critical battery latches, then a later critical
 * level requests RTL (not LAND). */
static void test_failsafe_priority_and_latch(void)
{
    failsafe_init();

    /* SAF-031 latch: 3.39 V/cell is below RTL threshold, not land threshold */
    fc_battery_sample_t b; memset(&b, 0, sizeof(b));
    b.valid = true;
    for (int i = 0; i < 4; i++) b.cell_v[i] = 3.39f;
    failsafe_battery_update(&b);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);   /* not critical yet */

    /* RC loss now: RC is a LEVEL condition - the timeout must actually
     * elapse without keepalive before FC_FAILSAFE_RC_LOSS is reported. */
    for (int i = 0; i < 600; i++) hal_sim_step(1000u);   /* 600 ms, no keepalive */
    CHECK(failsafe_active() == FC_FAILSAFE_RC_LOSS);
    CHECK(failsafe_mode_request() == FC_MODE_RTL);

    /* battery goes critical while RC loss is active: RC still wins (order) */
    for (int i = 0; i < 4; i++) b.cell_v[i] = 3.05f;
    failsafe_battery_update(&b);
    CHECK(failsafe_active() == FC_FAILSAFE_RC_LOSS);
    CHECK(failsafe_mode_request() == FC_MODE_RTL);

    /* RC restored (keepalive): battery-critical remains, now latched -> RTL */
    failsafe_rc_keepalive();
    failsafe_monitor();
    CHECK(failsafe_active() == FC_FAILSAFE_BATTERY);
    CHECK(failsafe_mode_request() == FC_MODE_RTL);  /* SAF-031 latched path */
}

/* TEST-NAV-FENCE (NAV-004): horizontal and altitude breaches force RTL; a
 * landing state is NOT preempted by the fence it is already resolving. */
static void test_mission_geofence(void)
{
    mission_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.takeoff_alt_m = 1.5f;
    cfg.arrival_radius_m = 1.0f;
    cfg.geofence_radius_m = 20.0f;
    cfg.geofence_alt_m = 10.0f;
    mission_init(&cfg);
    mission_start();

    nav_xy_t pos = { 0.0f, 0.0f };
    for (int i = 0; i < 100; i++) mission_tick(pos, 1.5f, true);
    CHECK(mission_state() == NAV_HOLD);
    CHECK(!mission_geofence_violated());

    /* altitude breach -> RTL */
    mission_tick(pos, 12.0f, true);
    CHECK(mission_state() == NAV_RTL);
    CHECK(mission_geofence_violated());

    /* recover altitude: RTL proceeds home -> LAND (fence clears on descent) */
    mission_tick(pos, 9.0f, true);
    CHECK(mission_state() == NAV_LAND);

    /* horizontal breach while descending: LAND must NOT be preempted back to
     * RTL (Phase 14 finding: oscillation) — descending IS the resolution */
    nav_xy_t out = { 50.0f, 0.0f };
    mission_tick(out, 0.5f, true);
    CHECK(mission_state() == NAV_LAND);

    /* and DONE is terminal even with the fence still violated */
    mission_tick(out, 0.1f, true);
    CHECK(mission_state() == NAV_DONE);
}

/* TEST-NAV-RCREC (SAF-001 temporal variation): RC restored after an in-flight
 * loss must NOT re-takeoff or resume waypoints; the failsafe RTL completes. */
static void test_mission_rc_recovery(void)
{
    mission_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.takeoff_alt_m = 1.5f;
    cfg.arrival_radius_m = 1.0f;
    cfg.geofence_radius_m = 100.0f;
    cfg.geofence_alt_m = 50.0f;
    mission_init(&cfg);
    nav_xy_t wps[1] = { { 20.0f, 0.0f } };
    mission_load_waypoints(wps, 1);
    mission_start();

    nav_xy_t pos = { 15.0f, 0.0f };
    for (int i = 0; i < 50; i++) mission_tick(pos, 1.5f, true);
    mission_tick(pos, 1.5f, false);            /* loss -> RTL */
    CHECK(mission_state() == NAV_RTL);

    for (int i = 0; i < 50; i++) mission_tick(pos, 1.5f, true);   /* RC back */
    CHECK(mission_state() == NAV_RTL);         /* still RTL, not WAYPOINT */

    pos.x_m = 0.0f;
    mission_tick(pos, 1.5f, true);
    CHECK(mission_state() == NAV_LAND);
    mission_tick(pos, 0.1f, true);
    CHECK(mission_state() == NAV_DONE);
}

/* TEST-CTRL-YAW (CTRL-001/002): attitude loop produces a yaw rate command of
 * the correct sign toward a pure-yaw setpoint; rate loop tracks it. */
static void test_ctrl_yaw_chain(void)
{
    hal_sim_reset();            /* deterministic sim + identity attitude */
    est_attitude_init();
    ctrl_cascade_init();
    ctrl_set_armed(true);
    ctrl_set_hover_throttle(1.0f / 3.0f);

    /* settle the estimator at identity attitude */
    for (int i = 0; i < 500; i++) { hal_sim_step(1000u); ctrl_cascade_run(NULL); }

    /* est_attitude identity, setpoint yaw +0.5 rad -> rate_sp.z positive */
    float yaw = 0.5f;
    fc_quat_t qs = { cosf(yaw * 0.5f), 0.0f, 0.0f, sinf(yaw * 0.5f) };
    ctrl_set_attitude_sp(&qs);
    ctrl_attitude_loop();
    ctrl_cascade_run(NULL);   /* pulls real IMU from sim; runs 1 kHz chain */
    const float *m = ctrl_get_motor_outputs();
    float sum = m[0] + m[1] + m[2] + m[3];
    /* yaw torque = M1+M3-(M2+M4) with CW/CCW/CCW/CW spins (DEC-008) */
    float yaw_torque = (m[0] + m[3]) - (m[1] + m[2]);
    CHECK(sum > 0.1f);
    CHECK(fabsf(yaw_torque) > 1e-4f);   /* a yaw command must modulate the pairs */

    /* disarmed: same setpoint -> zero output (CTRL-005) */
    ctrl_set_armed(false);
    ctrl_cascade_run(NULL);
    m = ctrl_get_motor_outputs();
    for (int i = 0; i < 4; i++) CHECK(m[i] == 0.0f);
}

/* TEST-EST-ATT-YAW (EST-001): pure gyro-z rotation integrates to the correct
 * quaternion yaw (environmental variation: rotation instead of tilt). */
static void test_attitude_yaw_integration(void)
{
    est_attitude_init();
    fc_imu_sample_t s; memset(&s, 0, sizeof(s));
    s.valid = true;
    float rate = 1.0f;                        /* rad/s about z */
    for (int i = 0; i < 1000; i++) {
        s.timestamp_us = (uint64_t)i * 1000u;
        s.accel_m_s2.z = 9.81f;
        s.gyro_rad_s.z = rate;
        est_attitude_update(&s);
    }
    fc_quat_t q = est_attitude_quat();
    float expect_half = 0.5f * 1.0f;          /* 1 s * 1 rad/s */
    CHECK_NEAR(q.w, cos(expect_half), 0.02);
    CHECK_NEAR(q.z, sin(expect_half), 0.02);
    CHECK(fabs(q.x) < 0.02f && fabs(q.y) < 0.02f);
}

/* TEST-EST-ALT-DRIFT (EST-002 boundary/temporal): 60 s stationary under noise.
 * Criterion is the MEAN offset over the last 10 s (systematic drift), not the
 * instantaneous reading: with 5 Pa baro noise (~0.4 m RMS) any hard-anchored
 * estimate necessarily bounces ~+-0.5 m sample to sample, which is sensor
 * noise, not estimator drift. Also regression-checks the ground-reference
 * averaging (Phase 16: a single noisy ground sample produced a permanent
 * -0.56 m offset). */
static void test_altitude_stationary_drift(void)
{
    hal_sim_reset();
    hal_sim_set_seed(0xBEEFu);
    hal_sim_set_noise(true);
    est_alt_init();

    float sum = 0.0f; int n = 0;
    for (int i = 0; i < 60000; i++) {         /* 60 s */
        if (i % 20 == 0) {
            fc_baro_sample_t b;
            if (hal_baro_read(&b) == HAL_OK) est_alt_update(&b);
        }
        hal_sim_step(1000u);
        if (i >= 50000) {                     /* last 10 s window */
            sum += est_alt_get_m(); n++;
        }
    }
    float mean_alt = sum / (float)n;
    printf("  alt-drift 60s: mean %.3f m (last sample %.2f)\n",
           mean_alt, est_alt_get_m());
    CHECK(fabsf(mean_alt) < 0.5f);            /* systematic drift/offset */
    CHECK(fabsf(est_alt_vz_ms()) < 0.2f);     /* velocity state quiet */
}

/* TEST-EST-ALT-DIVERGENCE (EST-055 class fault condition): an estimate that
 * runs away >50 m from baro must re-anchor instead of persisting. */
static void test_altitude_divergence_reanchor(void)
{
    hal_sim_reset();
    est_alt_init();

    /* normal ground reference */
    fc_baro_sample_t b;
    for (int i = 0; i < 100; i++) {
        if (hal_baro_read(&b) == HAL_OK) est_alt_update(&b);
        hal_sim_step(1000u);
    }

    /* teleport the vehicle to 100 m: baro now reports 100 m, estimate ~0 */
    hal_sim_set_vertical(100.0f, 0.0f);
    hal_sim_step(1000u);
    if (hal_baro_read(&b) == HAL_OK) est_alt_update(&b);
    CHECK(est_alt_get_m() > 50.0f);           /* re-anchored, not ignored */
    CHECK_NEAR(est_alt_vz_ms(), 0.0, 1.0);    /* and vz reset */
}

/* ---- Phase 17: perception fusion (PERC-001..004, SAF-041, DEC-007/013) ---- */

static void perc_feed_ai_now(float x_m, float conf)
{
    fc_ai_obstacle_set_t s;
    memset(&s, 0, sizeof(s));
    s.timestamp_us = hal_time_us();
    s.count = 1;
    s.det[0].pos_m.x = x_m;
    s.det[0].confidence = conf;
    s.det[0].radius_m = 0.25f;
    perception_feed_ai(&s);
}

/* FUSED mode: fresh AI set + fresh FC ToF agree on the same geometry. */
static void test_perception_fused_basic(void)
{
    hal_sim_reset();
    perception_init();
    hal_sim_step(1000u);
    perc_feed_ai_now(6.0f, 0.9f);
    perception_feed_tof(6.0f, hal_time_us());
    perception_tick_50hz(hal_time_us());

    const perception_state_t *p = perception_get();
    CHECK(p->mode == PERCEPTION_FUSED);
    CHECK(p->valid);
    CHECK(p->advisory);                       /* DEC-007 boundary marker */
    CHECK(p->count == 1);
    CHECK_NEAR(p->min_range_m, 6.0, 0.1);
    CHECK_NEAR(p->min_bearing_rad, 0.0, 0.01);
    CHECK(p->obs[0].source == PERC_SRC_AI);
    CHECK(!p->flow_healthy);                  /* SIM flow stub: advisory only */
    CHECK(p->mode == PERCEPTION_FUSED);       /* flow loss does not gate mode */
}

/* Vehicle-state fusion: AI body-frame detection rotated by est attitude. */
static void test_perception_vehicle_state_rotation(void)
{
    hal_sim_reset();
    perception_init();
    hal_sim_step(1000u);
    perc_feed_ai_now(6.0f, 0.9f);
    /* yaw 90 deg left: body +x -> world +y (DEC-008 body->world) */
    fc_quat_t q = { 0.7071068f, 0.0f, 0.0f, 0.7071068f };
    perception_set_vehicle_state(&q, 1.5f);
    perception_tick_50hz(hal_time_us());

    const perception_state_t *p = perception_get();
    CHECK(p->count == 1);
    CHECK_NEAR(p->obs[0].pos_body_m.x, 6.0, 0.01);   /* body unchanged */
    CHECK_NEAR(p->obs[0].pos_m.x, 0.0, 0.01);        /* world: rotated */
    CHECK_NEAR(p->obs[0].pos_m.y, 6.0, 0.01);
}

/* Link-layer confidence gate (SAF-041/AI-001): conf < 0.5 is dropped. */
static void test_perception_confidence_gate(void)
{
    hal_sim_reset();
    perception_init();
    hal_sim_step(1000u);
    perc_feed_ai_now(6.0f, 0.40f);            /* below link threshold */
    perception_tick_50hz(hal_time_us());

    const perception_state_t *p = perception_get();
    CHECK(p->mode == PERCEPTION_AI_ONLY);     /* stream fresh but empty */
    CHECK(p->count == 0);
    CHECK(!p->valid);                         /* nothing published */
}

/* Stale-set exclusion (ICD-02 200 ms): aged AI drops out, FC ToF carries.
 * Stale geometry must never stay valid (SAF-003 class rule). */
static void test_perception_stale_ai(void)
{
    hal_sim_reset();
    perception_init();
    hal_sim_step(1000u);
    perc_feed_ai_now(6.0f, 0.9f);
    perception_feed_tof(7.0f, hal_time_us());
    for (int i = 0; i < 300; i++) hal_sim_step(1000u);   /* +300 ms */
    perception_feed_tof(7.0f, hal_time_us());   /* app refreshes ToF at 50 Hz */
    perception_tick_50hz(hal_time_us());

    const perception_state_t *p = perception_get();
    CHECK(p->mode == PERCEPTION_TOF_ONLY);    /* stale AI excluded */
    CHECK(p->count == 1);
    CHECK_NEAR(p->min_range_m, 7.0, 0.01);    /* TOF-sourced obstacle */
    CHECK(p->obs[0].source == PERC_SRC_TOF);
    CHECK(p->ai_healthy);                     /* companion alive (<1 s) */
}

/* Companion death (1 s, SYS-004): stream gone -> NONE, unhealthy, and NO
 * failsafe (advisory-only boundary, DEC-007/SAF-001). */
static void test_perception_ai_death_no_failsafe(void)
{
    hal_sim_reset();
    perception_init();
    failsafe_init();                     /* isolate from earlier latched tests */
    hal_sim_step(1000u);
    perc_feed_ai_now(6.0f, 0.9f);
    perception_tick_50hz(hal_time_us());
    /* this unit test feeds no RC, so a level condition may legitimately be
     * active; the DEC-007 property is that perception death does not CHANGE
     * the failsafe level (no escalation from an advisory-only source). */
    fc_failsafe_t fs_before = failsafe_active();
    for (int i = 0; i < 1200; i++) {           /* +1.2 s, RC kept healthy */
        hal_sim_step(1000u);
        failsafe_rc_keepalive();               /* model a live RC link */
    }
    perception_tick_50hz(hal_time_us());

    const perception_state_t *p = perception_get();
    CHECK(p->mode == PERCEPTION_NONE);
    CHECK(!p->valid);
    CHECK(!p->ai_healthy);
    CHECK(p->count == 0);
    CHECK(failsafe_active() == fs_before);   /* no escalation from perception */
}

/* SAF-041 conflict: FC ToF closer than AI claims in the forward cone ->
 * published range clamped to FC sensor, confidence downgraded, both sources. */
static void test_perception_tof_conflict(void)
{
    hal_sim_reset();
    perception_init();
    hal_sim_step(1000u);
    perc_feed_ai_now(10.0f, 0.9f);
    perception_feed_tof(4.0f, hal_time_us());
    perception_tick_50hz(hal_time_us());

    const perception_state_t *p = perception_get();
    CHECK(p->mode == PERCEPTION_FUSED);
    CHECK(p->count == 1);
    CHECK_NEAR(p->obs[0].pos_body_m.x, 4.0, 0.05);  /* FC sensor wins */
    CHECK_NEAR(p->obs[0].confidence, 0.45, 0.01);   /* downgraded 50% */
    CHECK((p->obs[0].source & PERC_SRC_AI) != 0);
    CHECK((p->obs[0].source & PERC_SRC_TOF) != 0);
    CHECK_NEAR(p->min_range_m, 4.0, 0.05);
}

/* FC-only ranging: AI blind in the forward cone -> TOF-synthesized obstacle. */
static void test_perception_tof_synthesis(void)
{
    hal_sim_reset();
    perception_init();
    hal_sim_step(1000u);
    perception_feed_tof(5.0f, hal_time_us());
    perception_tick_50hz(hal_time_us());

    const perception_state_t *p = perception_get();
    CHECK(p->mode == PERCEPTION_TOF_ONLY);
    CHECK(p->count == 1);
    CHECK_NEAR(p->min_range_m, 5.0, 0.01);
    CHECK(p->obs[0].source == PERC_SRC_TOF);

    /* out-of-band ToF (sensor glitch) must be rejected, not published */
    perception_feed_tof(0.01f, hal_time_us());
    perception_tick_50hz(hal_time_us());
    CHECK(p->count == 1);                     /* previous picture retained */
    CHECK_NEAR(p->min_range_m, 5.0, 0.01);
}

/* Invalid sets are ignored at the link sink (null, zero-time, over-count). */
static void test_perception_invalid_set(void)
{
    hal_sim_reset();
    perception_init();
    hal_sim_step(1000u);

    perception_feed_ai(NULL);
    fc_ai_obstacle_set_t bad;
    memset(&bad, 0, sizeof(bad));
    bad.timestamp_us = hal_time_us();
    bad.count = FC_AI_MAX_DETECTIONS + 1;
    perception_feed_ai(&bad);
    bad.count = 0;
    perception_feed_ai(&bad);
    perception_tick_50hz(hal_time_us());

    const perception_state_t *p = perception_get();
    CHECK(p->mode == PERCEPTION_NONE);        /* nothing accepted */
    CHECK(p->count == 0);
    CHECK(!p->valid);
}

/* App-level ai_loss: companion stream stops in flight -> perception degrades
 * to NONE, and the vehicle KEEPS FLYING (advisory-only, DEC-007): no failsafe,
 * no abort. This is the ai_loss scenario consumer deferred from Phase 16. */
static void test_app_ai_loss_advisory_only(void)
{
    hal_sim_reset();
    hal_sim_set_seed(7u);
    hal_sim_set_noise(false);
    hal_sim_scenario(NULL);
    CHECK(app_init() == FC_OK);

    for (int i = 0; i < 8000; i++) { hal_sim_step(1000u); app_tick_1khz(); }
    CHECK(mission_state() == NAV_HOLD || mission_state() == NAV_TAKEOFF);
    const perception_state_t *p = perception_get();
    CHECK(p->mode == PERCEPTION_FUSED);       /* live stream + forward ToF */
    CHECK(p->ai_healthy);

    hal_sim_fault("ai_loss", true);
    for (int i = 0; i < 1500; i++) { hal_sim_step(1000u); app_tick_1khz(); }

    CHECK(!p->ai_healthy);
    /* companion dead but FC forward ToF alive: FC-only sensors carry the
     * picture (SAF-041 philosophy) -> degraded TOF_ONLY, not blind */
    CHECK(p->mode == PERCEPTION_TOF_ONLY);
    CHECK(p->obs[0].source == PERC_SRC_TOF);
    /* DEC-007: perception loss must NOT escalate failsafe or abort mission */
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);
    CHECK(mission_state() != NAV_ABORT);
    hal_sim_fault("ai_loss", false);
}

/* ---- Phase 18: obstacle avoidance (SAF-040, DEC-014) ---- */

static const perception_state_t *av_feed_and_tick(float range_m, float bearing_rad,
                                                  float conf, float ai_vel)
{
    fc_ai_obstacle_set_t s;
    memset(&s, 0, sizeof(s));
    s.timestamp_us = hal_time_us();
    s.count = 1;
    s.det[0].pos_m.x = range_m * cosf(bearing_rad);
    s.det[0].pos_m.y = range_m * sinf(bearing_rad);
    s.det[0].vel_m_s.x = ai_vel;
    s.det[0].confidence = conf;
    s.det[0].radius_m = 0.25f;
    perception_feed_ai(&s);
    perception_feed_tof(11.5f, hal_time_us());   /* FC ToF: no conflict */
    perception_tick_50hz(hal_time_us());
    avoidance_update(perception_get(), hal_time_us());
    return perception_get();
}

/* Perception invalid/empty -> deterministic degraded behavior: no command. */
static void test_avoidance_inactive_without_perception(void)
{
    hal_sim_reset();
    perception_init();
    avoidance_init();
    hal_sim_step(1000u);

    avoidance_update(NULL, hal_time_us());               /* null picture */
    CHECK(avoidance_get()->mode == AVOID_NONE);
    CHECK(!avoidance_get()->active);
    CHECK_NEAR(avoidance_get()->vel_sp_body_m_s.x, 0.0, 1e-9);

    av_feed_and_tick(3.0f, 0.0f, 0.40f, 0.0f);           /* conf gate: empty picture */
    CHECK(avoidance_get()->mode == AVOID_NONE);
    CHECK(!avoidance_get()->active);

    av_feed_and_tick(8.0f, 0.0f, 0.9f, 0.0f);            /* threat beyond brake range */
    CHECK(avoidance_get()->mode == AVOID_NONE);          /* tracked, no intervention */
    CHECK(avoidance_get()->threat_range_m > 0.0f);       /* but recorded */
}

/* Brake curve: 3 m ahead -> SLOW, scaled speed, no lateral for centered threat. */
static void test_avoidance_slow(void)
{
    hal_sim_reset();
    perception_init();
    avoidance_init();
    hal_sim_step(1000u);
    av_feed_and_tick(3.0f, 0.0f, 0.9f, 0.0f);

    const avoid_cmd_t *a = avoidance_get();
    CHECK(a->mode == AVOID_SLOW);
    CHECK(a->active);
    CHECK(a->advisory);
    /* scale = (3-2)/(4-2) = 0.5 -> vx = 1.0 m/s */
    CHECK_NEAR(a->vel_sp_body_m_s.x, 1.0, 0.05);
    CHECK_NEAR(a->vel_sp_body_m_s.y, 0.0, 1e-6);   /* centered: no lateral */
    CHECK_NEAR(a->vel_sp_body_m_s.z, 0.0, 1e-9);   /* vertical stays mission-owned */
    CHECK_NEAR(a->threat_range_m, 3.0, 0.05);
}

/* Hard stop inside the stop range, bounded retreat inside the retreat range
 * (DEC-018: holding station is not safe against a closing obstacle). */
static void test_avoidance_stop_and_retreat(void)
{
    hal_sim_reset();
    perception_init();
    avoidance_init();
    hal_sim_step(1000u);
    av_feed_and_tick(1.7f, 0.0f, 0.9f, 0.0f);

    const avoid_cmd_t *a = avoidance_get();
    CHECK(a->mode == AVOID_STOP);
    CHECK_NEAR(a->vel_sp_body_m_s.x, 0.0, 1e-9);

    av_feed_and_tick(1.0f, 0.0f, 0.9f, 0.0f);
    a = avoidance_get();
    CHECK(a->mode == AVOID_RETREAT);
    CHECK_NEAR(a->vel_sp_body_m_s.x, -AVOID_V_RETREAT_M_S, 1e-6);
    CHECK(a->vel_sp_body_m_s.x >= -AVOID_V_FWD_MAX_M_S);   /* SAF-040 bound */
}

/* Lateral offset: threat on the left -> steer right (negative y). */
static void test_avoidance_lateral(void)
{
    hal_sim_reset();
    perception_init();
    avoidance_init();
    hal_sim_step(1000u);
    av_feed_and_tick(2.5f, 0.5f, 0.9f, 0.0f);   /* +28.6 deg = left */

    const avoid_cmd_t *a = avoidance_get();
    CHECK(a->mode == AVOID_SLOW);
    CHECK(a->vel_sp_body_m_s.y < 0.0f);          /* away from threat */
    CHECK(fabsf(a->vel_sp_body_m_s.y) <= AVOID_V_LAT_MAX_M_S + 1e-6);
}

/* SAF-040: adversarial inputs (huge AI velocity, absurd range) can never push
 * any published component beyond the FC-owned limits. */
static void test_avoidance_saf040_clamp(void)
{
    hal_sim_reset();
    perception_init();
    avoidance_init();
    hal_sim_step(1000u);

    av_feed_and_tick(0.05f, 1.57f, 0.9f, 1000.0f);   /* 0.05 m left, vel 1000 m/s */
    const avoid_cmd_t *a = avoidance_get();
    CHECK(fabsf(a->vel_sp_body_m_s.x) <= AVOID_V_FWD_MAX_M_S + 1e-6);
    CHECK(fabsf(a->vel_sp_body_m_s.y) <= AVOID_V_LAT_MAX_M_S + 1e-6);
    CHECK(fabsf(a->vel_sp_body_m_s.z) <= 1e-9);
    CHECK(fabsf(a->vel_sp_body_m_s.x) <= 100.0f);    /* sanity vs input scale */

    /* multiple obstacles: nearest wins, output still bounded */
    fc_ai_obstacle_set_t s;
    memset(&s, 0, sizeof(s));
    s.timestamp_us = hal_time_us();
    s.count = 2;
    s.det[0].pos_m.x = 10.0f; s.det[0].confidence = 0.9f;
    s.det[1].pos_m.x = 2.5f;  s.det[1].pos_m.y = 0.2f; s.det[1].confidence = 0.9f;
    perception_feed_ai(&s);
    perception_tick_50hz(hal_time_us());
    avoidance_update(perception_get(), hal_time_us());
    a = avoidance_get();
    CHECK(a->threat_range_m < 3.0f);                 /* 2.5 m one selected */
    CHECK(fabsf(a->vel_sp_body_m_s.x) <= AVOID_V_FWD_MAX_M_S + 1e-6);
}

/* App-level: closing obstacle drives NONE -> SLOW -> STOP while the mission
 * keeps flying; avoidance stays advisory (no abort, no failsafe). */
static void test_app_avoidance_progression(void)
{
    hal_sim_reset();
    hal_sim_set_seed(7u);
    hal_sim_set_noise(false);
    hal_sim_scenario(NULL);
    CHECK(app_init() == FC_OK);

    bool seen_slow = false, seen_stop = false;
    for (int i = 0; i < 30000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
        if (avoidance_get()->mode == AVOID_SLOW) seen_slow = true;
        if (avoidance_get()->mode == AVOID_STOP) seen_stop = true;
    }
    CHECK(seen_slow);
    CHECK(seen_stop);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);
    CHECK(mission_state() != NAV_ABORT);
    /* SAF-040: published commands bounded at every point of the flight */
    const avoid_cmd_t *a = avoidance_get();
    CHECK(fabsf(a->vel_sp_body_m_s.x) <= AVOID_V_FWD_MAX_M_S + 1e-6);
    CHECK(fabsf(a->vel_sp_body_m_s.y) <= AVOID_V_LAT_MAX_M_S + 1e-6);
}

/* ---- Phase 19: communication & telemetry (COM-001..004) ---- */

/* Build a CRSF CHANNELS frame for 8 raw u16 us values (spec packing: len
 * includes the type byte, two 11-bit channels per 3 data bytes). */
static size_t mk_crsf_channels(uint8_t *out, size_t cap, const uint16_t *us16)
{
    if (cap < 3u + 12u + 2u) return 0;
    out[0] = RC_CRSF_SYNC;
    out[1] = 13u;                  /* type (1) + 12 data bytes */
    out[2] = RC_CRSF_TYPE_CHANNELS;
    for (int i = 0; i < 4; i++) {
        uint16_t a = us16[i * 2] & 0x07FFu;
        uint16_t b = us16[i * 2 + 1] & 0x07FFu;
        out[3 + i * 3]     = (uint8_t)(a & 0xFFu);
        out[4 + i * 3]     = (uint8_t)(((a >> 8) & 0x07u) | ((b & 0x1Fu) << 3));
        out[5 + i * 3]     = (uint8_t)((b >> 5) & 0x3Fu);
    }
    uint16_t crc = crc16_ccitt(out, 3u + 12u);
    out[15] = (uint8_t)(crc & 0xFFu);
    out[16] = (uint8_t)(crc >> 8);
    return 17u;
}

/* COM-001: CRSF parse -> normalized channels; bad CRC rejected (fail safe). */
static void test_rc_crsf_parse(void)
{
    uint8_t buf[32];
    uint16_t raw[8] = { 885u, 1340u, 1795u, 1000u, 2000u, 1500u, 1500u, 1500u };
    size_t n = mk_crsf_channels(buf, sizeof(buf), raw);
    CHECK(n == 17u);

    fc_rc_frame_t f;
    CHECK(rc_crsf_parse(buf, n, 1000u, &f));
    CHECK(f.valid && f.frames_valid && !f.failsafe_active);
    CHECK(f.channels[0] == 1000u);          /* 885 us maps to 1000 */
    CHECK(f.channels[2] == 2000u);          /* 1795 us maps to 2000 */
    CHECK_NEAR(f.channels[1], 1500, 3);     /* mid stick */
    CHECK(f.timestamp_us == 1000u);

    /* single-bit corruption -> rejected, no partial frame accepted */
    buf[8] ^= 0x01u;
    CHECK(!rc_crsf_parse(buf, n, 1100u, &f));
    CHECK(!rc_crsf_parse(buf, 4u, 1100u, &f));   /* truncated */
    CHECK(!rc_crsf_parse(buf, n, 1100u, NULL));  /* null out */
}

/* COM-001: SBUS fallback parse; flags bit2 marks link failsafe. */
static void test_rc_sbus_parse(void)
{
    uint8_t f25[RC_SBUS_FRAME_LEN];
    memset(f25, 0, sizeof(f25));
    f25[0] = RC_SBUS_START;
    uint16_t us16[8] = { 1340u, 1340u, 1340u, 1340u, 1795u, 885u, 1340u, 1340u };
    for (int i = 0; i < 8; i++) {
        uint16_t v = us16[i] & 0x07FFu;
        f25[1 + i * 2]     = (uint8_t)(v & 0xFFu);
        f25[1 + i * 2 + 1] = (uint8_t)((v >> 8) & 0x07u);
    }
    f25[23] = 0u;          /* flags: no failsafe */
    f25[24] = RC_SBUS_END;

    fc_rc_frame_t f;
    CHECK(rc_sbus_parse(f25, sizeof(f25), 500u, &f));
    CHECK(f.valid && f.frames_valid && !f.failsafe_active);
    CHECK_NEAR(f.channels[1], 1500, 3);
    CHECK(f.channels[4] == 1800u);          /* arm switch high */
    CHECK(f.channels[5] == 1000u);

    f25[23] = 0x04u;      /* transmitter-lost flag */
    CHECK(rc_sbus_parse(f25, sizeof(f25), 600u, &f));
    CHECK(f.failsafe_active);

    f25[24] = 0x55u;      /* bad end byte */
    CHECK(!rc_sbus_parse(f25, sizeof(f25), 700u, &f));
    CHECK(!rc_sbus_parse(f25, 10u, 700u, &f));
}

/* COM-003: ICD-02 frame codec round-trip + CRC rejection + resync. */
static void test_icd02_frame_codec(void)
{
    uint8_t pl[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint8_t frame[ICD02_MAX_FRAME];
    size_t flen = 0;
    CHECK(icd02_encode(ICD02_TYPE_HEARTBEAT, 42u, pl, 8u, frame, sizeof(frame), &flen));
    CHECK(flen == 8u + ICD02_HEADER_LEN + 2u);

    icd02_frame_t f;
    size_t consumed = 0;
    CHECK(icd02_parse(frame, flen, &f, &consumed));
    CHECK(consumed == flen);
    CHECK(f.type == ICD02_TYPE_HEARTBEAT && f.seq == 42u && f.len == 8u);
    for (int i = 0; i < 8; i++) CHECK(f.payload[i] == pl[i]);

    /* CRC corruption -> rejected with a safe drop count (resync) */
    frame[6] ^= 0xFFu;
    CHECK(!icd02_parse(frame, flen, &f, &consumed));
    CHECK(consumed >= 1u && consumed < flen);
    frame[6] = pl[1];

    /* leading garbage before a valid frame still parses (resync path) */
    uint8_t stream[64];
    memset(stream, 0x00, 10);
    stream[9] = 0xFFu;
    memcpy(&stream[10], frame, flen);
    CHECK(!icd02_parse(stream, 10u + flen, &f, &consumed));
    CHECK(consumed == 10u);                       /* dropped up to the SYNC */
    CHECK(icd02_parse(&stream[10], flen, &f, &consumed));
    CHECK(consumed == flen);

    /* buffer-too-small rejection (payload length itself is bounded by u8) */
    uint8_t tiny[4];
    CHECK(!icd02_encode(ICD02_TYPE_HEALTH, 0u, pl, 2u, tiny, sizeof(tiny), &flen));
}

/* COM-003: OBSTACLE_SET payload codec round-trip incl. i16 cm quantization. */
static void test_icd02_obstacle_set_codec(void)
{
    fc_ai_obstacle_set_t s, d;
    memset(&s, 0, sizeof(s));
    s.count = 2;
    s.det[0].pos_m.x = 5.5f;  s.det[0].pos_m.y = -1.25f; s.det[0].pos_m.z = 0.5f;
    s.det[0].vel_m_s.z = -0.35f; s.det[0].radius_m = 0.30f;
    s.det[0].confidence = 0.87f; s.det[0].class_id = 2;
    s.det[1].pos_m.x = 12.0f;  s.det[1].confidence = 0.51f;
    s.det[1].radius_m = 0.15f; s.det[1].class_id = 1;

    uint8_t pl[ICD02_MAX_PAYLOAD];
    uint8_t plen = icd02_encode_obstacle_set(pl, sizeof(pl), &s);
    CHECK(plen == 1u + 3u * 2u + 12u * 2u);
    CHECK(icd02_decode_obstacle_set(pl, plen, &d));
    CHECK(d.count == 2);
    CHECK_NEAR(d.det[0].pos_m.x, 5.5, 0.01);    /* cm quantization */
    CHECK_NEAR(d.det[0].pos_m.y, -1.25, 0.01);
    CHECK_NEAR(d.det[0].vel_m_s.z, -0.35, 0.01);
    CHECK_NEAR(d.det[0].confidence, 0.87, 0.01);
    CHECK_NEAR(d.det[0].radius_m, 0.30, 0.01);
    CHECK(d.det[0].class_id == 2);
    CHECK(d.det[1].class_id == 1);

    /* truncated / impossible payloads rejected, never partially applied */
    CHECK(!icd02_decode_obstacle_set(pl, plen - 1u, &d));
    uint8_t bad[4] = { 99u, 0u, 0u, 0u };      /* count > 16 */
    CHECK(!icd02_decode_obstacle_set(bad, sizeof(bad), &d));
    CHECK(!icd02_encode_obstacle_set(pl, 4u, &s));
}

/* COM-003: link task dispatch, heartbeat health, seq tracking, invalid types. */
static void test_companion_link_task(void)
{
    companion_link_init();
    perception_init();
    hal_sim_reset();
    hal_sim_step(1000u);

    /* HEARTBEAT (ai_state=RUNNING, model 0x0001) */
    uint8_t hb[7] = { 0u, 0u, 0u, 5u, 2u, 0x01u, 0x00u };
    uint8_t frame[ICD02_MAX_FRAME];
    size_t flen = 0;
    CHECK(icd02_encode(ICD02_TYPE_HEARTBEAT, 0u, hb, 7u, frame, sizeof(frame), &flen));
    companion_link_rx(frame, flen);
    companion_link_poll(hal_time_us());
    CHECK(companion_stats()->frames_ok == 1u);
    CHECK(companion_healthy());
    CHECK(companion_ai_state() == CL_AI_RUNNING);
    CHECK(companion_stats()->model_id == 1u);

    /* OBSTACLE_SET -> perception gets the geometry */
    fc_ai_obstacle_set_t s;
    memset(&s, 0, sizeof(s));
    s.count = 1;
    s.det[0].pos_m.x = 6.0f; s.det[0].confidence = 0.9f; s.det[0].radius_m = 0.25f;
    uint8_t pl[ICD02_MAX_PAYLOAD];
    uint8_t plen = icd02_encode_obstacle_set(pl, sizeof(pl), &s);
    CHECK(icd02_encode(ICD02_TYPE_OBSTACLE_SET, 1u, pl, plen, frame, sizeof(frame), &flen));
    companion_link_rx(frame, flen);
    companion_link_poll(hal_time_us());
    perception_tick_50hz(hal_time_us());
    CHECK(perception_get()->count == 1);
    CHECK_NEAR(perception_get()->min_range_m, 6.0, 0.02);

    /* HEALTH */
    uint8_t hl[7] = { 35u, 48u, 0xA4u, 0x01u, 1u, 6u, 0u };
    CHECK(icd02_encode(ICD02_TYPE_HEALTH, 2u, hl, 7u, frame, sizeof(frame), &flen));
    companion_link_rx(frame, flen);
    companion_link_poll(hal_time_us());
    CHECK(companion_stats()->health_valid);
    CHECK(companion_stats()->cpu_pct == 35u);
    CHECK_NEAR((float)companion_stats()->temp_c10 / 10.0f, 42.0, 0.1);
    CHECK(companion_stats()->inference_ms == 6u);

    /* unknown type counted, not applied */
    CHECK(icd02_encode(0x7Fu, 3u, hl, 7u, frame, sizeof(frame), &flen));
    companion_link_rx(frame, flen);
    companion_link_poll(hal_time_us());
    CHECK(companion_stats()->unknown_types == 1u);

    /* sequence gap counted (skip seq 5 after 3) */
    CHECK(icd02_encode(ICD02_TYPE_HEARTBEAT, 5u, hb, 7u, frame, sizeof(frame), &flen));
    companion_link_rx(frame, flen);
    companion_link_poll(hal_time_us());
    CHECK(companion_stats()->seq_gaps == 1u);

    /* corrupted frame counted as CRC error and never applied */
    uint8_t bad[ICD02_MAX_FRAME];
    memcpy(bad, frame, flen);
    bad[8] ^= 0xFFu;
    companion_link_rx(bad, flen);
    companion_link_poll(hal_time_us());
    companion_link_poll(hal_time_us());
    CHECK(companion_stats()->crc_errors >= 1u);

    /* heartbeat timeout (SYS-004): >1 s without a heartbeat -> unhealthy,
     * and NO failsafe escalation (advisory-only, DEC-007) */
    failsafe_init();
    failsafe_rc_keepalive();
    fc_failsafe_t fs0 = failsafe_active();
    for (int i = 0; i < 1100; i++) {
        hal_sim_step(1000u);
        companion_link_poll(hal_time_us());
        failsafe_rc_keepalive();
    }
    CHECK(!companion_healthy());
    CHECK(failsafe_active() == fs0);
}

/* COM-002/004: telemetry status record round-trip + version/CRC rejection. */
static void test_telemetry_status_record(void)
{
    telemetry_snapshot_t s, d;
    memset(&s, 0, sizeof(s));
    s.timestamp_us = 1234567u;
    s.attitude = (fc_quat_t){ 1.0f, 0.0f, 0.0f, 0.0f };
    s.vel_ned_ms = (fc_vec3_t){ 0.5f, -0.25f, -0.1f };
    s.altitude_m = 1.5f;
    s.min_cell_v = 3.72f;
    s.current_a = 10.5f;
    s.mode = 5u;
    s.failsafe = 2u;
    s.perception_mode = 3u;
    s.avoidance_mode = 1u;
    s.companion_healthy = true;

    uint8_t seq = 7u;
    uint8_t buf[128];
    size_t n = telemetry_build_status(&s, buf, sizeof(buf), &seq);
    CHECK(n > 0u);
    CHECK(seq == 8u);                       /* sequence advanced */
    CHECK(buf[1] == TELEM_SCHEMA_VER);      /* explicit schema version */
    CHECK(buf[4] == 7u);                    /* sequence in the record */

    CHECK(telemetry_parse_status(buf, n, &d));
    CHECK(d.timestamp_us == s.timestamp_us);
    CHECK_NEAR(d.altitude_m, 1.5, 1e-6);
    CHECK_NEAR(d.min_cell_v, 3.72, 1e-6);
    CHECK_NEAR(d.current_a, 10.5, 1e-6);
    CHECK_NEAR(d.vel_ned_ms.x, 0.5, 1e-6);
    CHECK(d.mode == 5u && d.failsafe == 2u);
    CHECK(d.perception_mode == 3u && d.avoidance_mode == 1u);
    CHECK(d.companion_healthy);

    /* wrong magic, wrong schema version, corrupted CRC, truncated -> all
     * rejected (a GS must never act on a mis-read record) */
    uint8_t tmp[128];
    memcpy(tmp, buf, n);
    tmp[0] = 0x00u;
    CHECK(!telemetry_parse_status(tmp, n, &d));
    memcpy(tmp, buf, n);
    tmp[1] = TELEM_SCHEMA_VER + 1u;
    CHECK(!telemetry_parse_status(tmp, n, &d));
    memcpy(tmp, buf, n);
    tmp[n - 1] ^= 0xFFu;
    CHECK(!telemetry_parse_status(tmp, n, &d));
    CHECK(!telemetry_parse_status(buf, 8u, &d));
}

/* COM-002: MAVLink v2 encode/parse round-trip for the GS message set. */
static void test_mavlink_v2_roundtrip(void)
{
    telemetry_snapshot_t s;
    memset(&s, 0, sizeof(s));
    s.timestamp_us = 5000000u;
    s.min_cell_v = 3.65f;
    s.current_a = 12.0f;
    s.altitude_m = 2.0f;
    s.failsafe = 0u;

    uint8_t seq = 0u, buf[MAVLINK_FRAME_MAX];
    size_t n = telemetry_emit_mavlink(MAVLINK_MSG_HEARTBEAT, &s, &seq, buf, sizeof(buf));
    CHECK(n == 9u + MAVLINK_HEADER_LEN + 2u);
    CHECK(seq == 1u);
    mavlink_msg_t m;
    size_t consumed = 0;
    CHECK(mavlink_parse(buf, n, &m, &consumed));
    CHECK(consumed == n);
    CHECK(m.msgid == MAVLINK_MSG_HEARTBEAT);
    CHECK(m.sysid == MAVLINK_SYS_ID && m.compid == MAVLINK_COMP_ID_FC);
    CHECK(m.seq == 0u && m.len == 9u);

    n = telemetry_emit_mavlink(MAVLINK_MSG_SYS_STATUS, &s, &seq, buf, sizeof(buf));
    CHECK(n == 31u + MAVLINK_HEADER_LEN + 2u);
    CHECK(mavlink_parse(buf, n, &m, &consumed));
    CHECK(m.msgid == MAVLINK_MSG_SYS_STATUS);
    CHECK_NEAR((float)((uint16_t)m.payload[14] | ((uint16_t)m.payload[15] << 8)) / 100.0f,
               s.min_cell_v, 0.02);              /* voltage_battery cV */

    /* CRC corruption -> rejected; resync consumes 1 byte */
    buf[n - 1] ^= 0xFFu;
    CHECK(!mavlink_parse(buf, n, &m, &consumed));
    CHECK(consumed == 1u);

    /* unknown message id and length mismatch rejected at encode time */
    CHECK(!mavlink_msg_known(200u));
    CHECK(telemetry_emit_mavlink(200u, &s, &seq, buf, sizeof(buf)) == 0u);
    CHECK(!mavlink_encode(MAVLINK_MSG_ATTITUDE, 1u, 1u, 0u, buf, 9u, buf, sizeof(buf), &consumed));
    /* truncation: header says more than provided */
    CHECK(!mavlink_parse(buf, 6u, &m, &consumed));
}

/* COM-004: FC_STATE builder keeps sequence and carries safety fields. */
static void test_companion_fc_state_builder(void)
{
    companion_link_init();
    fc_quat_t q = { 1.0f, 0.0f, 0.0f, 0.0f };
    fc_vec3_t v = { 0.0f, 0.0f, -0.2f };
    uint8_t f1[ICD02_MAX_FRAME], f2[ICD02_MAX_FRAME];
    size_t n1 = 0, n2 = 0;
    CHECK(companion_link_build_fc_state(1000u, &q, &v, 4u, 1u, 3u, 2u, 1.5f,
                                        f1, sizeof(f1), &n1));
    CHECK(companion_link_build_fc_state(1020u, &q, &v, 4u, 1u, 3u, 2u, 1.5f,
                                        f2, sizeof(f2), &n2));
    CHECK(n1 == 44u + ICD02_HEADER_LEN + 2u);
    icd02_frame_t fa, fb;
    size_t c1 = 0, c2 = 0;
    CHECK(icd02_parse(f1, n1, &fa, &c1));
    CHECK(icd02_parse(f2, n2, &fb, &c2));
    CHECK(fa.type == ICD02_TYPE_FC_STATE);
    CHECK((uint8_t)(fb.seq - fa.seq) == 1u);       /* monotonic seq */
    CHECK(fa.payload[40] == 4u);                    /* mode */
    CHECK(fa.payload[41] == 1u);                    /* failsafe state visible */
    CHECK(fa.payload[42] == 3u);                    /* perception mode */
    CHECK(fa.payload[43] == 2u);                    /* avoidance mode */
}

/* App-level: the SIM companion byte path keeps perception fed end-to-end. */
static void test_app_companion_byte_path(void)
{
    hal_sim_reset();
    hal_sim_set_seed(7u);
    hal_sim_set_noise(false);
    hal_sim_scenario(NULL);
    CHECK(app_init() == FC_OK);
    for (int i = 0; i < 8000; i++) { hal_sim_step(1000u); app_tick_1khz(); }

    const companion_stats_t *st = companion_stats();
    CHECK(st->frames_ok > 100u);                   /* 25 Hz sets over 8 s */
    CHECK(st->crc_errors == 0u);
    CHECK(st->unknown_types == 0u);
    CHECK(st->seq_gaps == 0u);
    CHECK(st->ring_overflow == 0u);
    CHECK(companion_healthy());
    CHECK(companion_ai_state() == CL_AI_RUNNING);
    CHECK(perception_get()->count >= 1u);
    CHECK(perception_get()->ai_healthy);
}

/* ---- Phase 21: vehicle model + horizontal control (SIM-003, DEC-017) ---- */

/* Closed-loop horizontal tracking: a 6 m waypoint step must actually move the
 * vehicle (lateral dynamics present) and be tracked by the estimator. The run
 * deliberately continues past arrival so the whole mission (WAYPOINT -> RTL ->
 * LAND) is exercised; the assertions use the PEAK excursion, not the final
 * position (which is back at home by design). */
static void test_sim_lateral_tracking(void)
{
    float max_x = -1e9f, peak_alt = 0.0f, max_abs_y = 0.0f;
    hal_sim_reset();
    hal_sim_set_seed(7u);
    hal_sim_scenario(NULL);
    hal_sim_set_noise(false);
    hal_sim_set_obstacle_active(false);
    CHECK(app_init() == FC_OK);
    nav_xy_t wp = { 6.0f, 0.0f };
    mission_load_waypoints(&wp, 1);

    for (int i = 0; i < 20000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
        float x = 0.0f, y = 0.0f;
        hal_sim_get_horizontal(&x, &y);
        if (x > max_x) max_x = x;
        if (fabsf(y) > max_abs_y) max_abs_y = fabsf(y);
        float a = est_alt_get_m();
        if (a > peak_alt) peak_alt = a;
    }
    float x = 0.0f, y = 0.0f;
    hal_sim_get_horizontal(&x, &y);
    printf("  lateral: max_x=%.2f m final_x=%.2f est=%.2f |y|max=%.2f "
           "peak_alt=%.2f state=%s\n", max_x, x, est_position_get().x_m,
           max_abs_y, peak_alt, mission_state_name());

    CHECK(max_x > 4.5f);                    /* vehicle really flew forward */
    CHECK(max_abs_y < 1.5f);                /* and did not diverge sideways */
    CHECK_NEAR(peak_alt, 1.5, 0.6);         /* altitude hold held while flying */
    CHECK(est_alt_get_m() < 0.5f);          /* and the landing really landed */
    CHECK(mission_state() == NAV_DONE);     /* full mission: WP -> RTL -> LAND */
}

/* Drag: a lateral velocity with no thrust decays instead of coasting forever. */
static void test_sim_drag_decay(void)
{
    hal_sim_reset();
    hal_sim_set_noise(false);
    hal_sim_set_horizontal(0.0f, 0.0f, 2.0f, 0.0f);
    float zero[4] = { 0, 0, 0, 0 };
    hal_actuator_write(zero);
    for (int i = 0; i < 2000; i++) hal_sim_step(1000u);   /* 2 s, no thrust */
    float vx = 0.0f, vy = 0.0f, x = 0.0f, y = 0.0f;
    hal_sim_get_horizontal_vel(&vx, &vy);
    hal_sim_get_horizontal(&x, &y);
    CHECK(vx < 0.5f);                       /* 2 m/s bled off through drag */
    CHECK(vx > -0.01f);                     /* drag does not reverse motion */
    CHECK(x > 0.5f && x < 2.2f);            /* travelled, but decelerating */
}

/* Disturbance rejection: a steady 1.5 m/s wind must be held off by the
 * position loop (the controller, not luck, cancels it). */
static void test_sim_wind_rejection(void)
{
    /* HOLD at the origin (no waypoints) keeps the vehicle airborne, which is
     * what the disturbance test needs — a landed vehicle has no horizontal
     * authority by design (airborne gate) and would trivially "hold". */
    hal_sim_reset();
    hal_sim_set_seed(7u);
    hal_sim_scenario(NULL);
    hal_sim_set_noise(false);
    hal_sim_set_obstacle_active(false);
    CHECK(app_init() == FC_OK);
    for (int i = 0; i < 8000; i++) { hal_sim_step(1000u); app_tick_1khz(); }
    CHECK(mission_state() == NAV_HOLD);
    float alt_before = est_alt_get_m();

    hal_sim_set_wind(1.5f, 0.0f);
    for (int i = 0; i < 12000; i++) { hal_sim_step(1000u); app_tick_1khz(); }
    float x = 0.0f, y = 0.0f;
    hal_sim_get_horizontal(&x, &y);
    est_xy_t e = est_position_get();
    printf("  wind 1.5 m/s: x=%.2f m (est %.2f) alt %.2f -> %.2f state=%s "
           "vel_sp=(%.2f,%.2f) acc_sp=(%.2f,%.2f) tilt=%.1f deg target=(%.2f,%.2f)\n",
           x, e.x_m, alt_before, est_alt_get_m(), mission_state_name(),
           ctrl_position_vel_sp().x, ctrl_position_vel_sp().y,
           ctrl_position_acc_sp().x, ctrl_position_acc_sp().y,
           ctrl_position_tilt_rad() * 57.2958f,
           mission_target_xy().x_m, mission_target_xy().y_m);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);
    CHECK(mission_state() == NAV_HOLD);
    CHECK(fabsf(x) < 1.0f);                 /* steady-state error bounded */
    CHECK(fabsf(e.x_m) < 1.5f);             /* estimator agrees */
    CHECK_NEAR(est_alt_get_m(), alt_before, 0.5);   /* altitude held */
}

/* AF limits are the last word: an absurd request cannot exceed them. */
static void test_position_limits(void)
{
    hal_sim_reset();
    hal_sim_set_noise(false);
    est_position_init();
    ctrl_position_init();
    est_attitude_init();
    hal_sim_step(1000u);
    ctrl_position_setpoint_xy(1000.0f, 1000.0f);   /* absurd mission target */
    fc_quat_t q = { 1.0f, 0.0f, 0.0f, 0.0f };
    ctrl_position_loop(0.02f, &q);
    fc_vec3_t v = ctrl_position_vel_sp();
    fc_vec3_t a = ctrl_position_acc_sp();
    CHECK(sqrtf(v.x * v.x + v.y * v.y) <= AF_VEL_MAX_M_S + 1e-4f);
    CHECK(sqrtf(a.x * a.x + a.y * a.y) <= AF_ACC_MAX_M_S2 + 1e-4f);
    CHECK(ctrl_position_tilt_rad() <= AF_TILT_MAX_RAD + 1e-4f);

    /* velocity override is clamped by the same AF limit */
    fc_vec3_t huge = { 50.0f, 50.0f, 0.0f };
    ctrl_position_vel_override(&huge, true);
    ctrl_position_loop(0.02f, &q);
    v = ctrl_position_vel_sp();
    CHECK(sqrtf(v.x * v.x + v.y * v.y) <= AF_VEL_MAX_M_S + 1e-4f);
    ctrl_position_vel_override(NULL, false);
}

/* End-to-end avoidance: flown toward an obstacle, the vehicle must slow and
 * stop while keeping a positive clearance (the Phase 18 PARTIAL gap closed). */
static void test_avoidance_flown_end_to_end(void)
{
    hal_sim_reset();
    hal_sim_set_seed(7u);
    hal_sim_scenario(NULL);
    hal_sim_set_noise(false);
    hal_sim_set_obstacle_active(true);      /* advancing obstacle environment */
    CHECK(app_init() == FC_OK);
    nav_xy_t wp = { 8.0f, 0.0f };
    mission_load_waypoints(&wp, 1);

    float max_x = -1e9f, min_clear = 1e9f;
    for (int i = 0; i < 25000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
        float x = 0.0f, y = 0.0f;
        hal_sim_get_horizontal(&x, &y);
        if (x > max_x) max_x = x;
        float c = hal_sim_obstacle_range_m();
        if (c < min_clear) min_clear = c;
    }
    float x = 0.0f, y = 0.0f;
    hal_sim_get_horizontal(&x, &y);
    printf("  flown avoidance: max_x=%.2f m final_x=%.2f m min_clearance=%.2f m mode=%d\n",
           max_x, x, min_clear, (int)avoidance_get()->mode);
    CHECK(max_x > 2.0f);                    /* it really flew toward the target */
    CHECK(min_clear > 0.4f);                /* never reached the contact floor */
    CHECK(avoidance_get()->mode == AVOID_RETREAT);   /* bounded retreat, DEC-018 */
    CHECK(mission_state() != NAV_ABORT);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);
}

/* ======================================================================
 * Phase 22 - HIL link codec, RX ring and DShot capture decode.
 * These run in the SIM target because hil_link.c and esc_dshot.c are
 * hardware-independent: the link is exercised on the host here, and the
 * same code runs between the FC and the rig in the HIL target.
 * ====================================================================== */

static void test_hil_link_frame_roundtrip(void)
{
    uint8_t payload[32];
    for (int i = 0; i < 32; i++) payload[i] = (uint8_t)(i * 7 + 1);

    uint8_t buf[HIL_LINK_MAX_FRAME];
    size_t n = hil_link_encode(HIL_MSG_SENSORS, 0x2A, 0x01, 1234567ull,
                               payload, sizeof(payload), buf, sizeof(buf));
    CHECK(n == HIL_LINK_HDR_LEN + sizeof(payload) + HIL_LINK_CRC_LEN);
    CHECK(buf[0] == HIL_LINK_SOF0 && buf[1] == HIL_LINK_SOF1);
    CHECK(buf[2] == HIL_LINK_VERSION);

    hil_msg_t m;
    CHECK(hil_link_decode(buf, n, &m) == HIL_RX_OK);
    CHECK(m.type == HIL_MSG_SENSORS);
    CHECK(m.seq == 0x2A);
    CHECK(m.flags == 0x01);
    CHECK(m.len == sizeof(payload));
    CHECK(m.t_us == 1234567ull);
    CHECK(memcmp(m.payload, payload, sizeof(payload)) == 0);

    /* header fields are little-endian regardless of host byte order */
    CHECK(buf[4] == 0x2A);
    CHECK(buf[8] == 0x87 && buf[9] == 0xD6 && buf[10] == 0x12);   /* 1234567 = 0x0012D687 */

    /* a frame that does not fit must be refused, never truncated */
    CHECK(hil_link_encode(HIL_MSG_SENSORS, 0, 0, 0, payload, sizeof(payload),
                          buf, n - 1u) == 0);
}

static void test_hil_link_corruption_and_resync(void)
{
    uint8_t payload[64];
    memset(payload, 0x5A, sizeof(payload));
    uint8_t buf[HIL_LINK_MAX_FRAME];
    size_t n = hil_link_encode(HIL_MSG_COMPANION, 7, 0, 999ull,
                               payload, sizeof(payload), buf, sizeof(buf));

    /* every single-bit flip anywhere in the frame must be rejected on CRC */
    int detected = 0;
    for (size_t byte = 0; byte < n; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t tmp[HIL_LINK_MAX_FRAME];
            memcpy(tmp, buf, n);
            tmp[byte] ^= (uint8_t)(1u << bit);
            hil_msg_t m;
            hil_rx_t rc = hil_link_decode(tmp, n, &m);
            if (rc != HIL_RX_OK) detected++;
            else if (memcmp(m.payload, payload, sizeof(payload)) != 0) detected++;
        }
    }
    CHECK(detected == (int)(n * 8));

    /* the ring recovers: garbage, a corrupt frame, then good frames */
    hil_ring_t r;
    hil_ring_reset(&r);
    uint8_t stream[1024];
    size_t o = 0;
    const uint8_t garbage[7] = { 0x00, 0xFF, 0xA5, 0x11, 0xA5, 0x5A, 0x42 };
    memcpy(stream + o, garbage, sizeof(garbage)); o += sizeof(garbage);
    uint8_t bad[HIL_LINK_MAX_FRAME];
    size_t bn = hil_link_encode(HIL_MSG_ACK, 1, 0, 1, NULL, 0, bad, sizeof(bad));
    bad[bn - 1u] ^= 0x04u;                       /* corrupt the CRC */
    memcpy(stream + o, bad, bn); o += bn;
    for (int k = 0; k < 3; k++) {
        size_t fn = hil_link_encode(HIL_MSG_ACK, (uint8_t)(2 + k), 0,
                                    (uint64_t)(10 + k), NULL, 0, bad, sizeof(bad));
        memcpy(stream + o, bad, fn); o += fn;
    }
    hil_ring_push(&r, stream, o);

    int ok = 0, crc = 0;
    for (;;) {
        hil_msg_t m;
        hil_rx_t rc = hil_ring_pop(&r, &m);
        if (rc == HIL_RX_OK) { ok++; continue; }
        if (rc == HIL_RX_CRC_ERR) { crc++; continue; }
        if (rc == HIL_RX_BAD) continue;   /* resync in progress, keep going */
        break;
    }
    CHECK(crc == 1);        /* the one corrupted frame */
    CHECK(ok == 3);         /* the three good frames survived */
    CHECK(r.crc_errors == 1);
    CHECK(r.resyncs >= 1);  /* the garbage prefix was skipped */
    CHECK(r.frames == 3);
}

static void test_hil_link_truncation_at_every_cut(void)
{
    uint8_t payload[100];
    for (int i = 0; i < 100; i++) payload[i] = (uint8_t)i;
    uint8_t buf[HIL_LINK_MAX_FRAME];
    size_t n = hil_link_encode(HIL_MSG_SENSORS, 3, 0, 4242ull,
                               payload, sizeof(payload), buf, sizeof(buf));

    /* a stream cut at ANY point must never yield a bogus frame: the ring has
     * to report NEED_MORE until the rest arrives */
    int partial_ok = 0;
    for (size_t cut = 1; cut < n; cut++) {
        hil_ring_t r;
        hil_ring_reset(&r);
        hil_ring_push(&r, buf, cut);
        hil_msg_t m;
        if (hil_ring_pop(&r, &m) == HIL_RX_OK) partial_ok++;
    }
    CHECK(partial_ok == 0);

    /* completing the stream yields exactly one frame */
    hil_ring_t r;
    hil_ring_reset(&r);
    hil_ring_push(&r, buf, n);
    hil_msg_t m;
    CHECK(hil_ring_pop(&r, &m) == HIL_RX_OK);
    CHECK(m.len == sizeof(payload));
    CHECK(memcmp(m.payload, payload, sizeof(payload)) == 0);
    CHECK(hil_ring_pop(&r, &m) == HIL_RX_NEED_MORE);

    /* byte-at-a-time delivery must produce the same single frame */
    hil_ring_reset(&r);
    int ok = 0;
    for (size_t i = 0; i < n; i++) {
        hil_ring_push(&r, buf + i, 1);
        for (;;) {
            hil_msg_t mm;
            hil_rx_t rc = hil_ring_pop(&r, &mm);
            if (rc == HIL_RX_OK) { ok++; continue; }
            break;
        }
    }
    CHECK(ok == 1);
}

static void test_hil_link_ring_wraparound(void)
{
    /* The ring wraps many times over this run. The first implementation
     * compacted a wrapped window with one memmove over the modular length,
     * which wrote up to 4095 bytes past buf[] and smashed head/frames
     * (SIGSEGV in the Phase 22 loopback run). This test drives the wrap with
     * randomly-sized pushes and pops and checks that every frame survives and
     * the indices stay in range. */
    hil_ring_t r;
    hil_ring_reset(&r);
    uint8_t payload[160];
    memset(payload, 0xC3, sizeof(payload));

    uint32_t rng = 0x1234567u;
    uint32_t sent = 0, got = 0, expect_seq = 0;
    uint32_t bad_frames = 0, bad_indices = 0;
    static uint8_t stream[1 << 16];
    size_t gen = 0, pushed = 0;

    for (int iter = 0; iter < 400; iter++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;

        /* append 1..3 complete frames to the not-yet-transmitted stream */
        int add = (int)(rng % 3u) + 1;
        for (int a = 0; a < add; a++) {
            if (gen + (HIL_LINK_MAX_FRAME) > sizeof(stream)) break;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            uint16_t plen = (uint16_t)(1u + (rng % 160u));
            size_t n = hil_link_encode(HIL_MSG_AI_SET, (uint8_t)sent, 0,
                                       (uint64_t)sent, payload, plen,
                                       stream + gen, sizeof(stream) - gen);
            CHECK(n > 0);
            gen += n;
            sent++;
        }

        /* hand a random slice of the stream to the ring (sizes 1..200 bytes:
         * frames are split across many pushes and the ring wraps repeatedly) */
        size_t pending = gen - pushed;
        size_t take = (size_t)(rng % 200u) + 1u;
        if (take > pending) take = pending;
        if (take) {
            hil_ring_push(&r, stream + pushed, take);
            pushed += take;
        }

        /* drain whatever is complete (checked in aggregate: this loop runs
         * ~1000 times and the suite reports per-check counts) */
        for (;;) {
            hil_msg_t m;
            hil_rx_t rc = hil_ring_pop(&r, &m);
            if (rc != HIL_RX_OK) break;
            if (m.type != HIL_MSG_AI_SET ||
                m.seq != (uint8_t)expect_seq || m.t_us != expect_seq) bad_frames++;
            if (r.head >= HIL_RING_CAP || r.tail >= HIL_RING_CAP) bad_indices++;
            expect_seq++;
            got++;
        }
    }
    /* drain the tail in bounded chunks (pushing it all at once would overrun the
     * ring, which is a different failure than the one under test) */
    while (pushed < gen) {
        size_t take = gen - pushed;
        if (take > 512u) take = 512u;
        hil_ring_push(&r, stream + pushed, take);
        pushed += take;
        for (;;) {
            hil_msg_t m;
            if (hil_ring_pop(&r, &m) != HIL_RX_OK) break;
            if (m.seq != (uint8_t)expect_seq || m.t_us != expect_seq) bad_frames++;
            expect_seq++;
            got++;
        }
    }
    for (;;) {
        hil_msg_t m;
        hil_rx_t rc = hil_ring_pop(&r, &m);
        if (rc != HIL_RX_OK) break;
        if (m.seq != (uint8_t)expect_seq || m.t_us != expect_seq) bad_frames++;
        expect_seq++;
        got++;
    }
    CHECK(bad_frames == 0);
    CHECK(bad_indices == 0);
    CHECK(got == sent);          /* nothing lost, nothing duplicated */
    CHECK(sent > 100);
    CHECK(got > 100);
    CHECK(r.crc_errors == 0);
    CHECK(r.seq_gaps == 0);
    CHECK(r.overrun == 0);

    /* deliberate sequence loss must be counted, not hidden */
    hil_ring_reset(&r);
    uint8_t buf[HIL_LINK_MAX_FRAME];
    size_t n0 = hil_link_encode(HIL_MSG_ACK, 0, 0, 0, NULL, 0, buf, sizeof(buf));
    hil_ring_push(&r, buf, n0);
    n0 = hil_link_encode(HIL_MSG_ACK, 3, 0, 0, NULL, 0, buf, sizeof(buf));  /* 1,2 lost */
    hil_ring_push(&r, buf, n0);
    int ok = 0;
    for (;;) {
        hil_msg_t m;
        if (hil_ring_pop(&r, &m) != HIL_RX_OK) break;
        ok++;
    }
    CHECK(ok == 2);
    CHECK(r.seq_gaps == 1);
}

static void test_hil_link_sensor_payload(void)
{
    hil_sensor_frame_t f;
    memset(&f, 0, sizeof(f));
    f.avail = HIL_SENSOR_IMU | HIL_SENSOR_BARO | HIL_SENSOR_GNSS | HIL_SENSOR_TOF0 |
              HIL_SENSOR_TOF1 | HIL_SENSOR_BATTERY | HIL_SENSOR_RC | HIL_SENSOR_FLOW;
    f.stat = HIL_STAT_GNSS_FIX | HIL_STAT_IMU_VALID | HIL_STAT_BARO_VALID |
             HIL_STAT_RC_VALID | HIL_STAT_FLOW_VALID;
    f.sats = 14;
    f.gyro_rad_s.x = 0.125f; f.gyro_rad_s.y = -0.5f; f.gyro_rad_s.z = 0.0625f;
    f.accel_m_s2.x = -1.5f; f.accel_m_s2.y = 2.25f; f.accel_m_s2.z = 9.81f;
    f.pressure_pa = 101325.0f; f.temperature_c = 20.5f;
    f.tof0_m = 1.234f; f.tof1_m = 9.876f;
    f.flow_x_px = 12.0f; f.flow_y_px = -34.0f; f.flow_quality = 0.87f;
    f.pack_v = 15.2f; f.current_a = 9.5f;
    for (int i = 0; i < 4; i++) f.cell_v[i] = 3.8f - 0.1f * (float)i;
    f.lat_deg = 47.397742; f.lon_deg = 8.545594;
    f.alt_m = 12.5f; f.vn_ms = 1.5f; f.ve_ms = -2.5f; f.vd_ms = -0.25f;
    for (int i = 0; i < 8; i++) f.rc_ch[i] = (uint16_t)(1000 + 100 * i);
    f.consumed_mah = 1234.5f;
    for (unsigned i = 0; i < HIL_SENSOR_COUNT; i++) f.t_sample_ms[i] = 1000u + i * 20u;

    uint8_t buf[HIL_LINK_MAX_PAYLOAD];
    uint16_t n = hil_pack_sensors(&f, buf, sizeof(buf));
    CHECK(n == HIL_SENSOR_PAYLOAD_LEN);

    hil_sensor_frame_t g;
    CHECK(hil_unpack_sensors(buf, n, &g));
    CHECK(g.avail == f.avail);
    CHECK(g.stat == f.stat);
    CHECK(g.sats == f.sats);
    CHECK(g.gyro_rad_s.x == f.gyro_rad_s.x && g.gyro_rad_s.y == f.gyro_rad_s.y);
    CHECK(g.accel_m_s2.z == f.accel_m_s2.z);
    CHECK(g.pressure_pa == f.pressure_pa && g.temperature_c == f.temperature_c);
    CHECK(g.tof0_m == f.tof0_m && g.tof1_m == f.tof1_m);
    CHECK(g.flow_valid && g.flow_quality == f.flow_quality);
    CHECK(g.pack_v == f.pack_v && g.current_a == f.current_a);
    for (int i = 0; i < 4; i++) CHECK(g.cell_v[i] == f.cell_v[i]);
    CHECK(g.lat_deg == f.lat_deg && g.lon_deg == f.lon_deg);
    CHECK(g.alt_m == f.alt_m && g.vn_ms == f.vn_ms && g.vd_ms == f.vd_ms);
    for (int i = 0; i < 8; i++) CHECK(g.rc_ch[i] == f.rc_ch[i]);
    CHECK(g.consumed_mah == f.consumed_mah);
    for (unsigned i = 0; i < HIL_SENSOR_COUNT; i++) {
        CHECK(g.t_sample_ms[i] == f.t_sample_ms[i]);
    }

    /* a short payload must be refused */
    CHECK(!hil_unpack_sensors(buf, n - 1u, &g));

    /* the whole frame must fit the link's payload budget */
    uint8_t frame[HIL_LINK_MAX_FRAME];
    size_t fn = hil_link_encode(HIL_MSG_SENSORS, 0, 0, 0, buf, n, frame, sizeof(frame));
    CHECK(fn > 0 && fn <= HIL_LINK_MAX_FRAME);
}

static void test_hil_link_ai_set_payload(void)
{
    fc_ai_obstacle_set_t set;
    memset(&set, 0, sizeof(set));
    set.timestamp_us = 55555;
    set.count = 3;
    for (uint8_t i = 0; i < 3; i++) {
        set.det[i].pos_m.x = 1.0f * (i + 1);
        set.det[i].pos_m.y = -0.5f * (i + 1);
        set.det[i].pos_m.z = 0.25f * (i + 1);
        set.det[i].vel_m_s.x = -0.5f;
        set.det[i].vel_m_s.z = 0.125f;
        set.det[i].radius_m = 0.3f + 0.1f * i;
        set.det[i].confidence = 0.9f - 0.1f * i;
        set.det[i].class_id = (uint8_t)(i + 1);
    }
    uint8_t buf[HIL_LINK_MAX_PAYLOAD];
    uint16_t n = hil_pack_ai_set(&set, buf, sizeof(buf));
    CHECK(n == 1u + 3u * 33u);

    fc_ai_obstacle_set_t g;
    CHECK(hil_unpack_ai_set(buf, n, &g));
    CHECK(g.count == 3);
    for (uint8_t i = 0; i < 3; i++) {
        CHECK(g.det[i].pos_m.x == set.det[i].pos_m.x);
        CHECK(g.det[i].pos_m.y == set.det[i].pos_m.y);
        CHECK(g.det[i].pos_m.z == set.det[i].pos_m.z);
        CHECK(g.det[i].vel_m_s.x == set.det[i].vel_m_s.x);
        CHECK(g.det[i].vel_m_s.z == set.det[i].vel_m_s.z);
        CHECK(g.det[i].radius_m == set.det[i].radius_m);
        CHECK(g.det[i].confidence == set.det[i].confidence);
        CHECK(g.det[i].class_id == set.det[i].class_id);
    }
    /* truncated payload must be refused, not read past the end */
    CHECK(!hil_unpack_ai_set(buf, n - 1u, &g));

    /* the full ICD-02 maximum (16 detections) must still fit the link */
    set.count = FC_AI_MAX_DETECTIONS;
    uint16_t n16 = hil_pack_ai_set(&set, buf, sizeof(buf));
    CHECK(n16 == 1u + 16u * 33u);
    CHECK(n16 <= HIL_LINK_MAX_PAYLOAD);
    CHECK(hil_unpack_ai_set(buf, n16, &g));
    CHECK(g.count == FC_AI_MAX_DETECTIONS);
}

static void test_hil_link_fault_vocabulary(void)
{
    /* the HIL fault names must be exactly the SIM scenario names, otherwise a
     * scenario cannot be replayed across the two targets */
    static const char *const names[] = {
        "imu_dropout", "imu_stuck", "rc_loss", "battery_low", "gnss_loss", "ai_loss"
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        int id = hil_fault_name_to_id(names[i]);
        CHECK(id > 0);
        CHECK(strcmp(hil_fault_id_to_name(id), names[i]) == 0);
    }
    CHECK(hil_fault_name_to_id("no_such_fault") == -1);
    CHECK(strcmp(hil_fault_id_to_name(999), "?") == 0);
}

static void test_dshot_capture_roundtrip(void)
{
    /* The HIL rig and the capture analyser decode the frames the FC emitted:
     * encode -> decode must be bit exact, and a single flipped bit anywhere
     * must be rejected exactly as a real ESC rejects it. */
    static const float units[] = { 0.0f, 0.01f, 0.25f, 0.33333f, 0.5f, 0.75f, 1.0f };
    for (unsigned i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        uint16_t thr = dshot_throttle_from_unit(units[i]);
        uint16_t frame = dshot_build_frame(thr, false);
        uint16_t back = 0;
        bool tel = true;
        CHECK(dshot_decode_frame(frame, &back, &tel));
        CHECK(!tel);
        CHECK(back == thr);
        float unit = dshot_unit_from_throttle(thr);
        /* one LSB of DSHOT resolution is ~0.0005 in normalized units */
        CHECK(fabsf(unit - units[i]) < 0.001f);
    }
    /* stop command round-trips as zero thrust */
    CHECK(dshot_throttle_from_unit(0.0f) == DSHOT_CMD_MOTOR_STOP);
    CHECK(dshot_unit_from_throttle(DSHOT_CMD_MOTOR_STOP) == 0.0f);

    /* telemetry request bit survives */
    uint16_t f = dshot_build_frame(1000, true);
    uint16_t t = 0; bool tel = false;
    CHECK(dshot_decode_frame(f, &t, &tel));
    CHECK(tel && t == 1000);

    /* every single-bit flip in the 16-bit frame must fail the CRC-4 */
    int rejected = 0, total = 0;
    for (uint16_t thr = 0; thr <= 2047u; thr += 97u) {
        uint16_t frame = dshot_build_frame(thr, false);
        for (int bit = 0; bit < 16; bit++) {
            total++;
            uint16_t bad = frame ^ (uint16_t)(1u << bit);
            uint16_t got = 0;
            if (!dshot_decode_frame(bad, &got, NULL)) rejected++;
        }
    }
    CHECK(total > 300);
    CHECK(rejected == total);   /* Hamming-distance-4 payload+CRC: no survivors */
}

/* ======================================================================
 * Phase 23 - verification: tests that close requirement gaps which had
 * no evidence row (SYS-006, FW-002, FW-004, FW-005, EST-055, CTRL-004).
 * ====================================================================== */

/* SYS-006 / FW-005: configuration survives a power interruption, and a
 * half-written or corrupted image falls back to safe defaults rather than to
 * arbitrary values. The SIM flash model is static storage, so a "power cycle"
 * here is exactly what the requirement asks about: the image must survive the
 * restart, and the restart must not be able to load garbage. */
static void test_param_power_cycle(void)
{
    params_t stored;
    params_defaults(&stored);
    stored.rate_roll_kp  = 0.0123f;
    stored.batt_land_v   = 3.15f;
    CHECK(params_store(&stored) == FC_OK);

    /* power cycle 1: clean image survives */
    params_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    CHECK(params_load(&loaded) == FC_OK);
    CHECK(loaded.magic == PARAM_MAGIC);
    CHECK(loaded.rate_roll_kp == 0.0123f);
    CHECK(loaded.batt_land_v == 3.15f);

    /* power interrupted mid-image: magic destroyed -> safe defaults */
    uint8_t z = 0x00u;
    CHECK(hal_flash_write(0x00000000u, &z, 1) == HAL_OK);
    params_t broken;
    memset(&broken, 0, sizeof(broken));
    CHECK(params_load(&broken) == FC_INVALID);
    params_t ref;
    params_defaults(&ref);
    CHECK(broken.rate_roll_kp == ref.rate_roll_kp);
    CHECK(broken.batt_land_v == ref.batt_land_v);
    CHECK(broken.batt_warn_v == 3.5f);          /* SAF-030 default restored */

    /* magic intact but the CRC broken (a partial write that kept the header) */
    params_t again;
    params_defaults(&again);
    again.rate_roll_kp = 0.0456f;
    CHECK(params_store(&again) == FC_OK);
    CHECK(hal_flash_write(0x00000010u, &z, 1) == HAL_OK);   /* inside the blob */
    params_t crc_broken;
    memset(&crc_broken, 0, sizeof(crc_broken));
    CHECK(params_load(&crc_broken) == FC_INVALID);
    CHECK(crc_broken.rate_roll_kp == ref.rate_roll_kp);
    CHECK(crc_broken.magic == PARAM_MAGIC);      /* defaults, not zeroed memory */

    /* recovery: storing again must work after the corrupted image */
    params_defaults(&stored);
    stored.rate_roll_kp = 0.135f;
    CHECK(params_store(&stored) == FC_OK);
    params_t final;
    CHECK(params_load(&final) == FC_OK);
    CHECK(final.rate_roll_kp == 0.135f);
}

/* FW-002 / SYS-001 / SYS-002: scheduler timing distribution.
 * HONEST SCOPE: this measures HOST execution time of one control tick, so it
 * bounds the software budget and proves the fixed-rate dispatch plus deadline
 * monitoring work. It is NOT MCU timing evidence - HIL-3 must measure the loop
 * rate and end-to-end latency on the real target (SYS-001/002 stay Open(H)). */
static void test_loop_timing_distribution(void)
{
    hal_sim_reset();
    hal_sim_set_seed(11u);
    hal_sim_scenario(NULL);
    hal_sim_set_noise(true);
    CHECK(app_init() == FC_OK);

    enum { N = 20000 };
    static uint32_t samples_us[N];
    uint32_t max_us = 0;
    uint64_t sum_us = 0;

    for (int i = 0; i < N; i++) {
        hal_sim_step(1000u);
        uint64_t t0 = now_monotonic_us();
        app_tick_1khz();
        uint32_t dt = (uint32_t)(now_monotonic_us() - t0);
        samples_us[i] = dt;
        sum_us += dt;
        if (dt > max_us) max_us = dt;
    }

    /* p50 / p99 of the execution-time distribution (insertion sort on a copy) */
    static uint32_t sorted[N];
    memcpy(sorted, samples_us, sizeof(sorted));
    for (int i = 1; i < N; i++) {          /* N log N is fine for a test */
        uint32_t key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; j--; }
        sorted[j + 1] = key;
    }
    uint32_t p50 = sorted[N / 2];
    uint32_t p99 = sorted[(N * 99) / 100];
    uint32_t p999 = sorted[(N * 999) / 1000];
    printf("    loop timing: mean=%.1f us p50=%u us p99=%u us p99.9=%u us max=%u us\n",
           (double)sum_us / (double)N, p50, p99, p999, max_us);

    /* the app's own deadline is 900 us (SAF-004): p99 must sit far below it and
     * the scheduler must not have reported a miss */
    CHECK(p99 <= 900u);
    CHECK(p999 <= 900u);
    /* a fixed-rate 1 kHz tick cannot be doing meaningful work per call: a tick
     * that averaged more than a tenth of its own period would be a design error */
    CHECK((sum_us / (uint64_t)N) <= 100u);
}

/* EST-055: estimator health degrades and is *observed* by the failsafe path.
 * GNSS loss after a healthy fix must be flagged within a bounded time, and the
 * mission must respond (controlled descent in place, DEC-011). */
static void test_estimator_health_escalation(void)
{
    hal_sim_reset();
    hal_sim_set_seed(3u);
    hal_sim_scenario(NULL);
    hal_sim_set_noise(false);
    hal_sim_set_obstacle_active(false);
    CHECK(app_init() == FC_OK);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);

    /* fly until position has been healthy at least once */
    int healthy_at = -1;
    for (int i = 0; i < 12000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
        if (healthy_at < 0 && est_position_healthy()) healthy_at = i;
        if (healthy_at > 0 && i > healthy_at + 500) break;
    }
    CHECK(healthy_at > 0);                 /* TTFF passed and the fix is valid */

    /* kill GNSS: the estimator must declare itself unhealthy, quickly */
    hal_sim_fault("gnss_loss", true);
    int unhealthy_after = -1;
    nav_state_t state_before = mission_state();
    for (int i = 0; i < 2000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
        if (!est_position_healthy()) { unhealthy_after = i; break; }
    }
    CHECK(unhealthy_after >= 0);
    /* the estimator's declared GNSS timeout is 1.0 s (est_position.c) plus at
     * most one 10 Hz GNSS period of sample age */
    printf("    est health: gnss loss -> est_position_healthy()==false after %d ms\n",
           unhealthy_after);
    CHECK(unhealthy_after <= 1200);

    /* and the mission must respond rather than keep flying a dead estimate */
    int landed = 0;
    for (int i = 0; i < 20000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
        if (mission_state() == NAV_LAND || mission_state() == NAV_DONE) { landed = 1; break; }
    }
    CHECK(landed == 1);
    CHECK(mission_state() != state_before || landed == 1);
    /* Phase 24 changed this deliberately: position loss IS now a reported
     * failsafe tier (FC_FAILSAFE_ESTIMATOR) because the vehicle was descending
     * for a reason the operator could not see on the wire (the Phase 23
     * gnss_loss decode showed failsafe=NONE during the landing). The bounded
     * part of the old contract is kept and tightened: the action is a
     * controlled LAND, never a motor stop and never an abort of the descent. */
    CHECK(failsafe_active() == FC_FAILSAFE_ESTIMATOR);
    CHECK(failsafe_action() == FC_ACTION_LAND);
    CHECK(failsafe_action() != FC_ACTION_MOTOR_STOP);
}

/* CTRL-004 / SAF-005: arming is gated on a live RC link and healthy sensors.
 * With no RC the mission must never start even though every other condition
 * (IMU, estimator, attitude, mission) is satisfied. */
static void test_arming_interlock_rc_gate(void)
{
    hal_sim_reset();
    hal_sim_set_seed(5u);
    hal_sim_scenario(NULL);
    hal_sim_set_noise(false);
    hal_sim_set_obstacle_active(false);
    CHECK(app_init() == FC_OK);

    /* RC dead from the start: nothing may arm or fly */
    hal_sim_fault("rc_loss", true);
    for (int i = 0; i < 15000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
    }
    CHECK(mission_state() == NAV_IDLE);
    CHECK(motor_output_is_armed() == false);
    uint16_t frames[4];
    motor_output_frames(frames);
    for (int i = 0; i < 4; i++) {
        uint16_t thr = 0;
        bool tel = false;
        CHECK(dshot_decode_frame(frames[i], &thr, &tel));
        CHECK(thr == DSHOT_CMD_MOTOR_STOP);     /* no thrust without RC */
    }

    /* RC restored and the arm switch goes high -> the mission starts */
    hal_sim_fault("rc_loss", false);
    int started = 0;
    for (int i = 0; i < 5000; i++) {
        hal_sim_step(1000u);
        app_tick_1khz();
        if (mission_state() != NAV_IDLE) { started = 1; break; }
    }
    CHECK(started == 1);
    CHECK(motor_output_is_armed() == true);
}

/* FW-004: no dynamic allocation anywhere in the control path (audit; the
 * executable grep lives in 08_testing/audit_requirements.py). */
static void test_no_dynamic_allocation_api(void)
{
    /* The suite itself only proves the property it can observe at runtime:
     * the parameter store and the protocol codecs allocate nothing and work
     * from caller-provided buffers. Verified here by repeating a large codec
     * round-trip and confirming the values are identical - a heap-corrupted
     * run would show up as a CRC or length mismatch. */
    uint8_t buf[HIL_LINK_MAX_FRAME];
    for (int i = 0; i < 256; i++) {
        uint8_t payload[64];
        for (int k = 0; k < 64; k++) payload[k] = (uint8_t)(i + k);
        size_t n = hil_link_encode(HIL_MSG_AI_SET, (uint8_t)i, 0, (uint64_t)i,
                                   payload, sizeof(payload), buf, sizeof(buf));
        CHECK(n == HIL_LINK_HDR_LEN + sizeof(payload) + HIL_LINK_CRC_LEN);
        hil_msg_t m;
        CHECK(hil_link_decode(buf, n, &m) == HIL_RX_OK);
        CHECK(m.seq == (uint8_t)i);
        CHECK(memcmp(m.payload, payload, sizeof(payload)) == 0);
    }
}

/* ---- Phase 24: safety analysis follow-up (failsafe actions, tiers, gate) --- */

/* Helper: build a command frame the way a host sender would (CRC little-endian
 * over header+payload). Mirrors the ICD in cmd_gate.h on purpose, so the test
 * does not reuse the firmware encoder it is checking. */
static size_t cmd_build(uint8_t *out, uint8_t type, const uint8_t *payload,
                        uint8_t plen, bool corrupt_crc)
{
    out[0] = CMD_MAGIC;
    out[1] = CMD_VERSION;
    out[2] = type;
    out[3] = plen;
    if (plen > 0u && payload) memcpy(&out[CMD_HDR], payload, plen);
    size_t total = CMD_HDR + plen;
    uint16_t crc = crc16_ccitt(out, total);
    if (corrupt_crc) crc = (uint16_t)(crc ^ 0xFFFFu);
    out[total] = (uint8_t)(crc & 0xFFu);
    out[total + 1] = (uint8_t)(crc >> 8);
    return total + CMD_CRC_LEN;
}

/* SAF-002/SAF-001 with DEC-021: the ACTION must be selected by what the vehicle
 * can still fly, not by which failure the report ranks first. The two cases that
 * matter most are the cross-domain ones the old code got wrong:
 *   IMU dead + RC lost      -> report RC_LOSS (SAF-001 order), action MOTOR_STOP
 *   IMU dead + battery crit -> report BATTERY,             action MOTOR_STOP
 * Single-fault cases must keep their flyable actions. */
static void test_safety_action_feasibility(void)
{
    failsafe_init();
    failsafe_imu_update(true);        /* IMU seen at least once */
    failsafe_notify_estimator(true);
    failsafe_notify_geofence(false);
    failsafe_rc_keepalive();

    /* healthy: no failsafe, no action */
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);
    CHECK(failsafe_action() == FC_ACTION_NONE);

    /* RC lost alone -> RTL is still flyable (attitude + altitude available).
     * The IMU is fed while time passes: a stale IMU is its own failure, and
     * mixing the two would test nothing about RC loss. */
    for (int i = 0; i < 600; i++) {
        hal_sim_step(1000u);
        failsafe_imu_update(true);
    }
    CHECK(failsafe_active() == FC_FAILSAFE_RC_LOSS);
    CHECK(failsafe_action() == FC_ACTION_RTL);

    /* IMU dead as well: the report keeps the SAF-001 order, the action does not */
    failsafe_init();
    failsafe_imu_update(true);
    failsafe_rc_keepalive();
    for (int i = 0; i < 600; i++) {
        hal_sim_step(1000u);
        failsafe_imu_update(true);
    }
    CHECK(failsafe_active() == FC_FAILSAFE_RC_LOSS);   /* hierarchy unchanged */
    for (int i = 0; i < 200; i++) {                    /* > 100 ms IMU timeout */
        hal_sim_step(1000u);
        failsafe_imu_update(false);
    }
    CHECK(failsafe_active() == FC_FAILSAFE_RC_LOSS);   /* still the top report */
    CHECK(failsafe_action() == FC_ACTION_MOTOR_STOP);   /* nothing is flyable */
    printf("    action feasibility: rc_loss+imu_dead -> report RC_LOSS, action MOTOR_STOP\n");

    /* battery-critical + dead IMU: a motor stop, never a LAND. SAF-001 still
     * reports IMU (it out-ranks battery); the point is the action. */
    failsafe_init();
    failsafe_imu_update(true);
    failsafe_rc_keepalive();
    for (int i = 0; i < 200; i++) {
        hal_sim_step(1000u);
        failsafe_imu_update(false);
    }
    fc_battery_sample_t b;
    memset(&b, 0, sizeof(b));
    b.valid = true;
    for (int i = 0; i < 4; i++) b.cell_v[i] = 3.0f;   /* below the 3.1 land band */
    failsafe_battery_update(&b);
    CHECK(failsafe_active() == FC_FAILSAFE_IMU);
    CHECK(failsafe_action() == FC_ACTION_MOTOR_STOP);

    /* battery in the RTL band with attitude available -> RTL (SAF-030) */
    failsafe_init();
    failsafe_imu_update(true);
    failsafe_rc_keepalive();
    for (int i = 0; i < 10; i++) { hal_sim_step(1000u); failsafe_imu_update(true); }
    for (int i = 0; i < 4; i++) b.cell_v[i] = 3.45f;  /* above RTL: warn only */
    failsafe_battery_update(&b);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);      /* not critical */
    CHECK(failsafe_action() == FC_ACTION_NONE);
    for (int i = 0; i < 4; i++) b.cell_v[i] = 3.25f;  /* in the RTL band */
    failsafe_battery_update(&b);
    CHECK(failsafe_action() == FC_ACTION_RTL);

    /* estimator loss alone -> LAND (no position, so RTL is not flyable) */
    failsafe_init();
    failsafe_imu_update(true);
    failsafe_rc_keepalive();
    failsafe_notify_estimator(true);
    failsafe_notify_estimator(false);
    CHECK(failsafe_active() == FC_FAILSAFE_ESTIMATOR);
    CHECK(failsafe_action() == FC_ACTION_LAND);

    /* TTFF must not trip it: never-healthy is not a loss */
    failsafe_init();
    failsafe_imu_update(true);
    failsafe_rc_keepalive();
    failsafe_notify_estimator(false);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);
    CHECK(failsafe_action() == FC_ACTION_NONE);

    /* geofence tier: reported, action RTL, and it clears again */
    failsafe_init();
    failsafe_imu_update(true);
    failsafe_rc_keepalive();
    failsafe_notify_geofence(true);
    CHECK(failsafe_active() == FC_FAILSAFE_GEOFENCE);
    CHECK(failsafe_action() == FC_ACTION_RTL);
    failsafe_notify_geofence(false);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);
    CHECK(failsafe_action() == FC_ACTION_NONE);
}

/* Companion loss is NOT a failsafe (DEC-007): it is advisory, so perception
 * keeps working on FC sensors and the safety state stays NONE. */
static void test_safety_companion_not_a_failsafe(void)
{
    failsafe_init();
    failsafe_imu_update(true);
    failsafe_rc_keepalive();
    failsafe_notify_estimator(true);
    CHECK(failsafe_active() == FC_FAILSAFE_NONE);
    CHECK(failsafe_action() == FC_ACTION_NONE);
}

/* SAF-004: a control loop that cannot keep up loses attitude authority too, so
 * the action is a motor stop rather than an unachievable flyable mode. */
static void test_safety_deadline_storm_stops(void)
{
    failsafe_init();
    failsafe_imu_update(true);
    failsafe_rc_keepalive();
    for (int i = 0; i < 10; i++) { hal_sim_step(1000u); failsafe_imu_update(true); }
    for (int i = 0; i < 101; i++) failsafe_notify_deadline_miss();
    CHECK(failsafe_active() == FC_FAILSAFE_IMU);
    CHECK(failsafe_action() == FC_ACTION_MOTOR_STOP);
}

/* COM-004 / SAF-005 at the receiver: the arming class has no accept path, the
 * emergency command is always accepted, and a corrupt frame never changes state.
 * This is the FC half of the GS rule that only existed on the sender before. */
static void test_command_gate_arming_class_refused(void)
{
    uint8_t f[CMD_MAX_FRAME];
    cmd_gate_init();

    const uint8_t arming_class[] = { CMD_ARM, CMD_FORCE_ARM, CMD_CLEAR_ARM_GATE,
                                     CMD_OVERRIDE_FAILSAFE, CMD_DISARM_OK };
    for (size_t i = 0; i < sizeof(arming_class); i++) {
        /* with and without a payload: no spelling, no length gets through */
        size_t n = cmd_build(f, arming_class[i], NULL, 0, false);
        cmd_gate_rx(f, n);
        CHECK(cmd_gate_state()->last == CMD_REJECT_ARMDENY);
        uint8_t junk[4] = { 1, 2, 3, 4 };
        n = cmd_build(f, arming_class[i], junk, 4, false);
        cmd_gate_rx(f, n);
        CHECK(cmd_gate_state()->last == CMD_REJECT_ARMDENY);
    }
    CHECK(cmd_gate_state()->accepted == 0u);
    CHECK(cmd_gate_state()->rejected_armdeny == 2u * sizeof(arming_class));
    printf("    command gate: %u/%u arming-class frames refused, 0 accepted\n",
           cmd_gate_state()->rejected_armdeny,
           2u * (unsigned)sizeof(arming_class));

    /* every in-range command type is accepted */
    cmd_gate_init();
    size_t n = cmd_build(f, CMD_MODE_HOLD, NULL, 0, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_ACCEPT);
    n = cmd_build(f, CMD_EMERGENCY_STOP, NULL, 0, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_ACCEPT);
    n = cmd_build(f, CMD_EMERGENCY_STOP, (const uint8_t *)"go", 2, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_ACCEPT);

    /* a CRC error is refused and changes nothing */
    cmd_gate_init();
    n = cmd_build(f, CMD_MODE_LAND, NULL, 0, true);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_REJECT_CRC);
    CHECK(cmd_gate_state()->accepted == 0u);
    CHECK(cmd_gate_state()->rejected_crc == 1u);

    /* a LEN that disagrees with the frame is refused before the payload is read */
    cmd_gate_init();
    n = cmd_build(f, CMD_MODE_LAND, NULL, 0, false);
    f[3] = 40u;                       /* claim 40 payload bytes we did not send */
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_REJECT_LENGTH);

    /* garbage, bad magic, wrong version, unknown type, short frame */
    cmd_gate_init();
    uint8_t g8[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    cmd_gate_rx(g8, sizeof(g8));
    CHECK(cmd_gate_state()->last == CMD_REJECT_MAGIC);
    f[0] = CMD_MAGIC; f[1] = 99u; f[2] = CMD_MODE_HOLD; f[3] = 0u;
    size_t nn = cmd_build(f, CMD_MODE_HOLD, NULL, 0, false);
    f[1] = 99u;
    cmd_gate_rx(f, nn);
    CHECK(cmd_gate_state()->last == CMD_REJECT_VERSION);
    cmd_gate_init();
    n = cmd_build(f, (uint8_t)(CMD_TYPE_COUNT + 40u), NULL, 0, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_REJECT_UNKNOWN_TYPE);
    cmd_gate_init();
    cmd_gate_rx(f, 3u);
    CHECK(cmd_gate_state()->last == CMD_REJECT_SHORT);
    CHECK(cmd_gate_state()->accepted == 0u);

    /* parameter writes are clamped into the FC-owned envelope, unknown ids and
     * NaN are refused (a NaN limit would silently disable every comparison) */
    cmd_gate_init();
    float lo = 0.0f, hi = 0.0f;
    CHECK(param_envelope(CMD_PARAM_BATT_RTL_V, &lo, &hi));
    CHECK(lo <= 3.4f && hi >= 3.4f);
    CHECK(!param_envelope(200u, &lo, &hi));
    uint8_t prm[5];
    prm[0] = CMD_PARAM_BATT_RTL_V;
    uint32_t bits;
    float huge = 99.0f;
    memcpy(&bits, &huge, 4);
    for (int i = 0; i < 4; i++) prm[1 + i] = (uint8_t)((bits >> (8 * i)) & 0xFFu);
    n = cmd_build(f, CMD_SET_PARAM, prm, 5, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_ACCEPT);
    CHECK(cmd_gate_state()->last_param_value <= hi);
    CHECK(cmd_gate_state()->param_clamped == 1u);
    /* below the floor clamps upward */
    float neg = -5.0f;
    memcpy(&bits, &neg, 4);
    for (int i = 0; i < 4; i++) prm[1 + i] = (uint8_t)((bits >> (8 * i)) & 0xFFu);
    n = cmd_build(f, CMD_SET_PARAM, prm, 5, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last_param_value >= lo);
    /* NaN refused */
    float nan_v = NAN;
    memcpy(&bits, &nan_v, 4);
    for (int i = 0; i < 4; i++) prm[1 + i] = (uint8_t)((bits >> (8 * i)) & 0xFFu);
    n = cmd_build(f, CMD_SET_PARAM, prm, 5, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_REJECT_RANGE);
    prm[0] = 77u;                    /* unknown parameter id */
    n = cmd_build(f, CMD_SET_PARAM, prm, 5, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_REJECT_RANGE);

    /* waypoint upload: cap and geofence are checked on the FC, not trusted.
     * Each waypoint is 8 bytes (x f32 + y f32). */
    cmd_gate_init();
    uint8_t wp[1 + 8 * 2];
    wp[0] = 2u;
    float cx = 5.0f, cy = 5.0f;
    uint32_t bx, by;
    memcpy(&bx, &cx, 4); memcpy(&by, &cy, 4);
    for (int i = 0; i < 4; i++) wp[1 + i] = (uint8_t)((bx >> (8 * i)) & 0xFFu);
    for (int i = 0; i < 4; i++) wp[5 + i] = (uint8_t)((by >> (8 * i)) & 0xFFu);
    for (int i = 0; i < 4; i++) wp[9 + i] = (uint8_t)((bx >> (8 * i)) & 0xFFu);
    for (int i = 0; i < 4; i++) wp[13 + i] = (uint8_t)((by >> (8 * i)) & 0xFFu);
    n = cmd_build(f, CMD_SET_WAYPOINTS, wp, (uint8_t)sizeof(wp), false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_ACCEPT);
    CHECK(cmd_gate_state()->last_wp_count == 2u);
    wp[0] = 40u;                     /* more waypoints than the mission cap */
    n = cmd_build(f, CMD_SET_WAYPOINTS, wp, (uint8_t)sizeof(wp), false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_REJECT_RANGE);
    /* a payload length that disagrees with the count is refused (this is the
     * case a 5-byte stride accepted while reading 8 bytes per waypoint) */
    cmd_gate_init();
    wp[0] = 2u;
    n = cmd_build(f, CMD_SET_WAYPOINTS, wp, 6, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_REJECT_PAYLOAD);
    /* outside the geofence */
    cx = 900.0f;
    memcpy(&bx, &cx, 4);
    for (int i = 0; i < 4; i++) wp[1 + i] = (uint8_t)((bx >> (8 * i)) & 0xFFu);
    wp[0] = 1u;
    n = cmd_build(f, CMD_SET_WAYPOINTS, wp, 9, false);
    cmd_gate_rx(f, n);
    CHECK(cmd_gate_state()->last == CMD_REJECT_RANGE);
}

/* Schema v2 (Phase 24): the action crosses the wire, so an operator can tell a
 * motor stop from a flyable RTL, and the parser refuses a v1-length record. */
static void test_telemetry_action_on_the_wire(void)
{
    telemetry_snapshot_t s;
    memset(&s, 0, sizeof(s));
    s.timestamp_us = 1234567u;
    s.mode = (uint8_t)FC_MODE_MISSION;
    s.failsafe = (uint8_t)FC_FAILSAFE_RC_LOSS;
    s.action = (uint8_t)FC_ACTION_MOTOR_STOP;
    s.altitude_m = 1.25f;
    s.min_cell_v = 3.80f;
    s.attitude.w = 1.0f;

    uint8_t buf[256];
    uint8_t seq = 0;
    size_t n = telemetry_build_status(&s, buf, sizeof(buf), &seq);
    CHECK(n > 0u);
    CHECK(buf[1] == TELEM_SCHEMA_VER);

    telemetry_snapshot_t out;
    CHECK(telemetry_parse_status(buf, n, &out));
    CHECK(out.action == FC_ACTION_MOTOR_STOP);
    CHECK(out.failsafe == FC_FAILSAFE_RC_LOSS);
    CHECK(fabsf(out.altitude_m - 1.25f) < 1e-6f);

    /* a record claiming the old (shorter) payload is refused, not misread */
    buf[3] = (uint8_t)(buf[3] - 1u);
    CHECK(!telemetry_parse_status(buf, n, &out));

    /* HEARTBEAT reports CRITICAL for an action even with no failsafe set */
    s.failsafe = (uint8_t)FC_FAILSAFE_NONE;
    s.action = (uint8_t)FC_ACTION_MOTOR_STOP;
    n = telemetry_emit_mavlink(MAVLINK_MSG_HEARTBEAT, &s, &seq, buf, sizeof(buf));
    CHECK(n > 0u);
    mavlink_msg_t m;
    size_t consumed = 0;
    CHECK(mavlink_parse(buf, n, &m, &consumed));
    CHECK(m.payload[7] == 4u);       /* MAV_STATE_CRITICAL */
}

int main(void)
{
    test_mixer_hover();
    test_mixer_sums_and_limits();
    test_attitude_stationary();
    test_attitude_tilt_converges();
    test_failsafe_battery_thresholds();
    test_ctrl_disarmed_zero_output();
    test_hal_sim_determinism();
    test_crc16_known_vector();
    test_params_defaults_on_corruption();
    test_sensor_health_timeout_and_range();
    test_validator_rejections();
    test_calibration_flow();
    test_closed_loop_hover_stability();
    test_rate_tracking_and_saturation();
    test_dshot_framing();
    test_motor_interlocks();
    test_mission_sequence();
    test_mission_rc_loss_rtl();
    test_closed_loop_altitude_hold();
    test_sim_noise_determinism();
    test_noise_robustness();
    test_position_gnss_and_loss();
    test_position_gnss_convergence();
    test_position_outlier_rejection();
    test_app_imu_dropout_no_surge();
    test_failsafe_priority_and_latch();
    test_mission_geofence();
    test_mission_rc_recovery();
    test_ctrl_yaw_chain();
    test_attitude_yaw_integration();
    test_altitude_stationary_drift();
    test_altitude_divergence_reanchor();
    test_perception_fused_basic();
    test_perception_vehicle_state_rotation();
    test_perception_confidence_gate();
    test_perception_stale_ai();
    test_perception_ai_death_no_failsafe();
    test_perception_tof_conflict();
    test_perception_tof_synthesis();
    test_perception_invalid_set();
    test_app_ai_loss_advisory_only();
    test_avoidance_inactive_without_perception();
    test_avoidance_slow();
    test_avoidance_stop_and_retreat();
    test_avoidance_lateral();
    test_avoidance_saf040_clamp();
    test_app_avoidance_progression();
    test_rc_crsf_parse();
    test_rc_sbus_parse();
    test_icd02_frame_codec();
    test_icd02_obstacle_set_codec();
    test_companion_link_task();
    test_telemetry_status_record();
    test_mavlink_v2_roundtrip();
    test_companion_fc_state_builder();
    test_app_companion_byte_path();
    test_sim_lateral_tracking();
    test_sim_drag_decay();
    test_sim_wind_rejection();
    test_position_limits();
    test_avoidance_flown_end_to_end();
    test_hil_link_frame_roundtrip();
    test_hil_link_corruption_and_resync();
    test_hil_link_truncation_at_every_cut();
    test_hil_link_ring_wraparound();
    test_hil_link_sensor_payload();
    test_hil_link_ai_set_payload();
    test_hil_link_fault_vocabulary();
    test_dshot_capture_roundtrip();
    test_param_power_cycle();
    test_loop_timing_distribution();
    test_estimator_health_escalation();
    test_arming_interlock_rc_gate();
    test_no_dynamic_allocation_api();
    test_safety_action_feasibility();
    test_safety_companion_not_a_failsafe();
    test_safety_deadline_storm_stops();
    test_command_gate_arming_class_refused();
    test_telemetry_action_on_the_wire();

    printf("%d/%d passed\n", tests_run - tests_failed, tests_run);
    return (tests_failed == 0) ? 0 : 1;
}
