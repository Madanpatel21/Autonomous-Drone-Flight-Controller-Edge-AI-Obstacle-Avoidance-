#ifndef HIL_LINK_H
#define HIL_LINK_H

/* HIL transport protocol (Phase 22, DEC-019).
 *
 * One framed, CRC-protected, sequence-numbered byte protocol between the FC
 * (running the real application) and the host rig that owns the vehicle and
 * sensor world. It replaces the electrical interfaces of the STM32 rig
 * (SPI/I2C/UART/DShot) at the HAL boundary, so the flight code is unchanged.
 *
 * Frame (little-endian, CRC-16/CCITT-FALSE over header+payload):
 *   +0  u8  0xA5   SOF0
 *   +1  u8  0x5A   SOF1
 *   +2  u8  version
 *   +3  u8  type    hil_msg_type_t
 *   +4  u8  seq     per-direction counter
 *   +5  u8  flags
 *   +6  u16 len     payload length
 *   +8  u64 t_us    SENDER clock (rig epoch; the rig is the time master)
 *   +16 payload[len]
 *   +16+len u16 crc
 *
 * The sender stamps its own clock in every frame; the receiver serves the
 * application from the LAST received rig timestamp, which is what makes the
 * FC's time base a slave of the host clock master (the synchronisation model
 * of HIL_DESIGN.md section 2). */

#include "fc_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HIL_LINK_SOF0        0xA5u
#define HIL_LINK_SOF1        0x5Au
#define HIL_LINK_VERSION     1u
#define HIL_LINK_HDR_LEN     16u
#define HIL_LINK_CRC_LEN     2u
#define HIL_LINK_MAX_PAYLOAD 640u
#define HIL_LINK_MAX_FRAME   (HIL_LINK_HDR_LEN + HIL_LINK_MAX_PAYLOAD + HIL_LINK_CRC_LEN)

typedef enum {
    HIL_MSG_TICK        = 1,  /* FC->rig: actuator outputs for one control tick */
    HIL_MSG_SENSORS     = 2,  /* rig->FC: the sensor frame (rates preserved) */
    HIL_MSG_AI_SET      = 3,  /* rig->FC: ICD-02 OBSTACLE_SET payload, 25 Hz */
    HIL_MSG_COMPANION   = 4,  /* rig->FC: raw ICD-02 UART bytes */
    HIL_MSG_FAULT       = 5,  /* FC->rig: fault injection set/clear */
    HIL_MSG_SCHED_FAULT = 6,  /* FC->rig: schedule a fault at an absolute time */
    HIL_MSG_SCENARIO    = 7,  /* FC->rig: scenario preset name */
    HIL_MSG_FLASH       = 8,  /* both: parameter-store request/response */
    HIL_MSG_ACK         = 9,
    HIL_MSG_ERROR       = 10,
    HIL_MSG_WORLD       = 11, /* rig->FC: truth snapshot for correlation */
    HIL_MSG_SHUTDOWN    = 12  /* FC->rig: end of run */
} hil_msg_type_t;

/* sensor-frame availability / status bits (payload offset 0) */
#define HIL_SENSOR_IMU      0x01u
#define HIL_SENSOR_BARO     0x02u
#define HIL_SENSOR_GNSS     0x04u
#define HIL_SENSOR_TOF0     0x08u
#define HIL_SENSOR_TOF1     0x10u
#define HIL_SENSOR_BATTERY  0x20u
#define HIL_SENSOR_RC       0x40u
#define HIL_SENSOR_FLOW     0x80u

/* secondary status bits (payload offset 129) */
#define HIL_STAT_GNSS_FIX    0x01u
#define HIL_STAT_RC_FAILSAFE 0x02u
#define HIL_STAT_IMU_VALID   0x04u
#define HIL_STAT_BARO_VALID  0x08u
#define HIL_STAT_RC_VALID    0x10u
#define HIL_STAT_FLOW_VALID  0x20u

#define HIL_SENSOR_PAYLOAD_LEN 164u
#define HIL_MAX_DETECTIONS     16u

/* Per-sensor sample timestamps travel with the values (ms resolution): a
 * sensor that refreshes slower than the 1 kHz tick keeps its OLD timestamp, so
 * the FC sees the true sample age and its staleness/heartbeat logic behaves
 * exactly as it will against real, rate-limited hardware. */
#define HIL_SENSOR_COUNT 7u   /* imu, baro, gnss, tof0, tof1, battery, rc */

typedef struct {
    uint64_t t_us;          /* rig clock at frame time */
    uint8_t  avail;         /* HIL_SENSOR_* */
    uint8_t  stat;          /* HIL_STAT_* */
    uint8_t  sats;
    fc_vec3_t gyro_rad_s;
    fc_vec3_t accel_m_s2;
    float    pressure_pa;
    float    temperature_c;
    float    tof0_m;
    float    tof1_m;
    float    flow_x_px, flow_y_px, flow_quality;
    bool     flow_valid;
    float    pack_v, current_a, cell_v[4], consumed_mah;
    double   lat_deg, lon_deg;
    float    alt_m, vn_ms, ve_ms, vd_ms;
    uint16_t rc_ch[8];
    /* HIL_SENSOR_* order: IMU, BARO, GNSS, TOF0, TOF1, BATTERY, RC */
    uint32_t t_sample_ms[HIL_SENSOR_COUNT];
} hil_sensor_frame_t;

/* index into hil_sensor_frame_t::t_sample_ms */
#define HIL_SIDX_IMU 0u
#define HIL_SIDX_BARO 1u
#define HIL_SIDX_GNSS 2u
#define HIL_SIDX_TOF0 3u
#define HIL_SIDX_TOF1 4u
#define HIL_SIDX_BATT 5u
#define HIL_SIDX_RC   6u

/* A decoded frame owns its payload: the decoder may be fed a buffer that is
 * reclaimed immediately (socket ring), so callers must not hold a pointer into
 * it. Fixed-size payload keeps the control path allocation-free (FW-003). */
typedef struct {
    uint8_t  type;
    uint8_t  seq;
    uint8_t  flags;
    uint16_t len;
    uint64_t t_us;
    uint8_t  payload[HIL_LINK_MAX_PAYLOAD];
} hil_msg_t;

/* ---- frame codec ---- */
size_t hil_link_encode(uint8_t type, uint8_t seq, uint8_t flags, uint64_t t_us,
                       const uint8_t *payload, uint16_t len,
                       uint8_t *out, size_t cap);

typedef enum {
    HIL_RX_NEED_MORE = 0,   /* incomplete frame, wait for more bytes */
    HIL_RX_OK        = 1,
    HIL_RX_CRC_ERR   = 2,   /* frame discarded, caller should resync */
    HIL_RX_BAD       = 3    /* impossible header (version/length) */
} hil_rx_t;

hil_rx_t hil_link_decode(const uint8_t *buf, size_t len, hil_msg_t *out);

/* ---- RX ring: buffered socket bytes decoded one frame at a time ---- */
#define HIL_RING_CAP 4096u
typedef struct {
    uint8_t  buf[HIL_RING_CAP];
    uint32_t head, tail;
    uint32_t frames, crc_errors, seq_gaps, resyncs, overrun;
    uint8_t  last_seq;
    bool     have_seq;
} hil_ring_t;

void hil_ring_reset(hil_ring_t *r);
size_t hil_ring_space(const hil_ring_t *r);
size_t hil_ring_push(hil_ring_t *r, const uint8_t *data, size_t len);
/* Pops one complete frame into *out (payload copied out of the ring). */
hil_rx_t hil_ring_pop(hil_ring_t *r, hil_msg_t *out);

/* ---- payload helpers ---- */
uint16_t hil_pack_sensors(const hil_sensor_frame_t *f, uint8_t *out, size_t cap);
bool     hil_unpack_sensors(const uint8_t *in, size_t len, hil_sensor_frame_t *f);
uint16_t hil_pack_ai_set(const fc_ai_obstacle_set_t *set, uint8_t *out, size_t cap);
bool     hil_unpack_ai_set(const uint8_t *in, size_t len, fc_ai_obstacle_set_t *set);

/* ---- fault vocabulary shared with the SIM scenarios ---- */
typedef enum {
    HIL_FAULT_NONE = 0,
    HIL_FAULT_IMU_DROPOUT,
    HIL_FAULT_IMU_STUCK,
    HIL_FAULT_RC_LOSS,
    HIL_FAULT_BATTERY_LOW,
    HIL_FAULT_GNSS_LOSS,
    HIL_FAULT_AI_LOSS,
    HIL_FAULT_COUNT
} hil_fault_id_t;

int         hil_fault_name_to_id(const char *name);   /* -1 when unknown */
const char *hil_fault_id_to_name(int id);

#endif /* HIL_LINK_H */