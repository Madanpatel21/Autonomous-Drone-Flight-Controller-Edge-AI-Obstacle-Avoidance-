#ifndef PERCEPTION_FUSION_H
#define PERCEPTION_FUSION_H
#include "fc_types.h"

/* Perception fusion (Phase 17, PERC-001..004). Fuses companion AI obstacle
 * sets (ICD-02 OBSTACLE_SET class) with FC-owned forward ToF/LiDAR ranging,
 * optical flow (advisory) and vehicle state into one bounded obstacle picture
 * in the vehicle FLU body frame (DEC-008/013).
 *
 * Safety boundary (DEC-007, AI-002, SAF-040): the picture is ADVISORY ONLY.
 * Consumers (avoidance, Phase 18) may steer within FC-owned velocity/accel
 * limits but never command attitude/rate directly and never override
 * failsafes. Loss of every perception source degrades to PERCEPTION_NONE and
 * must not by itself trigger a failsafe (companion loss is lowest in the
 * SAF-001 hierarchy). */

typedef enum {
    PERCEPTION_NONE = 0,   /* no fresh source: obstacle picture invalid */
    PERCEPTION_TOF_ONLY,   /* FC ranging only */
    PERCEPTION_AI_ONLY,    /* companion AI only (FC ranging stale/absent) */
    PERCEPTION_FUSED       /* AI + FC ranging both fresh */
} perception_mode_t;

typedef enum {
    PERC_SRC_NONE = 0,
    PERC_SRC_AI   = 1u << 0,
    PERC_SRC_TOF  = 1u << 1,
    PERC_SRC_FLOW = 1u << 2
} perc_source_t;

typedef struct {
    fc_vec3_t pos_m;        /* stability frame (body FLU rotated by est attitude) */
    fc_vec3_t pos_body_m;   /* body FLU frame as reported */
    fc_vec3_t vel_m_s;
    float radius_m;
    float confidence;       /* published (post-downgrade) 0..1 */
    uint8_t class_id;
    perc_source_t source;
} perception_obstacle_t;

typedef struct {
    perception_mode_t mode;
    bool valid;             /* mode != NONE and >=1 published obstacle */
    bool advisory;          /* constant true (DEC-007 boundary marker) */
    uint8_t count;
    perception_obstacle_t obs[FC_AI_MAX_DETECTIONS];
    float min_range_m;      /* nearest obstacle range (body xy), <0 if none */
    float min_bearing_rad;  /* FLU azimuth atan2(y, x): 0=forward, + = left */
    bool ai_healthy;        /* companion stream alive (<=1 s, SYS-004) */
    bool tof_healthy;       /* FC forward ranging alive */
    bool flow_healthy;      /* advisory egomotion source */
    uint64_t last_ai_us;    /* last accepted set timestamp (FC clock domain) */
    uint64_t last_tof_us;
} perception_state_t;

void perception_init(void);

/* Link-layer sink: the Phase 19 companion link parses ICD-02 frames into
 * fc_ai_obstacle_set_t and calls this. Enforces link rules here: count is
 * clamped to FC_AI_MAX_DETECTIONS, detections with confidence < 0.5 are
 * dropped (ICD-02 / SAF-041 / AI-001), invalid sets are ignored. */
void perception_feed_ai(const fc_ai_obstacle_set_t *set);

/* FC-owned forward range feed (hal_tof_read id 1). */
void perception_feed_tof(float range_m, uint64_t timestamp_us);

/* Optical flow feed: advisory egomotion source only (SIM backend stubbed
 * NOT_READY until the flow driver phase; never gates the mode by itself). */
void perception_feed_flow(const fc_flow_sample_t *f);

/* Vehicle state for body->stability-frame rotation (est attitude + altitude). */
void perception_set_vehicle_state(const fc_quat_t *q, float alt_m);

/* 50 Hz fusion tick: age-out (stale/invalid/failure), SAF-041 conflict
 * handling, mode computation, nearest-obstacle export. */
void perception_tick_50hz(uint64_t now_us);

const perception_state_t *perception_get(void);

#endif /* PERCEPTION_FUSION_H */
