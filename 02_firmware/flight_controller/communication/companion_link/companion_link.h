#ifndef COMPANION_LINK_H
#define COMPANION_LINK_H
#include "fc_types.h"
#include "icd02_frame.h"

/* Companion link task (DEC-003/015, ICD-02, COM-003). The frame codec lives
 * in common/protocols/icd02_frame.h so the SIM virtual companion and this
 * task share one implementation.
 *
 * RX behavior (acceptance: fail safely): CRC failure, unknown type,
 * over-length and truncated frames are counted and dropped, never applied;
 * sequence gaps are counted (loss observable, not fatal); a missing
 * HEARTBEAT for >1 s marks the companion unhealthy (SYS-004 window) but never
 * escalates a failsafe (DEC-006/007: advisory-only). */

#define CL_TYPE_HEARTBEAT      ICD02_TYPE_HEARTBEAT
#define CL_TYPE_OBSTACLE_SET   ICD02_TYPE_OBSTACLE_SET
#define CL_TYPE_HEALTH         ICD02_TYPE_HEALTH
#define CL_TYPE_FC_STATE       ICD02_TYPE_FC_STATE
#define CL_TYPE_FC_CONFIG_ACK  ICD02_TYPE_FC_CONFIG_ACK

#define COMPANION_HEARTBEAT_TIMEOUT_US 1000000u   /* SYS-004 */

typedef enum {
    CL_AI_UNKNOWN = 0,
    CL_AI_IDLE,        /* heartbeat without inference output */
    CL_AI_RUNNING,
    CL_AI_ERROR         /* inference crashed / thermal-throttled */
} companion_ai_state_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t unknown_types;
    uint32_t oversize;
    uint32_t seq_gaps;
    uint32_t seq_reorder;
    uint32_t ring_overflow;
    uint8_t  last_seq;
    uint64_t last_hb_us;
    uint64_t last_frame_us;
    companion_ai_state_t ai_state;
    uint16_t model_id;
    bool     health_valid;
    uint8_t  cpu_pct, mem_pct;
    int16_t  temp_c10;
    bool     camera_ok;
    uint16_t inference_ms;
} companion_stats_t;

void companion_link_init(void);

/* Push received bytes into the RX ring (any chunking), then poll(). */
void companion_link_rx(const uint8_t *buf, size_t n);

/* 100 Hz task: drain the ring, dispatch frames (OBSTACLE_SET ->
 * perception_feed_ai with the ICD-02 i16-cm decode), update health/timeouts. */
void companion_link_poll(uint64_t now_us);

bool companion_healthy(void);                 /* heartbeat within 1 s */
const companion_stats_t *companion_stats(void);
companion_ai_state_t companion_ai_state(void);

/* FC -> companion 0x10 FC_STATE builder (50 Hz caller). Sequence is internal. */
bool companion_link_build_fc_state(uint64_t now_us, const fc_quat_t *att,
                                   const fc_vec3_t *vel_ned_ms, uint8_t mode,
                                   uint8_t failsafe, uint8_t perception_mode,
                                   uint8_t avoidance_mode, float altitude_m,
                                   uint8_t *out, size_t out_cap,
                                   size_t *out_len);

#endif /* COMPANION_LINK_H */
