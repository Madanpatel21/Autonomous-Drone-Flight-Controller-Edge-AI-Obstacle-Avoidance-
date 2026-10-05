/* Host-side HIL rig (Phase 22, DEC-019).
 *
 * The rig is the far side of the HIL link and the CLOCK MASTER: it owns the
 * vehicle, the sensor environment and the virtual time. It runs the SAME
 * deterministic world model as the SIM (hal/sim/sim_model.c) so a HIL run and a
 * SIM run cannot drift apart, and it drives that world only from the DShot600
 * frames it decodes out of the actuator messages - the same causal chain as a
 * real rig (FC -> ESC signal -> vehicle -> sensors -> FC).
 *
 * WHAT THIS IS NOT: this is a host-side rig, not hardware. It proves the link,
 * the sensor-rate fidelity, the fault-injection path, the DShot bit stream and
 * the safety interlocks. It says nothing about an STM32's real timing or buses
 * (HIL-1..HIL-6 remain H-gated - see HIL_DESIGN.md).
 *
 * CLI:
 *   --port N            TCP port to listen on (default 45551; 0 = ephemeral)
 *   --scenario NAME     nominal|imu_dropout|rc_loss|battery_low|gnss_loss|
 *                       ai_loss|noiseless (faults scheduled IN FLIGHT)
 *   --seed N            world PRNG seed
 *   --ticks N           stop after N control ticks (default 30000)
 *   --fault NAME@AT_US  repeatable scheduled fault injection
 *   --capture FILE      binary DShot capture (what the "ESC" saw)
 *   --trace FILE        CSV world/estimate correlation trace
 *   --seed-noise on|off sensor noise on (default) / off
 *   --corrupt-every N   corrupt 1 payload byte in every Nth sensor frame
 *   --drop-every N      drop every Nth sensor frame entirely (link loss)
 *   --wdg-block         answer the FC's watchdog feed with a timeout event
 * Exit: 0 ok, 1 bind/accept failure, 2 protocol error.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
static void rig_sleep_200us(void) { usleep(200); }
static uint64_t rig_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int socklen_t;
#define close_socket closesocket
#define WSA_LAST_ERROR WSAGetLastError()
static uint64_t rig_now_ms(void) { return (uint64_t)GetTickCount64(); }
#endif

#include "hil_link.h"
#include "sim_model.h"
#include "esc_dshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RIG_FLASH_SIZE 4096u

/* sensor output rates (real classes; the FC must see these ages, HIL-2/HIL-3) */
#define RATE_HZ_IMU   1000u
#define RATE_HZ_BARO    50u
#define RATE_HZ_GNSS    10u
#define RATE_HZ_TOF0    50u
#define RATE_HZ_TOF1    25u
#define RATE_HZ_BATT    10u
#define RATE_HZ_RC     100u

typedef struct {
    /* cached sensor values + the rig timestamp each was produced at */
    fc_imu_sample_t     imu;      bool imu_ok;
    fc_baro_sample_t    baro;     bool baro_ok;
    fc_gnss_sample_t    gnss;     bool gnss_ok;
    fc_tof_sample_t     tof[2];   bool tof_ok[2];
    fc_battery_sample_t batt;     bool batt_ok;
    fc_rc_frame_t       rc;       bool rc_ok;
    uint64_t t_us[HIL_SENSOR_COUNT];
    bool have[HIL_SENSOR_COUNT];
} rig_sensors_t;

typedef struct {
    int      listen_fd, client_fd;
    hil_ring_t rx;
    uint8_t  tx[16384];
    uint32_t tx_head, tx_tail;
    uint8_t  seq_tx;          /* ONE counter per direction (see hil_link.h) */
    uint8_t  flash[RIG_FLASH_SIZE];
    /* metrics */
    uint32_t ticks;
    uint32_t sensor_frames, ai_frames, companion_frames;
    uint32_t crc_errors, seq_gaps, resyncs, overruns;
    uint32_t frames_sent, frames_rx;
    uint32_t wire_rx, wire_tx;
    uint32_t corrupt_injected, frames_dropped;
    uint32_t corrupt_every;        /* wire-corruption injection period (frames) */
    uint32_t ticks_await_fc;         /* ticks without a valid actuator frame */
    uint64_t fault_apply_us;          /* last fault application time */
    uint64_t fault_next_frame_us;     /* when it first became visible */
    uint32_t dshot_crc_err;           /* actuator frames a real ESC would reject */
    uint32_t dshot_frames;
    float    motor_max;
    FILE    *capture, *trace;
} rig_t;

static rig_t g;

/* ---------------- socket helpers ---------------- */

static void tx_push(const uint8_t *d, size_t len)
{
    uint32_t cap = (uint32_t)sizeof(g.tx);
    uint32_t space = cap - 1u - ((g.tx_head - g.tx_tail) & (cap - 1u));
    if (len > space) return;
    for (size_t i = 0; i < len; i++) {
        g.tx[g.tx_head] = d[i];
        g.tx_head = (g.tx_head + 1u) & (cap - 1u);
    }
}

static void tx_frame(uint8_t type, uint8_t seq, const uint8_t *pl, uint16_t len)
{
    uint8_t buf[HIL_LINK_MAX_FRAME];
    size_t n = hil_link_encode(type, seq, 0u, sim_model_time_us(), pl, len,
                               buf, sizeof(buf));
    if (!n) return;
    g.frames_sent++;
    /* Link corruption injection: flip one bit of the CRC field AFTER encoding,
     * i.e. real wire corruption. (Corrupting the payload before the encode
     * produced a frame with a VALID CRC - the peer accepted it and the "corrupt
     * frame" silently became a wrong sensor value. Found on the first
     * robustness run.) */
    if (g.corrupt_every && (g.frames_sent % g.corrupt_every) == 0u) {
        buf[n - 1u] ^= 0x08u;
        g.corrupt_injected++;
    }
    tx_push(buf, n);
}

static void tx_flush(void)
{
    while (g.tx_tail != g.tx_head) {
        uint32_t chunk = (g.tx_head > g.tx_tail)
                       ? (g.tx_head - g.tx_tail)
                       : ((uint32_t)sizeof(g.tx) - g.tx_tail);
#ifndef _WIN32
        ssize_t n = send(g.client_fd, g.tx + g.tx_tail, chunk, MSG_NOSIGNAL);
        int err = errno;
#else
        int n = send(g.client_fd, (const char *)(g.tx + g.tx_tail), (int)chunk, 0);
        int err = WSA_LAST_ERROR;
#endif
    if (n > 0) {
#ifdef HIL_DEBUG_TRACE
        if (g.wire_tx < 400u) {
            fprintf(stderr, "[rig] sent %d bytes (total %u)\n", (int)n, g.wire_tx);
        }
#endif
        g.wire_tx += (uint32_t)n;
            g.tx_tail = (g.tx_tail + (uint32_t)n) & ((uint32_t)sizeof(g.tx) - 1u);
            continue;
        }
#ifndef _WIN32
        if (err == EAGAIN || err == EWOULDBLOCK) return;
#else
        if (err == WSAEWOULDBLOCK) return;
#endif
        return;
    }
}

/* Wait until the FC has data (select-based: Sleep(1) quantises to ~15.6 ms on
 * Windows, which would dominate the rig's response time). */
static void rig_wait_readable(int fd, int ms)
{
    fd_set rf;
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    FD_ZERO(&rf);
#ifndef _WIN32
    FD_SET(fd, &rf);
#else
    FD_SET((SOCKET)fd, &rf);
#endif
    select(fd + 1, &rf, NULL, NULL, &tv);
}

static void rx_pump(void)
{
    uint8_t buf[4096];
    for (;;) {
#ifndef _WIN32
        ssize_t n = recv(g.client_fd, buf, sizeof(buf), 0);
#else
        int n = recv(g.client_fd, (char *)buf, (int)sizeof(buf), 0);
#endif
        if (n > 0) {
#ifdef HIL_DEBUG_TRACE
            if (g.wire_rx < 200u) {
                fprintf(stderr, "[rig] recv %d bytes (total %u)\n", (int)n, g.wire_rx);
            }
#endif
            g.wire_rx += (uint32_t)n;
            hil_ring_push(&g.rx, buf, (size_t)n);
            if ((size_t)n < sizeof(buf)) break;
            continue;
        }
        break;
    }
}

/* ---------------- sensor production ---------------- */

/* A sensor is only refreshed at its real output rate; between refreshes the rig
 * keeps the old value AND its old timestamp, so the FC observes the true sample
 * age (a 10 Hz GNSS must look 10 Hz). */
static bool due(uint64_t t_us, unsigned hz, uint64_t period_us)
{
    (void)t_us;
    (void)hz;
    return (sim_model_time_us() % period_us) == 0u;
}

static void refresh_sensors(rig_sensors_t *s)
{
    uint64_t t = sim_model_time_us();
    uint32_t tick_no = (uint32_t)(t / 1000u);

    if (sim_model_read_imu(&s->imu) == HAL_OK) { s->imu_ok = true; s->t_us[HIL_SIDX_IMU] = s->imu.timestamp_us; s->have[HIL_SIDX_IMU] = true; }
    else s->imu_ok = false;

    if (due(t, RATE_HZ_BARO, 1000000u / RATE_HZ_BARO)) {
        s->baro_ok = (sim_model_read_baro(&s->baro) == HAL_OK);
        if (s->baro_ok) { s->t_us[HIL_SIDX_BARO] = s->baro.timestamp_us; s->have[HIL_SIDX_BARO] = true; }
    }
    if (due(t, RATE_HZ_GNSS, 1000000u / RATE_HZ_GNSS)) {
        s->gnss_ok = (sim_model_read_gnss(&s->gnss) == HAL_OK);
        if (s->gnss_ok) { s->t_us[HIL_SIDX_GNSS] = s->gnss.timestamp_us; s->have[HIL_SIDX_GNSS] = true; }
    }
    if (due(t, RATE_HZ_TOF0, 1000000u / RATE_HZ_TOF0)) {
        s->tof_ok[0] = (sim_model_read_tof(0, &s->tof[0]) == HAL_OK);
        if (s->tof_ok[0]) { s->t_us[HIL_SIDX_TOF0] = s->tof[0].timestamp_us; s->have[HIL_SIDX_TOF0] = true; }
    }
    if (due(t, RATE_HZ_TOF1, 1000000u / RATE_HZ_TOF1)) {
        s->tof_ok[1] = (sim_model_read_tof(1, &s->tof[1]) == HAL_OK);
        if (s->tof_ok[1]) { s->t_us[HIL_SIDX_TOF1] = s->tof[1].timestamp_us; s->have[HIL_SIDX_TOF1] = true; }
    }
    if (due(t, RATE_HZ_BATT, 1000000u / RATE_HZ_BATT)) {
        s->batt_ok = (sim_model_read_battery(&s->batt) == HAL_OK);
        if (s->batt_ok) { s->t_us[HIL_SIDX_BATT] = s->batt.timestamp_us; s->have[HIL_SIDX_BATT] = true; }
    }
    if (tick_no % (1000u / RATE_HZ_RC) == 0u) {
        s->rc_ok = (sim_model_read_rc(&s->rc) == HAL_OK);
        if (s->rc_ok) { s->t_us[HIL_SIDX_RC] = s->rc.timestamp_us; s->have[HIL_SIDX_RC] = true; }
    }
}

static void build_sensor_frame(rig_sensors_t *s, hil_sensor_frame_t *f)
{
    memset(f, 0, sizeof(*f));
    f->t_us = sim_model_time_us();
    f->avail = 0;
    if (s->imu_ok) {
        f->avail |= HIL_SENSOR_IMU;
        f->accel_m_s2 = s->imu.accel_m_s2;
        f->gyro_rad_s = s->imu.gyro_rad_s;
        if (s->imu.valid) f->stat |= HIL_STAT_IMU_VALID;
    }
    if (s->baro_ok) {
        f->avail |= HIL_SENSOR_BARO;
        f->pressure_pa = s->baro.pressure_pa;
        f->temperature_c = s->baro.temperature_c;
        f->stat |= HIL_STAT_BARO_VALID;
    }
    if (s->gnss_ok) {
        f->avail |= HIL_SENSOR_GNSS;
        f->lat_deg = s->gnss.lat_deg;
        f->lon_deg = s->gnss.lon_deg;
        f->alt_m = s->gnss.alt_m;
        f->vn_ms = s->gnss.vn_ms; f->ve_ms = s->gnss.ve_ms; f->vd_ms = s->gnss.vd_ms;
        f->sats = s->gnss.sats;
        if (s->gnss.fix_valid) f->stat |= HIL_STAT_GNSS_FIX;
    }
    if (s->tof_ok[0]) { f->avail |= HIL_SENSOR_TOF0; f->tof0_m = s->tof[0].range_m; }
    if (s->tof_ok[1]) { f->avail |= HIL_SENSOR_TOF1; f->tof1_m = s->tof[1].range_m; }
    if (s->batt_ok) {
        f->avail |= HIL_SENSOR_BATTERY;
        f->pack_v = s->batt.pack_v;
        f->current_a = s->batt.current_a;
        for (int i = 0; i < 4; i++) f->cell_v[i] = s->batt.cell_v[i];
        f->consumed_mah = sim_model_battery_used_mah();
    }
    if (s->rc_ok) {
        f->avail |= HIL_SENSOR_RC;
        for (int i = 0; i < 8; i++) f->rc_ch[i] = s->rc.channels[i];
        f->stat |= HIL_STAT_RC_VALID;
    }
    for (unsigned i = 0; i < HIL_SENSOR_COUNT; i++) f->t_sample_ms[i] = (uint32_t)(s->t_us[i] / 1000u);
}

/* ---------------- message handling ---------------- */

static void reply_world(void)
{
    uint8_t pl[44];
    float x, y, z, vx, vy, vz, rng, mm[4];
    sim_model_get_horizontal(&x, &y);
    sim_model_get_vertical(&z, &vz);
    sim_model_get_horizontal_vel(&vx, &vy);
    rng = sim_model_obstacle_range_m();
    sim_model_get_motor(mm);
    float v[11] = { x, y, z, vx, vy, vz, rng, mm[0], mm[1], mm[2], mm[3] };
    memcpy(pl, v, sizeof(v));
    tx_frame(HIL_MSG_WORLD, g.seq_tx++, pl, sizeof(pl));
    tx_flush();
}

static void handle_flash(const hil_msg_t *m)
{
    if (m->len < 7u) return;
    uint8_t op = m->payload[0];
    uint32_t addr = 0;
    for (int i = 0; i < 4; i++) addr |= (uint32_t)m->payload[1 + i] << (8 * i);
    uint32_t len = (uint32_t)m->payload[5] | ((uint32_t)m->payload[6] << 8);

    if (op == 0u) {                       /* read */
        if (addr + len > RIG_FLASH_SIZE) len = 0;
        uint8_t out[HIL_LINK_MAX_PAYLOAD];
        memcpy(out, g.flash + addr, len);
        uint8_t hdr[7];
        hdr[0] = op;
        for (int i = 0; i < 4; i++) hdr[1 + i] = (uint8_t)(addr >> (8 * i));
        hdr[5] = (uint8_t)(len & 0xFFu);
        hdr[6] = (uint8_t)(len >> 8);
        memcpy(out + 7, hdr, 7);
        /* reply: flash payload with the response flag set */
        uint8_t buf[HIL_LINK_MAX_FRAME];
        size_t n = hil_link_encode(HIL_MSG_FLASH, g.seq_tx++, 0x01u,
                                   sim_model_time_us(), out, (uint16_t)(7u + len),
                                   buf, sizeof(buf));
        if (n) { tx_push(buf, n); g.frames_sent++; tx_flush(); }
        return;
    }
    if (op == 1u) {                       /* write */
        if (m->len >= 7u + len && addr + len <= RIG_FLASH_SIZE) {
            memcpy(g.flash + addr, m->payload + 7, len);
        }
        tx_frame(HIL_MSG_ACK, g.seq_tx++, NULL, 0);
        tx_flush();
        return;
    }
    if (op == 2u) {                       /* erase */
        memset(g.flash, 0xFF, RIG_FLASH_SIZE);
        tx_frame(HIL_MSG_ACK, g.seq_tx++, NULL, 0);
        tx_flush();
        return;
    }
}

/* Returns true when the message was an actuator tick (the world advances). */
static bool handle_msg(const hil_msg_t *m)
{
    switch (m->type) {
    case HIL_MSG_TICK: {
        if (m->len < 9u) return false;
        float unit[4];
        bool armed = (m->payload[8] != 0u);
        bool any_crc_err = false;
        for (int i = 0; i < 4; i++) {
            uint16_t frame = (uint16_t)(m->payload[2 * i] | ((uint16_t)m->payload[2 * i + 1] << 8));
            uint16_t thr = 0;
            bool tel = false;
            g.dshot_frames++;
            if (!dshot_decode_frame(frame, &thr, &tel)) {
                any_crc_err = true;          /* a real ESC would drop this motor */
                unit[i] = 0.0f;
            } else {
                unit[i] = dshot_unit_from_throttle(thr);
            }
            if (unit[i] > g.motor_max) g.motor_max = unit[i];
            if (g.capture) {
                uint8_t rec[12];
                uint64_t t = sim_model_time_us();
                for (int b = 0; b < 8; b++) rec[b] = (uint8_t)(t >> (8 * b));
                rec[8]  = (uint8_t)(frame & 0xFFu);
                rec[9]  = (uint8_t)(frame >> 8);
                rec[10] = (uint8_t)(armed ? 1u : 0u);
                rec[11] = (uint8_t)(any_crc_err ? 1u : 0u);
                fwrite(rec, 1, sizeof(rec), g.capture);
            }
        }
        if (any_crc_err) g.dshot_crc_err++;
        sim_model_set_motor(unit);
        return true;
    }
    case HIL_MSG_FAULT: {
        if (m->len >= 2u) {
            const char *name = hil_fault_id_to_name(m->payload[0]);
            sim_model_fault(name, m->payload[1] != 0u);
            g.fault_apply_us = sim_model_last_fault_us();
            g.fault_next_frame_us = 0u;
            printf("rig: fault %s %s at %llu us\n", name,
                   m->payload[1] ? "set" : "clear",
                   (unsigned long long)g.fault_apply_us);
        }
        return false;
    }
    case HIL_MSG_SCHED_FAULT: {
        if (m->len >= 9u) {
            const char *name = hil_fault_id_to_name(m->payload[0]);
            uint64_t at = 0;
            for (int i = 0; i < 8; i++) at |= (uint64_t)m->payload[1 + i] << (8 * i);
            sim_model_schedule_fault(name, at);
            printf("rig: scheduled fault %s at %llu us\n", name,
                   (unsigned long long)at);
        }
        return false;
    }
    case HIL_MSG_SCENARIO: {
        char name[32];
        uint16_t n = m->len < 31u ? m->len : 31u;
        memcpy(name, m->payload, n);
        name[n] = '\0';
        sim_model_scenario(name);
        printf("rig: scenario=%s\n", name);
        return false;
    }
    case HIL_MSG_FLASH:
        handle_flash(m);
        return false;
    case HIL_MSG_ACK:
        reply_world();
        return false;
    case HIL_MSG_SHUTDOWN:
        printf("rig: shutdown requested by FC\n");
        return false;
    default:
        return false;
    }
}

int main(int argc, char **argv)
{
    unsigned port = 45551u;
    unsigned long ticks_max = 30000ul;
    const char *scenario = NULL;
    const char *capture_path = NULL;
    const char *trace_path = NULL;
    unsigned long corrupt_every = 0, drop_every = 0;
    unsigned long idle_timeout_ms = 15000ul;
    bool noise = true;
    struct { const char *name; uint64_t at; } sched[8];
    int n_sched = 0;

    memset(&g, 0, sizeof(g));
    g.listen_fd = g.client_fd = -1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) {
            ticks_max = strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) {
            scenario = argv[++i];
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            sim_model_set_seed((uint32_t)strtoul(argv[++i], NULL, 10));
        } else if (!strcmp(argv[i], "--capture") && i + 1 < argc) {
            capture_path = argv[++i];
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            trace_path = argv[++i];
        } else if (!strcmp(argv[i], "--corrupt-every") && i + 1 < argc) {
            corrupt_every = (uint32_t)strtoul(argv[++i], NULL, 10);
            g.corrupt_every = corrupt_every;
        } else if (!strcmp(argv[i], "--drop-every") && i + 1 < argc) {
            drop_every = strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--idle-timeout-ms") && i + 1 < argc) {
            idle_timeout_ms = strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--seed-noise") && i + 1 < argc) {
            noise = strcmp(argv[++i], "off") != 0;
        } else if (!strcmp(argv[i], "--fault") && i + 1 < argc && n_sched < 8) {
            static char names[8][32];
            const char *spec = argv[++i];
            const char *at = strchr(spec, '@');
            size_t nl = at ? (size_t)(at - spec) : strlen(spec);
            if (nl >= sizeof(names[0])) nl = sizeof(names[0]) - 1u;
            memcpy(names[n_sched], spec, nl);
            names[n_sched][nl] = '\0';
            sched[n_sched].name = names[n_sched];
            sched[n_sched].at = at ? strtoul(at + 1, NULL, 10) : 0u;
            n_sched++;
        } else {
            fprintf(stderr, "hil_rig: unknown option %s\n", argv[i]);
            return 1;
        }
    }

    sim_model_reset();
    sim_model_set_noise(noise);
    if (scenario) sim_model_scenario(scenario);
    for (int i = 0; i < n_sched; i++) {
        if (sched[i].at) sim_model_schedule_fault(sched[i].name, sched[i].at);
        else             sim_model_fault(sched[i].name, true);
    }
    memset(g.flash, 0xFF, sizeof(g.flash));

    if (capture_path) {
        g.capture = fopen(capture_path, "wb");
        if (!g.capture) { fprintf(stderr, "hil_rig: cannot open capture %s\n", capture_path); return 1; }
    }
    if (trace_path) {
        g.trace = fopen(trace_path, "w");
        if (!g.trace) { fprintf(stderr, "hil_rig: cannot open trace %s\n", trace_path); return 1; }
        fprintf(g.trace, "t_us,x_m,y_m,z_m,vx,vy,vz,obst_range,m0,m1,m2,m3\n");
    }

#ifdef _WIN32
    { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); }
#endif
    g.listen_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (g.listen_fd < 0) { fprintf(stderr, "hil_rig: socket failed\n"); return 1; }
    int one = 1;
    setsockopt(g.listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g.listen_fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        fprintf(stderr, "hil_rig: bind %u failed\n", port);
        return 1;
    }
    if (listen(g.listen_fd, 1) != 0) { fprintf(stderr, "hil_rig: listen failed\n"); return 1; }

    /* report the bound port so a caller using --port 0 can find the rig */
    struct sockaddr_in bound;
    socklen_t bl = sizeof(bound);
    getsockname(g.listen_fd, (struct sockaddr *)&bound, &bl);
    unsigned bound_port = ntohs(bound.sin_port);
    printf("rig: listening on 127.0.0.1:%u scenario=%s ticks=%lu noise=%d\n",
           bound_port, scenario ? scenario : "nominal", ticks_max, (int)noise);
    fflush(stdout);

    g.client_fd = (int)accept(g.listen_fd, NULL, NULL);
    if (g.client_fd < 0) { fprintf(stderr, "hil_rig: accept failed\n"); return 1; }
#ifndef _WIN32
    int flags = fcntl(g.client_fd, F_GETFL, 0);
    fcntl(g.client_fd, F_SETFL, flags | O_NONBLOCK);
#else
    u_long nb = 1;
    ioctlsocket((SOCKET)g.client_fd, FIONBIO, &nb);
#endif
    setsockopt(g.client_fd, IPPROTO_TCP, 1 /* TCP_NODELAY */,
               (const char *)&one, sizeof(one));
    printf("rig: FC connected\n");
    fflush(stdout);

    hil_ring_reset(&g.rx);
    rig_sensors_t sens;
    memset(&sens, 0, sizeof(sens));

    /* Initial sensor frame at t=0 BEFORE the first actuator tick: the FC must
     * be able to complete app_init() (parameters are read over the link) and
     * run its first control tick, which is what produces the first tick. Without
     * this the two ends wait for each other forever - found on the first
     * loopback run (the rig only advances on a tick, the FC only sends a tick
     * from its first control cycle). */
    refresh_sensors(&sens);
    {
        hil_sensor_frame_t sf;
        uint8_t pl[HIL_SENSOR_PAYLOAD_LEN];
        build_sensor_frame(&sens, &sf);
        hil_pack_sensors(&sf, pl, sizeof(pl));
        tx_frame(HIL_MSG_SENSORS, g.seq_tx++, pl, sizeof(pl));
        g.sensor_frames++;
        tx_flush();
    }

    uint64_t wall0 = rig_now_ms();
    uint64_t last_tick_ms = wall0;
    uint32_t idle_ms = 0u;
    bool done = false;
    bool idle_timeout = false;
    while (!done && g.ticks < ticks_max) {
        rx_pump();
        bool ticked = false;
        for (;;) {
            hil_msg_t m;
            hil_rx_t rc = hil_ring_pop(&g.rx, &m);
            if (rc == HIL_RX_OK) {
                g.frames_rx++;
                if (m.type == HIL_MSG_SHUTDOWN) { done = true; break; }
                if (handle_msg(&m)) ticked = true;
                continue;
            }
            break;
        }
        g.crc_errors = g.rx.crc_errors;
        g.seq_gaps  = g.rx.seq_gaps;
        g.resyncs   = g.rx.resyncs;
        g.overruns  = g.rx.overrun;
        if (done) break;

        if (!ticked) {
            /* no actuator frame this iteration: wait briefly instead of
             * spinning the world without the FC (would be a fake rig) */
            g.ticks_await_fc++;
            if (idle_ms == 0u) idle_ms = rig_now_ms() - last_tick_ms;
            else if (rig_now_ms() - last_tick_ms > idle_timeout_ms) {
                printf("rig: IDLE TIMEOUT after %u ms without an actuator frame\n",
                       (unsigned)idle_timeout_ms);
                fflush(stdout);
                idle_timeout = true;
                break;
            }
            rig_wait_readable(g.client_fd, 1);
            continue;
        }
        last_tick_ms = rig_now_ms();
        idle_ms = 0u;

        /* the world advances only in response to a real actuator frame */
        sim_model_step(1000u);
        /* fault-injection latency: record when the model activated a fault and
         * when the frame carrying that state first left the rig. Both are in the
         * shared (rig) time base, so the difference is the injection-to-
         * observation latency the FC will experience. */
        {
            uint64_t lf = sim_model_last_fault_us();
            if (lf && lf != g.fault_apply_us) {
                g.fault_apply_us = lf;
                g.fault_next_frame_us = 0u;   /* next frame carries the new state */
            }
        }
        refresh_sensors(&sens);
        g.ticks++;

        hil_sensor_frame_t sf;
        build_sensor_frame(&sens, &sf);
        uint8_t pl[HIL_SENSOR_PAYLOAD_LEN];
        hil_pack_sensors(&sf, pl, sizeof(pl));

        bool drop = (drop_every && (g.ticks % drop_every) == 0u);
        if (!drop) {
            /* fault visibility measurement: the frame that first carries an
             * injected fault reaches the FC one control cycle later */
            if (g.fault_apply_us && !g.fault_next_frame_us) {
                g.fault_next_frame_us = sim_model_time_us();
            }
            if (corrupt_every && (g.ticks % corrupt_every) == 0u && sf.avail) {
                /* handled inside tx_frame() (post-encode wire corruption) */
            }
            tx_frame(HIL_MSG_SENSORS, g.seq_tx++, pl, sizeof(pl));
            g.sensor_frames++;
        } else {
            g.frames_dropped++;
        }

        /* companion AI stream: 25 Hz structured set + real ICD-02 bytes */
        fc_ai_obstacle_set_t set;
        if (sim_model_read_ai(&set) == HAL_OK) {
            uint8_t apl[HIL_LINK_MAX_PAYLOAD];
            uint16_t an = hil_pack_ai_set(&set, apl, sizeof(apl));
            if (an) { tx_frame(HIL_MSG_AI_SET, g.seq_tx++, apl, an); g.ai_frames++; }
        }
        uint8_t cbuf[512];
        size_t cn = 0;
        if (sim_model_companion_uart(cbuf, sizeof(cbuf), &cn) > 0u && cn > 0u) {
            tx_frame(HIL_MSG_COMPANION, g.seq_tx++, cbuf, (uint16_t)cn);
            g.companion_frames++;
        }

        tx_flush();

        if (g.trace && (g.ticks % 10u) == 0u) {
            float x, y, z, vx, vy, vz, mm[4], rng;
            sim_model_get_horizontal(&x, &y);
            sim_model_get_vertical(&z, &vz);
            sim_model_get_horizontal_vel(&vx, &vy);
            sim_model_get_motor(mm);
            rng = sim_model_obstacle_range_m();
            fprintf(g.trace, "%llu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                    (unsigned long long)sim_model_time_us(), x, y, z, vx, vy, vz,
                    rng, mm[0], mm[1], mm[2], mm[3]);
        }
    }
    /* Grace period: the FC keeps running for a moment after its last tick (it
     * still wants a WORLD snapshot for the estimator-vs-truth correlation and
     * sends a farewell frame). Keep serving control messages until the FC goes
     * away or the grace period expires - without this the post-run query was
     * never answered (corr unavailable on the first loopback run). */
    if (!idle_timeout) {
        uint64_t grace_end = rig_now_ms() + 2000ull;
        while (rig_now_ms() < grace_end) {
            rx_pump();
            bool shutdown = false;
            for (;;) {
                hil_msg_t m;
                hil_rx_t rc = hil_ring_pop(&g.rx, &m);
                if (rc == HIL_RX_OK) {
                    g.frames_rx++;
                    if (m.type == HIL_MSG_SHUTDOWN) { shutdown = true; break; }
                    (void)handle_msg(&m);
                    continue;
                }
                break;
            }
            g.crc_errors = g.rx.crc_errors;
            g.seq_gaps  = g.rx.seq_gaps;
            g.resyncs   = g.rx.resyncs;
            g.overruns  = g.rx.overrun;
            tx_flush();
            if (shutdown) break;
            rig_wait_readable(g.client_fd, 1);
        }
    }
    uint64_t wall_ms = rig_now_ms() - wall0;

    if (g.capture) { fflush(g.capture); fclose(g.capture); }
    if (g.trace) { fclose(g.trace); }

    float x, y, z, vx, vy, vz, rng;
    sim_model_get_horizontal(&x, &y);
    sim_model_get_vertical(&z, &vz);
    sim_model_get_horizontal_vel(&vx, &vy);
    rng = sim_model_obstacle_range_m();

    printf("rig: ticks=%u sensor_frames=%u ai_frames=%u companion_frames=%u\n",
           g.ticks, g.sensor_frames, g.ai_frames, g.companion_frames);
    printf("rig: link frames_rx=%u frames_sent=%u crc_errors=%u seq_gaps=%u resyncs=%u overruns=%u wire_rx=%u wire_tx=%u\n",
           g.frames_rx, g.frames_sent, g.crc_errors, g.seq_gaps, g.resyncs, g.overruns,
           g.wire_rx, g.wire_tx);
    printf("rig: ticks_await_fc=%u dshot_frames=%u dshot_crc_err=%u motor_max=%.3f\n",
           g.ticks_await_fc, g.dshot_frames, g.dshot_crc_err, g.motor_max);
    if (g.fault_apply_us) {
        printf("rig: fault_applied_us=%llu fault_visible_us=%llu fault_latency_us=%llu "
               "(control cycle = 1000 us; the FC must report first_loss within it)\n",
               (unsigned long long)g.fault_apply_us,
               (unsigned long long)g.fault_next_frame_us,
               (unsigned long long)(g.fault_next_frame_us - g.fault_apply_us));
    } else {
        printf("rig: fault_applied_us=none\n");
    }
    printf("rig: corrupt_injected=%u frames_dropped=%u wall_ms=%u wall_ticks_per_s=%u\n",
           g.corrupt_injected, g.frames_dropped, (unsigned)wall_ms,
           wall_ms ? (unsigned)((g.ticks * 1000ull) / wall_ms) : 0u);
    printf("rig: world x=%.2f y=%.2f z=%.2f v=(%.2f, %.2f, %.2f) obst_range=%.2f battery=%.1f mAh\n",
           x, y, z, vx, vy, vz, rng, sim_model_battery_used_mah());
    fflush(stdout);

    if (g.client_fd >= 0) close_socket(g.client_fd);
    if (g.listen_fd >= 0) close_socket(g.listen_fd);
    if (idle_timeout) return 3;
    return (g.dshot_crc_err || g.overruns) ? 2 : 0;
}