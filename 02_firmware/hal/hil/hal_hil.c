#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int socklen_t;
#define close_socket closesocket
#define WSA_LAST_ERROR WSAGetLastError()
#define WSA_EWOULDBLOCK WSAEWOULDBLOCK
#define WSA_EINPROGRESS WSAEWOULDBLOCK
#endif

#include "hal_hil.h"
#include "esc_dshot.h"
#include "motor_output.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* HIL HAL backend (Phase 22, DEC-019). See hal_hil.h for the timing model.
 * Everything here is host-side: the transport stands in for the STM32 buses.
 * Nothing in this file is hardware evidence. */

#define HIL_TX_CAP 4096u
#define HIL_COMPANION_FIFO 4096u

typedef struct {
    int      fd;
    hil_ring_t rx;
    /* TX ring (link I/O is flushed by hil_link_poll, never in the control path) */
    uint8_t  tx[HIL_TX_CAP];
    uint32_t tx_head, tx_tail;
    /* caches served to the control path */
    hil_sensor_frame_t sf;
    bool     have_sensor;
    uint64_t now_us;              /* rig clock = last received frame time */
    uint64_t last_sensor_us;
    fc_ai_obstacle_set_t ai;
    bool     have_ai;
    uint64_t ai_us;
    uint8_t  companion[HIL_COMPANION_FIFO];
    uint32_t comp_head, comp_tail;
    /* watchdog emulation (SAF-004): an IWDG that has fired stops accepting
     * feeds, exactly as a real one would stop the CPU. */
    uint32_t wdg_timeout_ms;
    uint64_t wdg_last_feed_us;
    bool     wdg_hung;
    /* one sequence counter per LINK DIRECTION (not per message type): the peer
     * detects loss by sequence, so every frame we send must share the counter -
     * separate per-type counters made a perfectly healthy stream look lossy
     * (found on the first loopback run: 283 phantom gaps in 4000 ticks). */
    uint8_t  seq_tx;
    /* pending synchronous response (flash / world), filled by dispatch */
    bool     have_resp;
    hil_msg_t resp;
    bool     lost_seen[HIL_SENSOR_COUNT];
    /* metrics */
    hil_stats_t st;
    bool     have_world;
    uint64_t world_t_us;
    float    world[11];
} hil_t;

static hil_t g;

/* ---------------- transport primitives ---------------- */

/* Wait until the socket is readable or the timeout expires.
 * select() is used instead of a sleep: on Windows the default timer
 * granularity turns Sleep(1) into ~15.6 ms, which would dominate a 1 kHz
 * control link (and made the flash request time out on the first run). */
static int hil_sock_wait_readable(int fd, int timeout_ms)
{
    fd_set rf;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    FD_ZERO(&rf);
#ifndef _WIN32
    FD_SET(fd, &rf);
#else
    FD_SET((SOCKET)fd, &rf);
#endif
    int rc = select(fd + 1, &rf, NULL, NULL, &tv);
    return rc;
}

static int hil_sock_connect(const char *host, uint16_t port, uint32_t timeout_ms)
{
#ifndef _WIN32
    int fd = socket(AF_INET, SOCK_STREAM, 0);
#else
    int fd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
    if (fd < 0) return -1;

    /* non-blocking connect with a bounded wait */
#ifndef _WIN32
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    u_long nb = 1;
    ioctlsocket((SOCKET)fd, FIONBIO, &nb);
#endif
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = inet_addr(host ? host : "127.0.0.1");

    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
#ifndef _WIN32
    if (rc != 0 && errno != EINPROGRESS) { close(fd); return -1; }
#else
    if (rc != 0 && WSA_LAST_ERROR != WSAEWOULDBLOCK) { close_socket(fd); return -1; }
#endif
    if (rc != 0) {
        /* select() until connected or timeout */
        fd_set wf;
        struct timeval tv;
        tv.tv_sec = (long)(timeout_ms / 1000u);
        tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
        FD_ZERO(&wf);
#ifndef _WIN32
        FD_SET(fd, &wf);
#else
        FD_SET((SOCKET)fd, &wf);
#endif
        int sel = select(fd + 1, NULL, &wf, NULL, &tv);
        if (sel <= 0) { close_socket(fd); return -1; }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl);
        if (soerr != 0) { close_socket(fd); return -1; }
    }
    return fd;
}

static size_t hil_tx_space(void)
{
    return (size_t)(HIL_TX_CAP - 1u - ((g.tx_head - g.tx_tail) & (HIL_TX_CAP - 1u)));
}

static void hil_tx_push(const uint8_t *data, size_t len)
{
    size_t space = hil_tx_space();
    if (len > space) { g.st.tx_drops++; return; }   /* never silently truncate */
    for (size_t i = 0; i < len; i++) {
        g.tx[g.tx_head] = data[i];
        g.tx_head = (g.tx_head + 1u) & (HIL_TX_CAP - 1u);
    }
}

static void hil_tx_frame(uint8_t type, uint8_t seq, const uint8_t *pl, uint16_t len)
{
    uint8_t buf[HIL_LINK_MAX_FRAME];
    size_t n = hil_link_encode(type, seq, 0u, g.now_us, pl, len, buf, sizeof(buf));
    if (n) { hil_tx_push(buf, n); g.st.frames_sent++; }
}

static void hil_companion_push(const uint8_t *d, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint32_t next = (g.comp_head + 1u) % HIL_COMPANION_FIFO;
        if (next == g.comp_tail) break;      /* FIFO full: drop oldest-marked tail */
        g.companion[g.comp_head] = d[i];
        g.comp_head = next;
    }
}

/* ---------------- frame dispatch ---------------- */

static void hil_note_age(uint64_t age_us, uint32_t idx)
{
    if (age_us > g.st.first_sample_age_max_us[idx]) g.st.first_sample_age_max_us[idx] = age_us;
}

static void hil_dispatch(const hil_msg_t *m)
{
    switch (m->type) {
    case HIL_MSG_SENSORS: {
        hil_sensor_frame_t sf;
        if (!hil_unpack_sensors(m->payload, m->len, &sf)) return;
        /* sample ages in the shared (rig) time base: this is the HIL staleness
         * measurement - a 10 Hz GNSS must look 10 Hz to the FC. */
        uint64_t now_ms = m->t_us / 1000u;
        static const uint8_t idx[HIL_SENSOR_COUNT] = {
            HIL_SENSOR_IMU, HIL_SENSOR_BARO, HIL_SENSOR_GNSS,
            HIL_SENSOR_TOF0, HIL_SENSOR_TOF1, HIL_SENSOR_BATTERY, HIL_SENSOR_RC
        };
        for (uint32_t i = 0; i < HIL_SENSOR_COUNT; i++) {
            if (!(sf.avail & idx[i])) {
                if (!g.lost_seen[i]) {
                    g.lost_seen[i] = true;
                    g.st.first_loss_us[i] = m->t_us;
                }
                continue;
            }
            uint64_t age_ms = (now_ms > sf.t_sample_ms[i]) ? (now_ms - sf.t_sample_ms[i]) : 0u;
            hil_note_age(age_ms * 1000u, i);
        }
        g.sf = sf;
        g.have_sensor = true;
        g.now_us = m->t_us;
        g.last_sensor_us = m->t_us;
        g.st.sensor_frames++;
        break;
    }
    case HIL_MSG_AI_SET: {
        fc_ai_obstacle_set_t set;
        if (hil_unpack_ai_set(m->payload, m->len, &set)) {
            set.timestamp_us = m->t_us;
            g.ai = set;
            g.have_ai = true;
            g.ai_us = m->t_us;
        }
        break;
    }
    case HIL_MSG_COMPANION:
        hil_companion_push(m->payload, m->len);
        break;
    case HIL_MSG_WORLD:
        if (m->len >= 44u) {
            memcpy(g.world, m->payload, 44u);
            g.world_t_us = m->t_us;
            g.have_world = true;
        }
        g.have_resp = true;      /* the world query is a request/response too */
        g.resp = *m;
        break;
    case HIL_MSG_ACK:
    case HIL_MSG_ERROR:
        /* synchronous responses are parked, not consumed: hil_link_request()
         * may be several poll() calls behind (any caller may have polled). */
        g.have_resp = true;
        g.resp = *m;
        break;
    case HIL_MSG_FLASH:
        if (m->flags & 0x01u) {          /* rig -> FC flash reply */
            g.have_resp = true;
            g.resp = *m;
        }
        break;
    default:
        break;
    }
}

void hil_link_poll(void)
{
    uint8_t buf[2048];

#ifdef HIL_DEBUG_TRACE
#define HIL_SANITY(tag) do { \
    if (g.rx.head >= HIL_RING_CAP || g.rx.tail >= HIL_RING_CAP) \
        fprintf(stderr, "[hil] CORRUPT(%s) head=%u tail=%u frames=%u\n", \
                (tag), g.rx.head, g.rx.tail, g.rx.frames); \
} while (0)
#else
#define HIL_SANITY(tag) ((void)0)
#endif

    HIL_SANITY("entry");

    /* 1. drain the socket into the RX ring (the "DMA" equivalent) */
    for (;;) {
#ifndef _WIN32
        ssize_t n = recv(g.fd, buf, sizeof(buf), 0);
        int err = errno;
#else
        int n = recv(g.fd, (char *)buf, (int)sizeof(buf), 0);
        int err = WSA_LAST_ERROR;
#endif
        if (n > 0) {
#ifdef HIL_DEBUG_TRACE
            if (g.st.wire_bytes_rx < 200u) {
                fprintf(stderr, "[hil] recv %d bytes (total %u)\n", (int)n, g.st.wire_bytes_rx);
            }
#endif
            g.st.wire_bytes_rx += (uint32_t)n;
            size_t pushed = hil_ring_push(&g.rx, buf, (size_t)n);
            (void)pushed;
            if ((size_t)n < sizeof(buf)) break;
            continue;
        }
        if (n == 0) break;                       /* rig closed the socket */
#ifndef _WIN32
        if (err == EAGAIN || err == EWOULDBLOCK) break;
#else
        if (err == WSA_EWOULDBLOCK) break;
#endif
        break;
    }

    /* 2. dispatch every complete frame */
    for (;;) {
        hil_msg_t m;
        hil_rx_t rc = hil_ring_pop(&g.rx, &m);
        if (rc == HIL_RX_OK) { hil_dispatch(&m); HIL_SANITY("dispatch"); continue; }
        if (rc == HIL_RX_NEED_MORE) break;
        HIL_SANITY("rxerr");
        /* CRC_ERR / BAD already counted and resynced inside the ring */
    }
    g.st.crc_errors = g.rx.crc_errors;
    g.st.seq_gaps  = g.rx.seq_gaps;
    g.st.resyncs   = g.rx.resyncs;

    /* 3. flush the TX ring */
    HIL_SANITY("pre-flush");
    while (g.tx_tail != g.tx_head) {
        uint32_t chunk = 0;
        if (g.tx_head > g.tx_tail) {
            chunk = g.tx_head - g.tx_tail;
        } else {
            chunk = HIL_TX_CAP - g.tx_tail;
        }
#ifndef _WIN32
        ssize_t n = send(g.fd, g.tx + g.tx_tail, chunk, MSG_NOSIGNAL);
#else
        int n = send(g.fd, (const char *)(g.tx + g.tx_tail), (int)chunk, 0);
#endif
        if (n > 0) {
#ifdef HIL_DEBUG_TRACE
            if (g.st.wire_bytes_tx < 400u) {
                fprintf(stderr, "[hil] SENT %d bytes (total %u)\n", (int)n, g.st.wire_bytes_tx);
            }
#endif
            g.st.wire_bytes_tx += (uint32_t)n;
            g.tx_tail = (g.tx_tail + (uint32_t)n) & (HIL_TX_CAP - 1u);
            continue;
        }
        g.st.tx_send_calls++;
        g.st.tx_send_err++;
#ifdef _WIN32
        g.st.tx_send_err_last = (int)WSA_LAST_ERROR;
#else
        g.st.tx_send_err_last = errno;
#endif
#ifdef HIL_DEBUG_TRACE
        if (g.st.tx_send_err <= 5u) {
            fprintf(stderr, "[hil] send fail #%u fd=%d err=%d pending=%u\n",
                    g.st.tx_send_err, g.fd, g.st.tx_send_err_last,
                    (unsigned)((g.tx_head - g.tx_tail) & (HIL_TX_CAP - 1u)));
        }
#endif
        break;   /* would block: retry on the next poll, never busy-wait */
    }

    /* 4. emulated IWDG (SAF-004): expired => stop accepting feeds */
    if (g.wdg_timeout_ms && !g.wdg_hung && g.have_sensor) {
        if ((g.now_us - g.wdg_last_feed_us) > (uint64_t)g.wdg_timeout_ms * 1000u) {
            g.wdg_hung = true;
            g.st.wdt_expiries++;
        }
    }
}

bool hil_link_wait_sensor(uint32_t timeout_ms)
{
    uint64_t deadline = (uint64_t)timeout_ms * 1000u;
    uint64_t waited = 0;
    while (waited <= deadline) {
        hil_link_poll();
        if (g.have_sensor) return true;
        hil_sock_wait_readable(g.fd, 1);
        waited += 1000u;
    }
    return g.have_sensor;
}

void hil_link_wait_io(uint32_t timeout_ms)
{
    if (g.fd >= 0) hil_sock_wait_readable(g.fd, (int)timeout_ms);
}

int hil_link_init(const char *host, uint16_t port, uint32_t timeout_ms)
{
    memset(&g, 0, sizeof(g));
    hil_ring_reset(&g.rx);
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    g.fd = hil_sock_connect(host, port, timeout_ms);
    if (g.fd < 0) return -1;
    /* Nagle would add up to 40 ms of link latency: a control link must not
     * batch ticks (HIL_DESIGN.md timing section). */
    int one = 1;
    setsockopt(g.fd, IPPROTO_TCP, 1 /* TCP_NODELAY */, (const char *)&one, sizeof(one));
    return 0;
}

void hil_link_shutdown(void)
{
    /* Best effort: a peer that has already closed the socket must not turn a
     * clean run into a failure (WSAECONNRESET on the farewell frame). */
    hil_tx_frame(HIL_MSG_SHUTDOWN, g.seq_tx++, NULL, 0);
    hil_link_poll();
    if (g.fd >= 0) close_socket(g.fd);
    g.fd = -1;
}

/* ---------------- link control API ---------------- */

bool hil_link_request(uint8_t type, const uint8_t *pl, uint16_t len, hil_msg_t *resp)
{
    uint8_t wait_kind = type;
    hil_tx_frame(type, g.seq_tx++, pl, len);
    g.have_resp = false;
    for (int i = 0; i < 2000; i++) {
        hil_link_poll();
        if (g.have_resp) {
            bool ok = (g.resp.type != HIL_MSG_ERROR) &&
                      (g.resp.type == wait_kind || wait_kind == HIL_MSG_ACK ||
                       g.resp.type == HIL_MSG_ACK || g.resp.type == HIL_MSG_WORLD);
            if (resp) *resp = g.resp;
            g.have_resp = false;
            return ok;
        }
        hil_sock_wait_readable(g.fd, 2);
    }
    return false;
}

void hil_link_fault(const char *name, bool active)
{
    int id = hil_fault_name_to_id(name);
    if (id < 0) return;
    uint8_t pl[2] = { (uint8_t)id, (uint8_t)(active ? 1 : 0) };
    hil_tx_frame(HIL_MSG_FAULT, g.seq_tx++, pl, sizeof(pl));
}

void hil_link_schedule_fault(const char *name, uint64_t at_us)
{
    int id = hil_fault_name_to_id(name);
    if (id < 0) return;
    uint8_t pl[9];
    pl[0] = (uint8_t)id;
    for (int i = 0; i < 8; i++) pl[1 + i] = (uint8_t)(at_us >> (8 * i));
    hil_tx_frame(HIL_MSG_SCHED_FAULT, g.seq_tx++, pl, sizeof(pl));
}

void hil_link_scenario(const char *name)
{
    if (!name) return;
    hil_tx_frame(HIL_MSG_SCENARIO, g.seq_tx++,
                 (const uint8_t *)name, (uint16_t)strlen(name));
}

void hil_link_request_world(void)
{
    hil_tx_frame(HIL_MSG_ACK, g.seq_tx++, NULL, 0);   /* rig replies with WORLD */
}

uint64_t hil_link_time_us(void) { return g.now_us; }
bool hil_link_world(uint64_t *t_us, float world[11])
{
    if (!g.have_world) return false;
    if (t_us) *t_us = g.world_t_us;
    if (world) memcpy(world, g.world, sizeof(g.world));
    return true;
}

void hil_link_stats(hil_stats_t *out)
{
    if (!out) return;
    *out = g.st;
    out->wdg_timeout_ms = g.wdg_timeout_ms;
    out->ring_stats = g.rx;
}

void hil_link_report(void)
{
    hil_stats_t s;
    hil_link_stats(&s);
    printf("hil_fc: ticks=%u sensor_frames=%u frames_sent=%u actuator_writes=%u\n",
           s.ticks, s.sensor_frames, s.frames_sent, s.actuator_writes);
    printf("hil_fc: link_stalls=%u tx_drops=%u crc_errors=%u seq_gaps=%u resyncs=%u overruns=%u\n",
           s.link_stalls, s.tx_drops, s.crc_errors, s.seq_gaps, s.resyncs,
           s.ring_stats.overrun);
    printf("hil_fc: tx_send_calls=%u tx_send_err=%u tx_send_err_last=%d\n",
           s.tx_send_calls, s.tx_send_err, s.tx_send_err_last);
    printf("hil_fc: wire_rx=%u wire_tx=%u wall_ms=%u wdt_expiries=%u wdg_timeout_ms=%u\n",
           s.wire_bytes_rx, s.wire_bytes_tx, (unsigned)s.wall_elapsed_ms,
           s.wdt_expiries, s.wdg_timeout_ms);
    static const char *const sid[HIL_SENSOR_COUNT] = { "imu", "baro", "gnss", "tof0", "tof1", "batt", "rc" };
    for (uint32_t i = 0; i < HIL_SENSOR_COUNT; i++) {
        printf("hil_fc: sensor[%s] age_max_us=%llu first_loss_us=%llu\n", sid[i],
               (unsigned long long)s.first_sample_age_max_us[i],
               (unsigned long long)s.first_loss_us[i]);
    }
}

/* ================= HAL contract ================= */

uint64_t hal_time_us(void) { return g.now_us; }

void hal_sleep_until_us(uint64_t deadline_us)
{
    /* The rig paces the FC: waiting is the rig's job (it is the clock master).
     * The FC never busy-waits on a local clock - blocking here would defeat the
     * host-master time base that makes HIL timing measurable. */
    (void)deadline_us;
}

hal_status_t hal_imu_read(fc_imu_sample_t *out)
{
    if (!out) return HAL_ERROR;
    if (!g.have_sensor || !(g.sf.avail & HIL_SENSOR_IMU)) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = (uint64_t)g.sf.t_sample_ms[HIL_SIDX_IMU] * 1000u;
    out->accel_m_s2 = g.sf.accel_m_s2;
    out->gyro_rad_s = g.sf.gyro_rad_s;
    out->valid = (g.sf.stat & HIL_STAT_IMU_VALID) != 0u;
    return HAL_OK;
}

hal_status_t hal_baro_read(fc_baro_sample_t *out)
{
    if (!out) return HAL_ERROR;
    if (!g.have_sensor || !(g.sf.avail & HIL_SENSOR_BARO)) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = (uint64_t)g.sf.t_sample_ms[HIL_SIDX_BARO] * 1000u;
    out->pressure_pa = g.sf.pressure_pa;
    out->temperature_c = g.sf.temperature_c;
    out->valid = (g.sf.stat & HIL_STAT_BARO_VALID) != 0u;
    return HAL_OK;
}

hal_status_t hal_gnss_read(fc_gnss_sample_t *out)
{
    if (!out) return HAL_ERROR;
    if (!g.have_sensor || !(g.sf.avail & HIL_SENSOR_GNSS)) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = (uint64_t)g.sf.t_sample_ms[HIL_SIDX_GNSS] * 1000u;
    out->lat_deg = g.sf.lat_deg;
    out->lon_deg = g.sf.lon_deg;
    out->alt_m = g.sf.alt_m;
    out->vn_ms = g.sf.vn_ms; out->ve_ms = g.sf.ve_ms; out->vd_ms = g.sf.vd_ms;
    out->sats = g.sf.sats;
    out->fix_valid = (g.sf.stat & HIL_STAT_GNSS_FIX) != 0u;
    out->valid = true;
    return HAL_OK;
}

hal_status_t hal_tof_read(int id, fc_tof_sample_t *out)
{
    if (!out || (id != 0 && id != 1)) return HAL_ERROR;
    uint8_t bit = (id == 0) ? HIL_SENSOR_TOF0 : HIL_SENSOR_TOF1;
    uint32_t idx = (id == 0) ? HIL_SIDX_TOF0 : HIL_SIDX_TOF1;
    if (!g.have_sensor || !(g.sf.avail & bit)) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = (uint64_t)g.sf.t_sample_ms[idx] * 1000u;
    out->range_m = (id == 0) ? g.sf.tof0_m : g.sf.tof1_m;
    out->valid = true;
    return HAL_OK;
}

hal_status_t hal_flow_read(fc_flow_sample_t *out)
{
    /* Optical flow is NOT_READY in this build (same as the SIM backend); the
     * link carries the field so a flow-equipped rig needs no HAL change. */
    if (!out) return HAL_ERROR;
    if (!g.have_sensor || !(g.sf.avail & HIL_SENSOR_FLOW) || !g.sf.flow_valid) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = g.now_us;
    out->flow_x_px = g.sf.flow_x_px;
    out->flow_y_px = g.sf.flow_y_px;
    out->quality = g.sf.flow_quality;
    out->valid = true;
    return HAL_OK;
}

hal_status_t hal_companion_ai_read(fc_ai_obstacle_set_t *out)
{
    if (!out) return HAL_ERROR;
    if (!g.have_ai) return HAL_NOT_READY;
    *out = g.ai;
    return HAL_OK;
}

size_t hal_companion_uart_read(uint8_t *out, size_t cap, size_t *n_out)
{
    if (!out || !n_out) return 0;
    *n_out = 0;
    size_t used = 0;
    while (g.comp_tail != g.comp_head && used < cap) {
        out[used++] = g.companion[g.comp_tail];
        g.comp_tail = (g.comp_tail + 1u) % HIL_COMPANION_FIFO;
    }
    *n_out = used;
    return used;
}

hal_status_t hal_battery_read(fc_battery_sample_t *out)
{
    if (!out) return HAL_ERROR;
    if (!g.have_sensor || !(g.sf.avail & HIL_SENSOR_BATTERY)) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = (uint64_t)g.sf.t_sample_ms[HIL_SIDX_BATT] * 1000u;
    out->pack_v = g.sf.pack_v;
    out->current_a = g.sf.current_a;
    for (int i = 0; i < 4; i++) out->cell_v[i] = g.sf.cell_v[i];
    out->consumed_mah = g.sf.consumed_mah;
    out->valid = true;
    return HAL_OK;
}

hal_status_t hal_rc_read(fc_rc_frame_t *out)
{
    if (!out) return HAL_ERROR;
    if (!g.have_sensor || !(g.sf.avail & HIL_SENSOR_RC)) return HAL_NOT_READY;
    memset(out, 0, sizeof(*out));
    out->timestamp_us = (uint64_t)g.sf.t_sample_ms[HIL_SIDX_RC] * 1000u;
    for (int i = 0; i < 8; i++) out->channels[i] = g.sf.rc_ch[i];
    out->frames_valid = (g.sf.stat & HIL_STAT_RC_VALID) != 0u;
    out->failsafe_active = (g.sf.stat & HIL_STAT_RC_FAILSAFE) != 0u;
    out->valid = true;
    return HAL_OK;
}

hal_status_t hal_actuator_write(const float motor[4])
{
    if (!motor) return HAL_ERROR;
    /* Production DShot600 frames - the exact 16-bit words an ESC receives -
     * cross the link, so the rig (and the capture analyser) verify the same
     * bytes the TIM1 DMA would emit. No socket I/O happens here. */
    uint16_t frames[4];
    motor_output_frames(frames);
    uint8_t pl[9];
    for (int i = 0; i < 4; i++) {
        pl[2 * i]     = (uint8_t)(frames[i] & 0xFFu);
        pl[2 * i + 1] = (uint8_t)(frames[i] >> 8);
    }
    pl[8] = (uint8_t)(motor_output_is_armed() ? 1u : 0u);
    hil_tx_frame(HIL_MSG_TICK, g.seq_tx++, pl, sizeof(pl));
    g.st.actuator_writes++;
    return HAL_OK;
}

/* ---- flash: served by the rig's parameter image (init-time only) ---- */
hal_status_t hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!buf || len > 512u) return HAL_ERROR;
    uint8_t pl[7];
    pl[0] = 0u;                                  /* op = read */
    for (int i = 0; i < 4; i++) pl[1 + i] = (uint8_t)(addr >> (8 * i));
    pl[5] = (uint8_t)(len & 0xFFu);
    pl[6] = (uint8_t)(len >> 8);
    hil_msg_t resp;
    if (!hil_link_request(HIL_MSG_FLASH, pl, sizeof(pl), &resp)) return HAL_ERROR;
    if (resp.len < len) return HAL_ERROR;
    memcpy(buf, resp.payload, len);
    return HAL_OK;
}

hal_status_t hal_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (!buf || len > 512u) return HAL_ERROR;
    uint8_t pl[7 + 512];
    pl[0] = 1u;                                  /* op = write */
    for (int i = 0; i < 4; i++) pl[1 + i] = (uint8_t)(addr >> (8 * i));
    pl[5] = (uint8_t)(len & 0xFFu);
    pl[6] = (uint8_t)(len >> 8);
    memcpy(pl + 7, buf, len);
    hil_msg_t resp;
    return hil_link_request(HIL_MSG_FLASH, pl, (uint16_t)(7u + len), &resp) ? HAL_OK : HAL_ERROR;
}

hal_status_t hal_flash_erase(uint32_t addr)
{
    uint8_t pl[7];
    pl[0] = 2u;
    for (int i = 0; i < 4; i++) pl[1 + i] = (uint8_t)(addr >> (8 * i));
    pl[5] = 0u; pl[6] = 0u;
    hil_msg_t resp;
    return hil_link_request(HIL_MSG_FLASH, pl, sizeof(pl), &resp) ? HAL_OK : HAL_ERROR;
}

hal_status_t hal_wdg_init(uint32_t timeout_ms)
{
    g.wdg_timeout_ms = timeout_ms;
    g.wdg_last_feed_us = g.now_us;
    g.wdg_hung = false;
    return HAL_OK;
}

void hal_wdg_feed(void)
{
    if (g.wdg_hung) return;        /* a fired IWDG ignores further feeds */
    g.wdg_last_feed_us = g.now_us;
}

/* ---- internal: tick accounting used by the HIL entry point ---- */
void hil_link_tick(bool got_new_frame)
{
    g.st.ticks++;
    if (!got_new_frame) g.st.link_stalls++;
}

void hil_link_ring_sanity(const char *where)
{
#ifdef HIL_DEBUG_TRACE
    if (g.rx.head >= HIL_RING_CAP || g.rx.tail >= HIL_RING_CAP)
        fprintf(stderr, "[hil] CORRUPT after %s head=%u tail=%u frames=%u\n",
                where, g.rx.head, g.rx.tail, g.rx.frames);
#else
    (void)where;
#endif
}

void hil_link_set_wall_elapsed_ms(uint64_t ms) { g.st.wall_elapsed_ms = ms; }