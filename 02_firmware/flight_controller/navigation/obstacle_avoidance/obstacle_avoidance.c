#include "obstacle_avoidance.h"
#include <string.h>
#include <math.h>

/* Obstacle avoidance implementation (Phase 18, SAF-040, DEC-014).
 *
 * Policy (deterministic, bounded, FC-owned):
 *  1. Threat selection: the nearest perception obstacle inside the ±45°
 *     forward cone. Outside the cone = not a collision risk for a forward-
 *     translating vehicle (documented simplification: the vehicle has no
 *     lateral mission motion in SIM yet, so cone-scoped threats are the ones
 *     that matter; a full local obstacle map/grid is Phase 18+ scope and is
 *     listed honestly as a gap, not claimed).
 *  2. Forward speed: linearly scaled from AVOID_V_FWD_MAX at the brake range
 *     down to 0 at the stop range (range-based brake curve); inside the stop
 *     range the forward command is exactly zero (deterministic safe stop).
 *  3. Lateral offset: steer away from the threat bearing, magnitude linear in
 *     threat proximity, clamped to AVOID_V_LAT_MAX.
 *  4. SAF-040 clamp: every published component is clamped to FC-owned limits
 *     after policy evaluation — the clamp is the LAST step, so no input
 *     (obstacle velocity, confidence, corrupted range) can exceed it.
 *  5. Perception invalid/empty → AVOID_NONE, command zero (deterministic
 *     degraded behavior: keep the mission, no avoidance action).
 *
 * No RNG, no dynamic allocation; pure function of the perception picture. */

static avoid_cmd_t cmd;

void avoidance_init(void)
{
    memset(&cmd, 0, sizeof(cmd));
    cmd.advisory = true;   /* DEC-007 boundary marker */
    cmd.threat_range_m = -1.0f;
}

static float clampf(float v, float lim)
{
    if (v > lim)  return lim;
    if (v < -lim) return -lim;
    return v;
}

void avoidance_update(const perception_state_t *p, uint64_t now_us)
{
    (void)now_us;
    memset(&cmd.vel_sp_body_m_s, 0, sizeof(cmd.vel_sp_body_m_s));
    cmd.threat_range_m   = -1.0f;
    cmd.threat_bearing_rad = 0.0f;
    cmd.mode = AVOID_NONE;
    cmd.active = false;

    if (!p || !p->valid || !p->advisory || p->count == 0u) {
        return;   /* deterministic degraded behavior (DEC-013 modes) */
    }

    /* --- threat selection: nearest obstacle in the forward cone --- */
    float best = -1.0f, best_brg = 0.0f;
    for (uint8_t i = 0; i < p->count; i++) {
        float rng = sqrtf(p->obs[i].pos_body_m.x * p->obs[i].pos_body_m.x +
                          p->obs[i].pos_body_m.y * p->obs[i].pos_body_m.y);
        float brg = atan2f(p->obs[i].pos_body_m.y, p->obs[i].pos_body_m.x);
        if (fabsf(brg) > AVOID_CONE_RAD) continue;
        if (best < 0.0f || rng < best) {
            best = rng;
            best_brg = brg;
        }
    }
    if (best < 0.0f) return;   /* threats exist but none in the cone */

    cmd.threat_range_m   = best;
    cmd.threat_bearing_rad = best_brg;

    /* --- forward speed: range-based brake curve, hard stop inside stop range.
     *     Threat beyond the brake envelope = no intervention yet (mode NONE,
     *     zero command; threat fields stay recorded for telemetry). */
    if (best >= AVOID_BRAKE_RANGE_M) {
        return;   /* tracked, but no intervention outside the brake envelope */
    }
    float vx = 0.0f;
    if (best <= AVOID_RETREAT_RANGE_M) {
        /* DEC-018: holding station is NOT safe against a closing obstacle —
         * the SIM environment advances 0.5 m/s and a mission keeps pushing
         * toward the target, so a pure stop was driven into contact (measured
         * clearance 0.30 m = contact floor before this rule). Inside the
         * retreat range the vehicle backs away at a bounded speed. */
        cmd.mode = AVOID_RETREAT;
        vx = -AVOID_V_RETREAT_M_S;
    } else if (best <= AVOID_STOP_RANGE_M) {
        cmd.mode = AVOID_STOP;
        vx = 0.0f;   /* deterministic safe stop */
    } else {
        cmd.mode = AVOID_SLOW;
        float scale = (best - AVOID_STOP_RANGE_M) /
                      (AVOID_BRAKE_RANGE_M - AVOID_STOP_RANGE_M);
        vx = AVOID_V_FWD_MAX_M_S * scale;
    }

    /* --- lateral offset: steer away from the threat (positive bearing =
     *     threat on the left -> steer right = negative y command) --- */
    float vy = 0.0f;
    if (cmd.mode != AVOID_NONE && best < AVOID_BRAKE_RANGE_M &&
        cmd.mode != AVOID_RETREAT) {
        float prox = 1.0f - (best - AVOID_STOP_RANGE_M) /
                            (AVOID_BRAKE_RANGE_M - AVOID_STOP_RANGE_M);
        if (prox < 0.0f) prox = 0.0f;
        if (fabsf(best_brg) > 1e-3f) {
            vy = -(best_brg > 0.0f ? 1.0f : -1.0f) * AVOID_V_LAT_MAX_M_S * prox;
        }
    }

    /* --- SAF-040 clamp: LAST step, nothing downstream can exceed these --- */
    cmd.vel_sp_body_m_s.x = clampf(vx, AVOID_V_FWD_MAX_M_S);
    cmd.vel_sp_body_m_s.y = clampf(vy, AVOID_V_LAT_MAX_M_S);
    cmd.vel_sp_body_m_s.z = 0.0f;   /* vertical stays mission-owned (DEC-011) */
    cmd.active = (cmd.mode != AVOID_NONE);
}

const avoid_cmd_t *avoidance_get(void)
{
    return &cmd;
}
