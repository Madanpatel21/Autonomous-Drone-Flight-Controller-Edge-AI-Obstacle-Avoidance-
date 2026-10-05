#include "hal_interfaces.h"
#include "app_main.h"
#include "est_alt.h"
#include "est_attitude.h"
#include "est_position.h"
#include "failsafe_sm.h"
#include "mission_sm.h"
#include "perception_fusion.h"
#include "obstacle_avoidance.h"
#include "companion_link.h"
#include "telemetry.h"
#include "cmd_gate.h"
#include "sensor_hub.h"
#include "mavlink2.h"
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static void pace_1ms(void) { Sleep(1); }
#else
#include <unistd.h>
static void pace_1ms(void) { usleep(1000); }
#endif

static volatile bool g_running = true;

static void on_sigint(int sig) { (void)sig; g_running = false; }

int main(int argc, char **argv)
{
    signal(SIGINT, on_sigint);

    /* scenario: argv[1] or FC_SIM_SCENARIO env (nominal|imu_dropout|rc_loss|
     * battery_low|gnss_loss|ai_loss|noiseless). Seed: argv[2] or FC_SIM_SEED.
     * FC_SIM_FAST=1 disables real-time pacing (batch runs); FC_SIM_TICKS=N
     * stops after N ticks (deterministic scenario automation, SIM-004). */
    const char *scenario = (argc > 1) ? argv[1] : getenv("FC_SIM_SCENARIO");
    const char *seed_s = (argc > 2) ? argv[2] : getenv("FC_SIM_SEED");
    const char *fast = getenv("FC_SIM_FAST");
    const char *ticks_s = getenv("FC_SIM_TICKS");
    const char *trace = getenv("FC_SIM_TRACE");
    const char *gs_log = getenv("FC_SIM_GS_LOG");   /* ground-station byte log */
    const char *cmd_log = getenv("FC_SIM_CMD_LOG"); /* ground-station command frames */
    unsigned long max_ticks = ticks_s ? strtoul(ticks_s, NULL, 0) : 0;

    hal_sim_reset();
    if (seed_s) hal_sim_set_seed((uint32_t)strtoul(seed_s, NULL, 0));
    if (scenario) {
        hal_sim_scenario(scenario);
        printf("sim: scenario=%s%s\n", scenario, fast ? " fast" : " realtime");
    }

    if (app_init() != FC_OK) {
        fprintf(stderr, "fc_sim: app_init failed\n");
        return 1;
    }

    /* Ground-station COMMAND frames (Phase 24, COM-004/SAF-005): the real
     * FC-side command gate consumes a host-built frame file before the flight
     * loop runs, so the wire policy can be exercised without a command
     * transport (and without ever letting a GS arm the vehicle). One frame per
     * record, records separated by a newline. */
    if (cmd_log && cmd_log[0]) {
        FILE *cf = fopen(cmd_log, "rb");
        if (!cf) {
            fprintf(stderr, "fc_sim: cannot open FC_SIM_CMD_LOG=%s\n", cmd_log);
            return 1;
        }
        uint8_t frame[CMD_MAX_FRAME];
        size_t n = 0;
        int c;
        while ((c = fgetc(cf)) != EOF) {
            if (n < CMD_MAX_FRAME) frame[n++] = (uint8_t)c;
            if (c == '\n' && n > 0u) {
                cmd_gate_rx(frame, n - 1u);   /* newline is a separator, not data */
                n = 0;
            }
        }
        if (n > 0u) cmd_gate_rx(frame, n);   /* last frame without a newline */
        fclose(cf);
        const cmd_gate_t *cg = cmd_gate_state();
        printf("cmdgate: frames_in accepted=%u rejected=%u armdeny=%u crc=%u "
               "len=%u magic=%u ver=%u unknown=%u short=%u range=%u last=%d "
               "clamped=%u\n",
               cg->accepted, cg->rejected, cg->rejected_armdeny, cg->rejected_crc,
               cg->rejected_length, cg->rejected_magic, cg->rejected_version,
               cg->rejected_unknown, cg->rejected_short, cg->rejected_range,
               (int)cg->last, cg->param_clamped);
    }

    /* Ground-station byte log (Phase 20): the SAME bytes a GS would receive
     * over the telemetry UART — MAVLink v2 frames (Phase 19 encoder) and the
     * versioned binary status record — written to a file for offline decode. */
    FILE *gs = NULL;
    uint8_t gs_seq = 0;      /* MAVLink sequence */
    uint8_t telem_seq = 0;   /* status-record sequence (separate counter:
                              * sharing one counter made MAVLink gap
                              * detection meaningless, DEC-015) */
    if (gs_log) {
        gs = fopen(gs_log, "wb");
        if (!gs) {
            fprintf(stderr, "fc_sim: cannot open FC_SIM_GS_LOG=%s\n", gs_log);
            return 1;
        }
    }

    unsigned long tick = 0;
    while (g_running) {
        /* Deterministic 1 kHz tick drives the whole application (SIM-002). */
        hal_sim_step(1000u);
        app_tick_1khz();
        hal_wdg_feed();
        tick++;

        if (!fast) pace_1ms();          /* real-time by default (observability) */

        if (trace && (tick % 2000u) == 0u) {
            float z = 0.0f, vz = 0.0f;
            hal_sim_get_vertical(&z, &vz);
            printf("trace t=%lus true_z=%.2f true_vz=%.2f est_z=%.2f est_vz=%.2f\n",
                   tick / 1000ul, z, vz, est_alt_get_m(), est_alt_vz_ms());
        }

        /* 20 Hz ground-station stream: HEARTBEAT, SYS_STATUS, ATTITUDE and the
         * binary status record, in production byte order. */
        if (gs && (tick % 50u) == 0u) {
            telemetry_snapshot_t snap;
            memset(&snap, 0, sizeof(snap));
            snap.timestamp_us = hal_time_us();
            snap.attitude = est_attitude_quat();
            est_xy_t p = est_position_get();
            snap.vel_ned_ms.x = 0.0f;
            snap.vel_ned_ms.y = 0.0f;
            snap.vel_ned_ms.z = -est_alt_vz_ms();
            snap.altitude_m = est_alt_get_m();
            const fc_battery_sample_t *b = sensor_hub_battery();
            if (b) {
                float minc = b->cell_v[0];
                for (int i = 1; i < 4; i++) {
                    if (b->cell_v[i] < minc) minc = b->cell_v[i];
                }
                snap.min_cell_v = minc;
                snap.current_a = b->current_a;
            }
            snap.mode = (uint8_t)mission_state();
            snap.failsafe = (uint8_t)failsafe_active();
            snap.perception_mode = (uint8_t)perception_get()->mode;
            snap.avoidance_mode = (uint8_t)avoidance_get()->mode;
            snap.companion_healthy = companion_healthy() ? 1u : 0u;
            snap.action = (uint8_t)failsafe_action();

            uint8_t buf[MAVLINK_FRAME_MAX + 128u];
            size_t n = 0;
            n = telemetry_emit_mavlink(MAVLINK_MSG_HEARTBEAT, &snap, &gs_seq,
                                       buf, sizeof(buf));
            if (n) fwrite(buf, 1, n, gs);
            n = telemetry_emit_mavlink(MAVLINK_MSG_SYS_STATUS, &snap, &gs_seq,
                                       buf, sizeof(buf));
            if (n) fwrite(buf, 1, n, gs);
            n = telemetry_emit_mavlink(MAVLINK_MSG_ATTITUDE, &snap, &gs_seq,
                                       buf, sizeof(buf));
            if (n) fwrite(buf, 1, n, gs);
            n = telemetry_build_status(&snap, buf, sizeof(buf), &telem_seq);
            if (n) fwrite(buf, 1, n, gs);
        }
        if (max_ticks && tick >= max_ticks) {
            printf("sim: done %lu ticks\n", tick);
            break;
        }
    }

    if (gs) {
        fflush(gs);
        fclose(gs);
        printf("sim: gs log written to %s\n", gs_log);
    }
    app_shutdown();
    return 0;
}
