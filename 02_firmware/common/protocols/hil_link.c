#include "hil_link.h"
#include "crc16.h"
#include <string.h>

/* HIL transport codec (Phase 22). Explicit little-endian field access: both
 * endpoints may be a different architecture, and a real rig link must not
 * depend on host endianness or struct padding. */

/* ---- little-endian primitives ---- */
static void put_u16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put_u32(uint8_t *p, uint32_t v) { for (int i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void put_u64(uint8_t *p, uint64_t v) { for (int i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void put_f32(uint8_t *p, float v)    { uint32_t u; memcpy(&u,&v,4); put_u32(p,u); }
static void put_f64(uint8_t *p, double v)   { uint64_t u; memcpy(&u,&v,8); put_u64(p,u); }
static uint16_t get_u16(const uint8_t *p)   { return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t get_u32(const uint8_t *p)   { uint32_t v=0; for (int i=0;i<4;i++) v |= (uint32_t)p[i]<<(8*i); return v; }
static uint64_t get_u64(const uint8_t *p)   { uint64_t v=0; for (int i=0;i<8;i++) v |= (uint64_t)p[i]<<(8*i); return v; }
static float    get_f32(const uint8_t *p)   { uint32_t u=get_u32(p); float v; memcpy(&v,&u,4); return v; }
static double   get_f64(const uint8_t *p)   { uint64_t u=get_u64(p); double v; memcpy(&v,&u,8); return v; }

size_t hil_link_encode(uint8_t type, uint8_t seq, uint8_t flags, uint64_t t_us,
                       const uint8_t *payload, uint16_t len,
                       uint8_t *out, size_t cap)
{
    if (!out || len > HIL_LINK_MAX_PAYLOAD) return 0;
    size_t total = HIL_LINK_HDR_LEN + (size_t)len + HIL_LINK_CRC_LEN;
    if (cap < total) return 0;

    out[0] = HIL_LINK_SOF0;
    out[1] = HIL_LINK_SOF1;
    out[2] = HIL_LINK_VERSION;
    out[3] = type;
    out[4] = seq;
    out[5] = flags;
    put_u16(out + 6, len);
    put_u64(out + 8, t_us);
    if (len && payload) memcpy(out + HIL_LINK_HDR_LEN, payload, len);
    put_u16(out + HIL_LINK_HDR_LEN + len, crc16_ccitt(out, HIL_LINK_HDR_LEN + len));
    return total;
}

hil_rx_t hil_link_decode(const uint8_t *buf, size_t len, hil_msg_t *out)
{
    if (!buf || len < 2u) return HIL_RX_NEED_MORE;
    /* Pure decoder: it never mutates the caller's buffer. Resync is the ring's
     * job (hil_ring_pop compacts to the next SOF), so a corrupted frame can
     * never stall the stream. */
    if (buf[0] != HIL_LINK_SOF0 || buf[1] != HIL_LINK_SOF1) return HIL_RX_BAD;
    if (!out) return HIL_RX_NEED_MORE;
    if (len < HIL_LINK_HDR_LEN) return HIL_RX_NEED_MORE;

    if (buf[2] != HIL_LINK_VERSION) return HIL_RX_BAD;
    uint16_t plen = get_u16(buf + 6);
    if (plen > HIL_LINK_MAX_PAYLOAD) return HIL_RX_BAD;

    size_t total = HIL_LINK_HDR_LEN + (size_t)plen + HIL_LINK_CRC_LEN;
    if (len < total) return HIL_RX_NEED_MORE;

    uint16_t want = get_u16(buf + HIL_LINK_HDR_LEN + plen);
    uint16_t got  = crc16_ccitt(buf, HIL_LINK_HDR_LEN + plen);
    if (want != got) return HIL_RX_CRC_ERR;

    out->type    = buf[3];
    out->seq     = buf[4];
    out->flags   = buf[5];
    out->len     = plen;
    out->t_us    = get_u64(buf + 8);
    if (plen) memcpy(out->payload, buf + HIL_LINK_HDR_LEN, plen);
    return HIL_RX_OK;
}

/* ---- RX ring ---- */
void hil_ring_reset(hil_ring_t *r)
{
    if (!r) return;
    r->head = r->tail = 0;
    r->frames = r->crc_errors = r->seq_gaps = r->resyncs = r->overrun = 0;
    r->last_seq = 0;
    r->have_seq = false;
}

size_t hil_ring_space(const hil_ring_t *r)
{
    if (!r) return 0;
    return (size_t)(HIL_RING_CAP - 1u - ((r->head - r->tail) & (HIL_RING_CAP - 1u)));
}

size_t hil_ring_push(hil_ring_t *r, const uint8_t *data, size_t len)
{
    if (!r || !data || len == 0u) return 0;
    size_t space = hil_ring_space(r);
    if (len > space) {
        r->overrun++;
        len = space;          /* drop the newest bytes, count it: never silent */
    }
    for (size_t i = 0; i < len; i++) {
        r->buf[r->head] = data[i];
        r->head = (r->head + 1u) & (HIL_RING_CAP - 1u);
    }
    return len;
}

/* Byte at logical offset `off` from tail, honouring the wrap. */
static inline uint8_t ring_at(const hil_ring_t *r, size_t off)
{
    return r->buf[(r->tail + off) & (HIL_RING_CAP - 1u)];
}

hil_rx_t hil_ring_pop(hil_ring_t *r, hil_msg_t *out)
{
    if (!r || !out) return HIL_RX_NEED_MORE;
    size_t avail = (size_t)((r->head - r->tail) & (HIL_RING_CAP - 1u));
    if (avail == 0u) return HIL_RX_NEED_MORE;

    /* Resync to the first SOF in the live window.
     *
     * The window is NOT necessarily linear: once the ring wraps, the live bytes
     * are [tail..CAP) followed by [0..head). The first implementation compacted
     * with a single memmove over the modular length, which wrote up to 4095
     * bytes past buf[] and smashed head/frames - found by the Phase 22 loopback
     * run (SIGSEGV in hil_ring_push). Every access here is modular instead. */
    size_t off = 0;
    while (off + 1u < avail) {
        if (ring_at(r, off) == HIL_LINK_SOF0 && ring_at(r, off + 1u) == HIL_LINK_SOF1) break;
        off++;
    }
    if (off + 1u >= avail) return HIL_RX_NEED_MORE;   /* no full SOF yet: wait */

    if (off > 0u) {
        r->tail = (r->tail + (uint32_t)off) & (HIL_RING_CAP - 1u);
        avail -= off;
        r->resyncs++;
    }

    if (avail < (size_t)HIL_LINK_HDR_LEN) return HIL_RX_NEED_MORE;

    /* Validate the version BEFORE trusting the length field: a byte pair that
     * happens to look like an SOF would otherwise make the ring wait forever
     * for a bogus 600-byte frame (found by the resync unit test). */
    if (ring_at(r, 2) != HIL_LINK_VERSION) {
        r->tail = (r->tail + 1u) & (HIL_RING_CAP - 1u);
        r->resyncs++;
        return HIL_RX_BAD;
    }

    uint16_t plen = (uint16_t)(ring_at(r, 6u) | ((uint16_t)ring_at(r, 7u) << 8));
    if (plen > HIL_LINK_MAX_PAYLOAD) {
        r->tail = (r->tail + 1u) & (HIL_RING_CAP - 1u);   /* impossible length */
        r->resyncs++;
        return HIL_RX_BAD;
    }
    size_t need = (size_t)HIL_LINK_HDR_LEN + (size_t)plen + (size_t)HIL_LINK_CRC_LEN;
    if (avail < need) return HIL_RX_NEED_MORE;             /* frame still arriving */

    /* Assemble exactly one frame into a linear window (bounded by the max
     * frame size), then decode it with the pure codec. */
    uint8_t win[HIL_LINK_MAX_FRAME];
    for (size_t k = 0; k < need; k++) win[k] = ring_at(r, k);

    hil_msg_t m;
    hil_rx_t rc = hil_link_decode(win, need, &m);
    if (rc == HIL_RX_CRC_ERR) {
        r->crc_errors++;
        r->tail = (r->tail + 1u) & (HIL_RING_CAP - 1u);   /* drop one byte, resync */
        return HIL_RX_CRC_ERR;
    }
    if (rc == HIL_RX_BAD) {
        r->resyncs++;
        r->tail = (r->tail + 1u) & (HIL_RING_CAP - 1u);
        return HIL_RX_BAD;
    }
    if (rc != HIL_RX_OK) return HIL_RX_NEED_MORE;

    r->tail = (r->tail + (uint32_t)need) & (HIL_RING_CAP - 1u);
    r->frames++;
    if (r->have_seq) {
        uint8_t expect = (uint8_t)(r->last_seq + 1u);
        if (m.seq != expect) r->seq_gaps++;
    }
    r->last_seq = m.seq;
    r->have_seq = true;

    *out = m;
    return HIL_RX_OK;
}

/* ---- sensor payload ---- */
uint16_t hil_pack_sensors(const hil_sensor_frame_t *f, uint8_t *out, size_t cap)
{
    if (!f || !out || cap < HIL_SENSOR_PAYLOAD_LEN) return 0;
    uint8_t *p = out;
    p[0] = f->avail;
    p[1] = 0u; p[2] = 0u; p[3] = 0u;
    put_f32(p + 4,  f->gyro_rad_s.x);
    put_f32(p + 8,  f->gyro_rad_s.y);
    put_f32(p + 12, f->gyro_rad_s.z);
    put_f32(p + 16, f->accel_m_s2.x);
    put_f32(p + 20, f->accel_m_s2.y);
    put_f32(p + 24, f->accel_m_s2.z);
    put_f32(p + 28, f->pressure_pa);
    put_f32(p + 32, f->temperature_c);
    put_f32(p + 36, f->tof0_m);
    put_f32(p + 40, f->tof1_m);
    put_f32(p + 44, f->pack_v);
    put_f32(p + 48, f->current_a);
    for (int i = 0; i < 4; i++) put_f32(p + 52 + 4 * i, f->cell_v[i]);
    put_f32(p + 68, f->alt_m);
    put_f32(p + 72, f->vn_ms);
    put_f32(p + 76, f->ve_ms);
    put_f32(p + 80, f->vd_ms);
    put_f64(p + 84, f->lat_deg);
    put_f64(p + 92, f->lon_deg);
    put_f32(p + 100, f->flow_x_px);
    put_f32(p + 104, f->flow_y_px);
    put_f32(p + 108, f->flow_quality);
    for (int i = 0; i < 8; i++) put_u16(p + 112 + 2 * i, f->rc_ch[i]);
    p[128] = f->sats;
    p[129] = f->stat;
    put_f32(p + 130, f->consumed_mah);
    for (unsigned i = 0; i < HIL_SENSOR_COUNT; i++) put_u32(p + 134 + 4 * i, f->t_sample_ms[i]);
    p[162] = 0u; p[163] = 0u;
    return HIL_SENSOR_PAYLOAD_LEN;
}

bool hil_unpack_sensors(const uint8_t *in, size_t len, hil_sensor_frame_t *f)
{
    if (!in || !f || len < HIL_SENSOR_PAYLOAD_LEN) return false;
    memset(f, 0, sizeof(*f));
    const uint8_t *p = in;
    f->avail = p[0];
    f->gyro_rad_s.x = get_f32(p + 4);
    f->gyro_rad_s.y = get_f32(p + 8);
    f->gyro_rad_s.z = get_f32(p + 12);
    f->accel_m_s2.x = get_f32(p + 16);
    f->accel_m_s2.y = get_f32(p + 20);
    f->accel_m_s2.z = get_f32(p + 24);
    f->pressure_pa  = get_f32(p + 28);
    f->temperature_c= get_f32(p + 32);
    f->tof0_m       = get_f32(p + 36);
    f->tof1_m       = get_f32(p + 40);
    f->pack_v       = get_f32(p + 44);
    f->current_a    = get_f32(p + 48);
    for (int i = 0; i < 4; i++) f->cell_v[i] = get_f32(p + 52 + 4 * i);
    f->alt_m  = get_f32(p + 68);
    f->vn_ms  = get_f32(p + 72);
    f->ve_ms  = get_f32(p + 76);
    f->vd_ms  = get_f32(p + 80);
    f->lat_deg = get_f64(p + 84);
    f->lon_deg = get_f64(p + 92);
    f->flow_x_px    = get_f32(p + 100);
    f->flow_y_px    = get_f32(p + 104);
    f->flow_quality = get_f32(p + 108);
    for (int i = 0; i < 8; i++) f->rc_ch[i] = get_u16(p + 112 + 2 * i);
    f->sats = p[128];
    f->stat = p[129];
    f->flow_valid  = (f->stat & HIL_STAT_FLOW_VALID) != 0u;
    f->consumed_mah = get_f32(p + 130);
    for (unsigned i = 0; i < HIL_SENSOR_COUNT; i++) f->t_sample_ms[i] = get_u32(p + 134 + 4 * i);
    return true;
}

/* ---- AI set payload ---- */
uint16_t hil_pack_ai_set(const fc_ai_obstacle_set_t *set, uint8_t *out, size_t cap)
{
    if (!set || !out) return 0;
    uint8_t n = set->count;
    if (n > HIL_MAX_DETECTIONS) n = HIL_MAX_DETECTIONS;
    size_t need = 1u + (size_t)n * 33u;
    if (cap < need) return 0;
    out[0] = n;
    uint8_t *p = out + 1;
    for (uint8_t i = 0; i < n; i++) {
        const fc_ai_detection_t *d = &set->det[i];
        put_f32(p + 0,  d->pos_m.x);
        put_f32(p + 4,  d->pos_m.y);
        put_f32(p + 8,  d->pos_m.z);
        put_f32(p + 12, d->vel_m_s.x);
        put_f32(p + 16, d->vel_m_s.y);
        put_f32(p + 20, d->vel_m_s.z);
        put_f32(p + 24, d->radius_m);
        put_f32(p + 28, d->confidence);
        p[32] = d->class_id;
        p += 33;
    }
    return (uint16_t)need;
}

bool hil_unpack_ai_set(const uint8_t *in, size_t len, fc_ai_obstacle_set_t *set)
{
    if (!in || !set || len < 1u) return false;
    memset(set, 0, sizeof(*set));
    uint8_t n = in[0];
    if (n > HIL_MAX_DETECTIONS) n = HIL_MAX_DETECTIONS;
    if (len < 1u + (size_t)n * 33u) return false;
    const uint8_t *p = in + 1;
    set->count = n;
    for (uint8_t i = 0; i < n; i++) {
        set->det[i].pos_m.x     = get_f32(p + 0);
        set->det[i].pos_m.y     = get_f32(p + 4);
        set->det[i].pos_m.z     = get_f32(p + 8);
        set->det[i].vel_m_s.x   = get_f32(p + 12);
        set->det[i].vel_m_s.y   = get_f32(p + 16);
        set->det[i].vel_m_s.z   = get_f32(p + 20);
        set->det[i].radius_m    = get_f32(p + 24);
        set->det[i].confidence  = get_f32(p + 28);
        set->det[i].class_id    = p[32];
        p += 33;
    }
    return true;
}

/* ---- fault vocabulary (identical to the SIM scenario names) ---- */
static const char *const k_fault_names[HIL_FAULT_COUNT] = {
    "none", "imu_dropout", "imu_stuck", "rc_loss",
    "battery_low", "gnss_loss", "ai_loss"
};

int hil_fault_name_to_id(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < HIL_FAULT_COUNT; i++) {
        if (!strcmp(name, k_fault_names[i])) return i;
    }
    return -1;
}

const char *hil_fault_id_to_name(int id)
{
    if (id < 0 || id >= HIL_FAULT_COUNT) return "?";
    return k_fault_names[id];
}