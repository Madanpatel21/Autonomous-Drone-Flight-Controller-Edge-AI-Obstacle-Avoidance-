#ifndef OBSTACLE_AVOIDANCE_H
#define OBSTACLE_AVOIDANCE_H
#include "fc_types.h"
#include "perception_fusion.h"

/* Obstacle avoidance (Phase 18, SAF-040, DEC-014). Consumers of the Phase 17
 * perception picture; publishes a bounded avoidance velocity setpoint in the
 * vehicle FLU body frame. ADVISORY: the avoidance module owns every limit and
 * clamps its own output before publishing — no upstream input (obstacle
 * velocity, confidence, count) can push a command past these FC-owned
 * constants. Vertical motion is mission-owned; avoidance never commands vz
 * (altitude is the failsafe-critical axis, DEC-011).
 *
 * Degraded behavior is deterministic and safe: with an invalid or empty
 * perception picture the module is inactive and commands zero. Loss of
 * perception never triggers a failsafe (DEC-007). */

typedef enum {
    AVOID_NONE = 0,   /* no threat / perception invalid: command zero      */
    AVOID_SLOW,       /* threat inside BRAKE_RANGE: forward speed scaled   */
    AVOID_STOP,       /* threat inside STOP_RANGE: forward speed zero      */
    AVOID_RETREAT     /* threat inside RETREAT_RANGE: bounded reverse (DEC-018) */
} avoid_mode_t;

typedef struct {
    avoid_mode_t mode;
    bool active;          /* mode != AVOID_NONE */
    bool advisory;        /* constant true (DEC-007 boundary marker) */
    fc_vec3_t vel_sp_body_m_s;   /* FLU body-frame velocity setpoint */
    float threat_range_m;        /* range that drove the mode, <0 if none */
    float threat_bearing_rad;    /* FLU azimuth of that threat */
} avoid_cmd_t;

void avoidance_init(void);

/* 50 Hz update: collision-risk estimation + bounded lateral/forward command. */
void avoidance_update(const perception_state_t *p, uint64_t now_us);

const avoid_cmd_t *avoidance_get(void);

/* FC-owned limits (SAF-040). Exposed for tests and the Phase 21 tuner; the
 * avoidance module clamps every published component to these. */
#define AVOID_V_FWD_MAX_M_S   2.0f
#define AVOID_V_LAT_MAX_M_S   1.0f
#define AVOID_V_RETREAT_M_S   1.0f
#define AVOID_BRAKE_RANGE_M   4.0f
#define AVOID_STOP_RANGE_M    2.0f
#define AVOID_RETREAT_RANGE_M 1.5f
#define AVOID_CONE_RAD        0.785f   /* ±45 deg forward threat cone */

#endif /* OBSTACLE_AVOIDANCE_H */
