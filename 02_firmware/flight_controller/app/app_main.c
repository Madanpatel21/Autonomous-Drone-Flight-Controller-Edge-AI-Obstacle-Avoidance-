#include "app_main.h"
#include "hal_interfaces.h"
#include "est_attitude.h"
#include "est_alt.h"
#include "est_position.h"
#include "ctrl_cascade.h"
#include "ctrl_position.h"
#include "mixer_quadx.h"
#include "motor_output.h"
#include "sensor_hub.h"
#include "failsafe_sm.h"
#include "mission_sm.h"
#include "perception_fusion.h"
#include "obstacle_avoidance.h"
#include "companion_link.h"
#include "telemetry.h"
#include "cmd_gate.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Scheduler per FIRMWARE_ARCHITECTURE.md task table (DEC-004, FW-002).
 * SIM executes one deterministic 1 kHz tick per call; STM32 backend provides
 * the same tick from a hardware timer ISR. Deadlines are monitored (SAF-004). */

#define TICK_HZ        1000u
#define ATT_DIV        4u     /* 250 Hz attitude loop */
#define LOW_DIV        20u    /* 50 Hz low-rate */
#define FAILSAFE_DIV   10u    /* 100 Hz */

typedef struct {
    uint32_t tick;
    uint32_t deadline_misses;
    fc_imu_sample_t imu;
    bool have_imu;
} sched_t;

static sched_t g;

fc_status_t app_init(void)
{
    memset(&g, 0, sizeof(g));
    est_attitude_init();
    ctrl_cascade_init();
    ctrl_position_init();
    motor_output_init();
    sensor_hub_init();
    failsafe_init();
    est_alt_init();
    est_position_init();
    perception_init();
    avoidance_init();
    companion_link_init();
    cmd_gate_init();
    mission_init(NULL);   /* home = (0,0); set at arm in landing phase */
    printf("fc: init ok (HAL v%d)\n", HAL_INTERFACE_VERSION);
    return FC_OK;
}

static void control_path(void)
{
    fc_imu_sample_t s;
    if (sensor_hub_imu(&s)) {
        g.imu = s;
        g.have_imu = true;
    } else {
        /* IMU dropout: do NOT keep feeding the last sample as valid.
         * Phase 15 root cause (SAF-003): the stale sample stayed valid=true,
         * so the zero-output path never ran and est_alt_imu_feed integrated a
         * frozen accel at 1 kHz -> uncommanded climb to 46 m in the
         * imu_dropout scenario. Mark the sample invalid instead so the
         * controller takes the SAF-003 zero-output path and the estimator
         * stops integrating. */
        g.have_imu = false;
        g.imu.valid = false;
    }
    if (g.have_imu) est_attitude_update(&g.imu);
    failsafe_imu_update(g.have_imu);   /* SAF-003 health feed */
    ctrl_cascade_run(&g.imu);   /* invalid sample -> zero outputs (SAF-003) */
}

void app_tick_1khz(void)
{
    uint64_t t0 = hal_time_us();
    g.tick++;

    control_path();

    if (g.tick % ATT_DIV == 0) {
        ctrl_attitude_loop();
    }
    if (g.tick % 10 == 0) {
        ctrl_altitude_loop();   /* 100 Hz altitude hold (Phase 14) */
    }
    if (g.tick % FAILSAFE_DIV == 0) {
        failsafe_monitor();
        motor_output_poll(hal_time_us());   /* stale-command stop (SAF-003) */

        /* companion link RX (100 Hz, DEC-003): real ICD-02 frames -> ring ->
         * CRC -> dispatch; OBSTACLE_SET feeds perception_feed_ai. */
        uint8_t rxbuf[256];
        size_t n_rx = 0;
        if (hal_companion_uart_read(rxbuf, sizeof(rxbuf), &n_rx) > 0 && n_rx > 0u) {
            companion_link_rx(rxbuf, n_rx);
        }
        companion_link_poll(hal_time_us());
    }
    if (g.tick % LOW_DIV == 0) {
        sensor_hub_poll();
        const fc_baro_sample_t *b = sensor_hub_baro();
        if (b) est_alt_update(b);
        const fc_battery_sample_t *bat = sensor_hub_battery();
        if (bat) failsafe_battery_update(bat);

        /* navigation (Phase 14): RC feed, mission tick, altitude setpoint */
        fc_rc_frame_t rc;
        bool rc_ok = (hal_rc_read(&rc) == HAL_OK && rc.valid);
        if (rc_ok) {
            rc_ok = !rc.failsafe_active;
            failsafe_rc_keepalive();
        }
        /* GNSS position update (10 Hz class) + health/age check */
        fc_gnss_sample_t gn;
        if (hal_gnss_read(&gn) == HAL_OK) est_position_gnss(&gn);
        est_position_tick();
        est_xy_t epos = est_position_get();
        est_position_predict(0.02f);       /* 50 Hz predict */

        /* perception fusion (Phase 17, PERC-001..004): companion AI sets +
         * FC forward ToF + vehicle state -> bounded advisory obstacle picture.
         * DEC-007: advisory only; perception loss never escalates failsafe. */
        fc_tof_sample_t ftof;
        if (hal_tof_read(1, &ftof) == HAL_OK && ftof.valid) {
            perception_feed_tof(ftof.range_m, ftof.timestamp_us);
        }
        fc_flow_sample_t ff;
        if (hal_flow_read(&ff) == HAL_OK && ff.valid) {
            perception_feed_flow(&ff);
        }
        fc_quat_t pq = est_attitude_quat();
        perception_set_vehicle_state(&pq, est_alt_get_m());
        perception_tick_50hz(hal_time_us());

        /* obstacle avoidance (Phase 18, SAF-040): bounded advisory velocity
         * setpoint from the perception picture. Advisory only (DEC-007/014):
         * the vehicle mission is not yet overridden (no lateral dynamics in
         * SIM); the command is published for the position/velocity controller
         * integration and is FC-limit-clamped inside the module. */
        avoidance_update(perception_get(), hal_time_us());

        /* horizontal control (Phase 21, DEC-017): the mission target drives
         * the position loop; while avoidance is active its body-frame setpoint
         * takes over (rotated into the world frame by the estimated yaw). */
        nav_xy_t tgt = mission_target_xy();
        ctrl_position_setpoint_xy(tgt.x_m, tgt.y_m);
        const avoid_cmd_t *avc = avoidance_get();
        if (avc->active) {
            float yaw = atan2f(2.0f * (pq.w * pq.z + pq.x * pq.y),
                               1.0f - 2.0f * (pq.y * pq.y + pq.z * pq.z));
            float cy = cosf(yaw), sy = sinf(yaw);
            fc_vec3_t v_world = {
                avc->vel_sp_body_m_s.x * cy - avc->vel_sp_body_m_s.y * sy,
                avc->vel_sp_body_m_s.x * sy + avc->vel_sp_body_m_s.y * cy,
                0.0f
            };
            ctrl_position_vel_override(&v_world, true);
        } else {
            ctrl_position_vel_override(NULL, false);
        }
        ctrl_position_loop(0.02f, &pq);

        /* Position loss is a reported failsafe tier (Phase 24): feed the
         * estimator health to the failsafe module, which latches it so the
         * GNSS 3 s TTFF cannot trip it before the first fix. */
        failsafe_notify_estimator(est_position_healthy());
        /* Geofence tier (NAV-004/SAF-001): a breach must be visible to the
         * operator on the wire, not just as a mission transition. */
        failsafe_notify_geofence(mission_geofence_violated());

        /* Safety action by flyability (Phase 24 / DEC-021, SAF-001/002/003).
         * Before this the app reacted to the failsafe NAME, so RC loss claimed
         * RTL even when the IMU had also failed and no control authority
         * existed at all. */
        fc_safety_action_t act = failsafe_action();
        if (act == FC_ACTION_MOTOR_STOP) {
            /* No flyable action: attitude cannot be stabilised, so thrust is
             * cut and the mission stops commanding (SAF-003). */
            ctrl_set_armed(false);
            mission_abort();
        } else if (act == FC_ACTION_LAND) {
            /* Battery-critical (SAF-030) or estimator loss: descend in place.
             * RTL is not flyable without a position estimate, so LAND is the
             * bounded action for both (SAF-002). */
            mission_land_now();
        } else if (act == FC_ACTION_RTL) {
            /* RC loss / geofence / latched RTL band: the mission SM reacts to
             * rc_ok==false on its own; an explicit request keeps the intent in
             * one place instead of relying on that side effect. */
            mission_request_rtl();
        }

        nav_xy_t pos = { epos.x_m, epos.y_m };
        mission_tick(pos, est_alt_get_m(), rc_ok);

        if (mission_state() == NAV_TAKEOFF || mission_state() == NAV_LAND ||
            mission_state() == NAV_WAYPOINT || mission_state() == NAV_RTL) {
            float asp = mission_altitude_sp();
            if (asp == asp) ctrl_set_altitude_sp(asp);   /* NaN check */
        }

        /* demo/auto start: arm switch high starts the mission once (SIM + bench) */
        if (rc_ok && rc.channels[4] > 1500 && mission_state() == NAV_IDLE) {
            ctrl_set_armed(true);
            mission_start();
        }

        /* state-change diagnostics (flight log line per transition) */
        static nav_state_t last_state = NAV_IDLE;
        if (mission_state() != last_state) {
            last_state = mission_state();
            printf("nav: %s alt_sp=%.2f alt=%.2f vz=%.2f\n",
                   mission_state_name(), mission_altitude_sp(),
                   est_alt_get_m(), est_alt_vz_ms());
        }

        /* safety diagnostics (state-change lines only): WHY control was taken
         * over and WHAT the vehicle can still do (Phase 24). Before this the
         * log only ever showed nav states, so an operator reading a flight log
         * could not tell a flyable RTL from a motor stop. */
        static fc_failsafe_t last_fs = FC_FAILSAFE_NONE;
        static fc_safety_action_t last_act = FC_ACTION_NONE;
        fc_failsafe_t dbg_fs = failsafe_active();
        fc_safety_action_t dbg_act = failsafe_action();
        if (dbg_fs != last_fs || dbg_act != last_act) {
            last_fs = dbg_fs;
            last_act = dbg_act;
            printf("safety: failsafe=%d action=%d\n", (int)dbg_fs, (int)dbg_act);
        }

        /* perception mode diagnostics (state-change lines only) */
        static perception_mode_t last_pmode = PERCEPTION_NONE;
        const perception_state_t *pv = perception_get();
        if (pv->mode != last_pmode) {
            last_pmode = pv->mode;
            printf("perception: mode=%d count=%u min_range=%.2f ai_ok=%d tof_ok=%d\n",
                   (int)pv->mode, pv->count, pv->min_range_m,
                   pv->ai_healthy, pv->tof_healthy);
        }

        /* avoidance mode diagnostics (state-change lines only) */
        static avoid_mode_t last_amode = AVOID_NONE;
        const avoid_cmd_t *av = avoidance_get();
        if (av->mode != last_amode) {
            last_amode = av->mode;
            printf("avoidance: mode=%d threat=%.2f brg=%.2f v=(%.2f, %.2f)\n",
                   (int)av->mode, av->threat_range_m, av->threat_bearing_rad,
                   av->vel_sp_body_m_s.x, av->vel_sp_body_m_s.y);
        }
    }

    uint64_t dt = hal_time_us() - t0;
    if (dt > 900u) {           /* deadline check (SAF-004) */
        g.deadline_misses++;
        failsafe_notify_deadline_miss();
    }
}

void app_shutdown(void)
{
    printf("fc: shutdown. ticks=%u deadline_misses=%u\n", g.tick, g.deadline_misses);
}
