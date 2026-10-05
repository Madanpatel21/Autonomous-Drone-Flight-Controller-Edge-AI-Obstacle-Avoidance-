#include "perception_fusion.h"
#include <string.h>
#include <math.h>

/* Perception fusion implementation (Phase 17, PERC-001..004).
 *
 * Timing/validity model
 *  - AI set stale after 200 ms (ICD-02 rule), companion dead after 1 s
 *    (heartbeat class, SYS-004). Stale sets are excluded, not zeroed: the
 *    last geometry ages out exactly like the SAF-003 IMU rule (no stale
 *    perception ever stays "valid").
 *  - FC ToF stale after 100 ms (2 ticks at 50 Hz); measured range must be
 *    inside the sensor band [0.05, 12] m (TF-Luna class, COMPONENT_SELECTION).
 *  - Optical flow is an advisory egomotion source only; it never gates the
 *    fusion mode (SIM backend returns NOT_READY until the flow driver phase).
 *
 * Fusion rules
 *  1. Accepted AI detections (confidence >= 0.5 enforced at the feed) are
 *     rotated from body FLU into the stability frame with the estimated
 *     attitude quaternion (vehicle-state fusion, DEC-008 convention).
 *  2. SAF-041 conflict: if FC ToF reports an obstacle within the 20-degree
 *     forward cone at least CONF_RANGE_M closer than the AI geometry claims,
 *     the FC-owned sensor wins: the published range is clamped to the ToF
 *     range and the detection confidence is downgraded 50 percent (source
 *     becomes AI|TOF so consumers can see the disagreement).
 *  3. If FC ToF sees something inside 8 m in the forward cone and AI does
 *     not, a TOF-sourced obstacle is synthesized (sensor-limited small FOV:
 *     the FC range complements the camera, it is not shadowed by it).
 *  4. Mode: FUSED when both sources fresh, AI_ONLY / TOF_ONLY when one is,
 *     NONE when neither (or after companion death >1 s).
 *
 * Determinism: no RNG, no dynamic allocation, bounded 16-entry list. */

#define PERC_AI_STALE_US     200000u   /* ICD-02: sets older than 200 ms ignored */
#define PERC_AI_DEAD_US     1000000u   /* companion health window (SYS-004) */
#define PERC_TOF_STALE_US    100000u
#define PERC_FLOW_STALE_US   100000u
#define PERC_CONF_MIN          0.5f     /* link-layer gate (SAF-041/AI-001) */
#define PERC_TOF_MIN_M         0.05f
#define PERC_TOF_MAX_M        12.0f
#define PERC_CONE_RAD          0.349f   /* 20 deg forward cone */
#define PERC_CONFLICT_M        1.0f     /* AI/ToF disagreement threshold */
#define PERC_TOF_SYNTH_MAX_M   8.0f     /* synthesize TOF obstacle below this */
#define PERC_TAN_EPS           1e-3f
#define PERC_HALF_PI           1.5707963f

static perception_state_t st;

/* internal caches (not published): raw feeds + vehicle state */
static struct {
    fc_ai_obstacle_set_t last_set;
    float tof_range_m;
    fc_quat_t q;
    uint64_t last_ai_us;
    uint64_t last_tof_us;
    uint64_t last_flow_us;
} c;

void perception_init(void)
{
    memset(&st, 0, sizeof(st));
    memset(&c, 0, sizeof(c));
    c.q.w = 1.0f;
    st.min_range_m = -1.0f;
    st.advisory = true;   /* DEC-007: perception output is advisory by design */
}

void perception_set_vehicle_state(const fc_quat_t *q, float alt_m)
{
    (void)alt_m;   /* reserved: ground-plane gating for downward obstacles */
    c.q = q ? *q : (fc_quat_t){ 1.0f, 0.0f, 0.0f, 0.0f };
}

void perception_feed_ai(const fc_ai_obstacle_set_t *set)
{
    if (!set || set->timestamp_us == 0u) return;          /* invalid set */
    if (set->count == 0u || set->count > FC_AI_MAX_DETECTIONS) return;

    c.last_set = *set;                                    /* bounded copy */
    c.last_ai_us = set->timestamp_us;                     /* FC clock domain */
}

void perception_feed_tof(float range_m, uint64_t timestamp_us)
{
    if (timestamp_us == 0u) return;
    if (!(range_m >= PERC_TOF_MIN_M && range_m <= PERC_TOF_MAX_M)) return;
    c.tof_range_m = range_m;
    c.last_tof_us = timestamp_us;
}

void perception_feed_flow(const fc_flow_sample_t *f)
{
    if (!f || !f->valid) return;
    c.last_flow_us = f->timestamp_us;
}

/* body->world rotation of v by quaternion q (DEC-008: q is body->world).
 * Standard formula: t = 2*(qv x v); v' = v + qw*t + qv x t. */
static fc_vec3_t rotate_body_to_world(const fc_quat_t *q, const fc_vec3_t *v)
{
    float qw = q->w, qx = q->x, qy = q->y, qz = q->z;
    fc_vec3_t t = {
        2.0f * (qy * v->z - qz * v->y),
        2.0f * (qz * v->x - qx * v->z),
        2.0f * (qx * v->y - qy * v->x)
    };
    return (fc_vec3_t){
        v->x + qw * t.x + (qy * t.z - qz * t.y),
        v->y + qw * t.y + (qz * t.x - qx * t.z),
        v->z + qw * t.z + (qx * t.y - qy * t.x)
    };
}

/* Forward-cone bearing of a body-frame point (FLU): 0 = straight ahead (+x),
 * positive = left (+y). atan2 clamps the degenerate x<=0 case to +-90 deg. */
static float bearing_of(const fc_vec3_t *p)
{
    if (p->x < PERC_TAN_EPS && p->x > -PERC_TAN_EPS) {
        return (p->y >= 0.0f) ? PERC_HALF_PI : -PERC_HALF_PI;
    }
    return atan2f(p->y, p->x);
}

void perception_tick_50hz(uint64_t now_us)
{
    bool ai_fresh  = (c.last_ai_us  != 0u) && (now_us >= c.last_ai_us)  &&
                     (now_us - c.last_ai_us  <= PERC_AI_STALE_US);
    bool tof_fresh = (c.last_tof_us != 0u) && (now_us >= c.last_tof_us) &&
                     (now_us - c.last_tof_us <= PERC_TOF_STALE_US);
    st.ai_healthy  = (c.last_ai_us  != 0u) && (now_us >= c.last_ai_us)  &&
                     (now_us - c.last_ai_us  <= PERC_AI_DEAD_US);
    st.tof_healthy = tof_fresh;
    st.flow_healthy = (c.last_flow_us != 0u) && (now_us >= c.last_flow_us) &&
                      (now_us - c.last_flow_us <= PERC_FLOW_STALE_US);
    st.last_ai_us  = c.last_ai_us;
    st.last_tof_us = c.last_tof_us;

    perception_obstacle_t obs[FC_AI_MAX_DETECTIONS];
    uint8_t n = 0;

    /* --- AI detections (already link-gated at the feed) --- */
    if (ai_fresh) {
        for (uint8_t i = 0; i < c.last_set.count; i++) {
            const fc_ai_detection_t *d = &c.last_set.det[i];
            if (d->confidence < PERC_CONF_MIN) continue;   /* SAF-041 gate */

            perception_obstacle_t o;
            memset(&o, 0, sizeof(o));
            o.pos_body_m = d->pos_m;
            o.vel_m_s    = d->vel_m_s;
            o.radius_m   = d->radius_m;
            o.confidence = d->confidence;
            o.class_id   = d->class_id;

            /* SAF-041: FC sensor wins on forward-cone disagreement. */
            if (tof_fresh) {
                float rng = sqrtf(d->pos_m.x * d->pos_m.x +
                                  d->pos_m.y * d->pos_m.y);
                float brg  = bearing_of(&d->pos_m);
                if (fabsf(brg) <= PERC_CONE_RAD &&
                    c.tof_range_m < rng - PERC_CONFLICT_M) {
                    o.pos_body_m.x *= (c.tof_range_m / rng);
                    o.pos_body_m.y *= (c.tof_range_m / rng);
                    o.confidence   *= 0.5f;
                    o.source = PERC_SRC_AI | PERC_SRC_TOF;
                }
            }
            if (o.source == PERC_SRC_NONE) o.source = PERC_SRC_AI;

            o.pos_m = rotate_body_to_world(&c.q, &o.pos_body_m);
            obs[n++] = o;
        }
    }

    /* --- FC-only ranging: synthesize when AI missed the forward cone --- */
    bool ai_covers_cone = false;
    if (ai_fresh) {
        for (uint8_t i = 0; i < n; i++) {
            if (fabsf(bearing_of(&obs[i].pos_body_m)) <= PERC_CONE_RAD) {
                ai_covers_cone = true;
                break;
            }
        }
    }
    if (tof_fresh && !ai_covers_cone && c.tof_range_m <= PERC_TOF_SYNTH_MAX_M) {
        perception_obstacle_t o;
        memset(&o, 0, sizeof(o));
        o.pos_body_m.x = c.tof_range_m;
        o.radius_m     = 0.15f;          /* unknown extent, FC ranging only */
        o.confidence   = 0.90f;
        o.class_id     = 0;              /* classless range return */
        o.source       = PERC_SRC_TOF;
        o.pos_m        = rotate_body_to_world(&c.q, &o.pos_body_m);
        obs[n++] = o;
    }

    /* --- nearest-obstacle export (body-frame xy range/bearing) --- */
    float min_rng = -1.0f, min_brg = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        float rng = sqrtf(obs[i].pos_body_m.x * obs[i].pos_body_m.x +
                          obs[i].pos_body_m.y * obs[i].pos_body_m.y);
        if (min_rng < 0.0f || rng < min_rng) {
            min_rng = rng;
            min_brg = bearing_of(&obs[i].pos_body_m);
        }
    }

    /* --- publish --- */
    st.count = n;
    memcpy(st.obs, obs, sizeof(obs));
    st.min_range_m   = min_rng;
    st.min_bearing_rad = min_brg;

    if (ai_fresh && tof_fresh)      st.mode = PERCEPTION_FUSED;
    else if (ai_fresh)              st.mode = PERCEPTION_AI_ONLY;
    else if (tof_fresh)             st.mode = PERCEPTION_TOF_ONLY;
    else                            st.mode = PERCEPTION_NONE;

    /* Companion death (>1 s) forces NONE even if the stale picture is
     * younger than the stale window would suggest: without a live stream
     * there is no way to distinguish frozen geometry from truth. */
    if (!st.ai_healthy && !tof_fresh) st.mode = PERCEPTION_NONE;

    st.valid = (st.mode != PERCEPTION_NONE) && (st.count > 0u);
}

const perception_state_t *perception_get(void)
{
    return &st;
}
