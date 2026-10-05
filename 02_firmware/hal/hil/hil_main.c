#include "hal_interfaces.h"
#include "hal_hil.h"
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
#include "sensor_hub.h"
#include "motor_output.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static uint64_t now_ms(void) { return (uint64_t)GetTickCount64(); }
#else
#include <time.h>
#include <unistd.h>
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}
#endif

/* HIL entry point (Phase 22, DEC-019): the REAL flight-control application with
 * the world on the far side of the HIL link. One control tick per rig exchange:
 *
 *   1. send the actuator frame produced by motor_output (production DShot600)
 *   2. run app_tick_1khz() - it reads cached sensors, never the socket
 *   3. wait for / consume the rig's sensor frame and dispatch it into the cache
 *   4. feed the emulated watchdog
 *
 * CLI (all optional):
 *   --host H --port P     rig endpoint (default 127.0.0.1:45551)
 *   --scenario NAME       nominal|imu_dropout|rc_loss|battery_low|gnss_loss|
 *                         ai_loss|noiseless (scheduled IN FLIGHT on the rig)
 *   --fault NAME@AT_US    repeatable immediate/scheduled fault injection
 *   --ticks N             stop after N control ticks (default 30000)
 *   --seed N              forwarded to the rig
 *   --wdg-starve          stop feeding the watchdog (SAF-004 negative test)
 *   --report              print the metric report only
 * Exit: 0 ok, 1 setup/link failure, 2 app failure, 3 watchdog expired. */

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    unsigned port = 45551u;
    const char *scenario = NULL;
    unsigned long ticks = 30000ul;
    bool starve = false;
    const char *faults[8];
    unsigned long fault_at[8];
    int n_faults = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);   /* progress must survive a crash/abort */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) {
            scenario = argv[++i];
        } else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) {
            ticks = strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--fault") && i + 1 < argc && n_faults < 8) {
            /* NAME or NAME@AT_US */
            static char names[8][32];
            const char *spec = argv[++i];
            const char *at = strchr(spec, '@');
            unsigned long when_us = 0;
            size_t nlen = at ? (size_t)(at - spec) : strlen(spec);
            if (nlen >= sizeof(names[0])) nlen = sizeof(names[0]) - 1u;
            memcpy(names[n_faults], spec, nlen);
            names[n_faults][nlen] = '\0';
            if (at) when_us = strtoul(at + 1, NULL, 10);
            faults[n_faults] = names[n_faults];
            fault_at[n_faults] = when_us;
            n_faults++;
        } else if (!strcmp(argv[i], "--wdg-starve")) {
            starve = true;
        } else {
            fprintf(stderr, "fc_hil: unknown option %s\n", argv[i]);
            return 1;
        }
    }

    if (hil_link_init(host, (uint16_t)port, 10000u) != 0) {
        fprintf(stderr, "fc_hil: cannot reach rig at %s:%u\n", host, port);
        return 1;
    }
    printf("fc_hil: link up to %s:%u (host clock master)\n", host, port);

    if (scenario) {
        hil_link_scenario(scenario);
        printf("fc_hil: scenario=%s\n", scenario);
    }
    for (int i = 0; i < n_faults; i++) {
        if (fault_at[i] > 0u) hil_link_schedule_fault(faults[i], fault_at[i]);
        else                 hil_link_fault(faults[i], true);
        printf("fc_hil: fault %s at %lu us\n", faults[i], fault_at[i]);
    }

    if (app_init() != FC_OK) {
        fprintf(stderr, "fc_hil: app_init failed\n");
        hil_link_shutdown();
        return 2;
    }
    hal_wdg_init(500u);   /* SAF-004: 500 ms, matches the STM32 IWDG intent */

    if (!hil_link_wait_sensor(10000u)) {
        fprintf(stderr, "fc_hil: no sensor frame from rig\n");
        hil_link_shutdown();
        return 1;
    }

    uint64_t t_wall0 = now_ms();
    for (unsigned long tick = 0; tick < ticks; tick++) {
        uint64_t before = hil_link_time_us();

        /* control tick: reads cached sensors only (no socket in the loop) */
        app_tick_1khz();

        if (!starve) hal_wdg_feed();

        /* consume the rig's answer for this tick */
        uint64_t waited = 0;
        bool got = false;
        while (waited < 200000u) {
            hil_link_poll();
            if (hil_link_time_us() != before) { got = true; break; }
            hil_link_wait_io(1);
            waited += 1000u;
        }
        hil_link_tick(got);

        if (tick % 2000u == 0u) {
            printf("trace t=%lums est_alt=%.2f alt_sp=%.2f nav=%s avoid=%d\n",
                   (unsigned long)(hil_link_time_us() / 1000ul),
                   est_alt_get_m(), mission_altitude_sp(),
                   mission_state_name(), (int)avoidance_get()->mode);
        }
    }

    uint64_t wall_ms = now_ms() - t_wall0;
    hil_link_set_wall_elapsed_ms(wall_ms);

    /* correlation snapshot: rig truth vs FC estimate (HIL-4 style point) */
    hil_link_request_world();
    for (int i = 0; i < 200; i++) {
        hil_link_poll();
        uint64_t t;
        float w[11];
        if (hil_link_world(&t, w)) break;
        hil_link_wait_io(1);
    }
    {
        uint64_t t = 0;
        float w[11];
        if (hil_link_world(&t, w)) {
            est_xy_t p = est_position_get();
            float err_xy = (float)sqrt((p.x_m - w[0]) * (p.x_m - w[0]) +
                                        (p.y_m - w[1]) * (p.y_m - w[1]));
            printf("hil_fc: corr t=%lums true=(%.2f, %.2f, %.2f) est=(%.2f, %.2f, %.2f) "
                   "err_xy=%.3f err_alt=%.3f obst_range=%.2f motors=(%.3f %.3f %.3f %.3f)\n",
                   (unsigned long)(t / 1000ul), w[0], w[1], w[2],
                   p.x_m, p.y_m, est_alt_get_m(),
                   err_xy, est_alt_get_m() - w[2], w[6], w[7], w[8], w[9], w[10]);
        } else {
            printf("hil_fc: corr unavailable (no rig reply)\n");
        }
    }

    app_shutdown();
    hil_link_report();
    hil_stats_t st;
    hil_link_stats(&st);
    printf("hil_fc: wall_ticks_per_s=%u\n",
           wall_ms ? (unsigned)((ticks * 1000ull) / wall_ms) : 0u);

    int rc = 0;
    if (st.wdt_expiries) {
        printf("hil_fc: FAIL watchdog expired %u time(s) - SAF-004 interlock armed\n",
               st.wdt_expiries);
        rc = 3;
    }
    if (st.link_stalls || st.tx_drops) rc = 1;
    hil_link_shutdown();
    return rc;
}