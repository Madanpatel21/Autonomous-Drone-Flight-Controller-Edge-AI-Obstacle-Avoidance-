#include "failsafe_sm.h"
#include "hal_interfaces.h"
#include <string.h>

/* Monitors: RC (SYS-003, 500 ms), battery (SAF-030/031), deadline (SAF-004),
 * IMU health (SAF-003), geofence + estimator health (Phase 24).
 *
 * Level vs latch (Phase 16 root-cause fix): the first version escalated
 * one-way and never de-escalated, so a recovered RC left FC_FAILSAFE_RC_LOSS
 * active forever, masking any later battery failsafe. RC and IMU are LEVEL
 * conditions: they clear when the sensor recovers. Battery-critical is
 * LATCHED with hysteresis (clears only above the RTL threshold) because cell
 * sag recovers under reduced load and would otherwise flap the failsafe.
 *
 * Phase 24: three failsafe states existed in fc_failsafe_t but nothing could
 * ever produce them (GEOFENCE, ESTIMATOR, COMPANION). Geofence and estimator
 * now have real levels; companion stays deliberately NOT a failsafe (DEC-007,
 * advisory only) and is reported through the companion-health field instead.
 */

#define RC_TIMEOUT_US       500000u
#define IMU_TIMEOUT_US      100000u   /* 100 ms of no valid IMU -> IMU failure */
#define BATT_WARN_V         3.5f
#define BATT_RTL_V          3.4f
#define BATT_LAND_V         3.1f
#define DEADLINE_STOP_COUNT 100u      /* > this many overruns: stop commanding */

typedef struct {
    uint64_t last_rc_us;
    uint64_t last_imu_us;
    bool     imu_valid_seen;   /* an IMU sample has arrived since init */
    bool     batt_rtl_latched; /* SAF-031: sticky once below RTL threshold */
    bool     batt_critical;    /* level, hysteresis vs BATT_RTL_V */
    bool     est_ever_healthy; /* GNSS TTFF must not look like an estimator loss */
    bool     est_healthy;
    bool     fence_violated;
    uint32_t deadline_misses;
} fs_t;

static fs_t f;

/* Shared predicate: is attitude authority unavailable? Both the IMU health
 * path and the deadline path can make the control loop untrustworthy, and both
 * therefore remove every flyable action (DEC-021). */
static bool attitude_authority_lost(void)
{
    uint64_t now = hal_time_us();
    bool imu_dead = f.imu_valid_seen && (now - f.last_imu_us) > IMU_TIMEOUT_US;
    return imu_dead || f.deadline_misses > DEADLINE_STOP_COUNT;
}

void failsafe_init(void)
{
    memset(&f, 0, sizeof(f));
    f.last_rc_us  = hal_time_us();
    f.last_imu_us = hal_time_us();
    f.est_healthy = false;
}

void failsafe_monitor(void)
{
    /* conditions are re-derived from levels on every call (SAF-001 order:
     * RC loss > IMU > battery > geofence > companion) */
    /* nothing to do here for battery (level set by failsafe_battery_update) */
}

void failsafe_rc_keepalive(void) { f.last_rc_us = hal_time_us(); }

void failsafe_imu_update(bool imu_valid)
{
    uint64_t now = hal_time_us();
    if (imu_valid) {
        f.last_imu_us = now;
        f.imu_valid_seen = true;
        return;
    }
    /* invalid sample: the level clears only after the timeout is exceeded by
     * a FRESH valid sample (i.e. recovery is proven, not assumed) */
    (void)now;
}

void failsafe_notify_geofence(bool violated) { f.fence_violated = violated; }

void failsafe_notify_estimator(bool healthy)
{
    f.est_healthy = healthy;
    if (healthy) f.est_ever_healthy = true;
}

void failsafe_battery_update(const fc_battery_sample_t *b)
{
    if (!b || !b->valid || b->cell_v[0] <= 0.0f) return;

    float min_cell = b->cell_v[0];
    for (int i = 1; i < 4; i++) if (b->cell_v[i] < min_cell) min_cell = b->cell_v[i];

    if (min_cell < BATT_LAND_V) {
        /* first observation already critical -> LAND (SAF-031 latch only
         * applies to packs seen crossing the RTL band) */
        f.batt_critical = true;
    } else if (min_cell < BATT_RTL_V) {
        f.batt_rtl_latched = true;   /* in RTL band -> latch RTL intent */
    } else {
        f.batt_critical = false;     /* recovered above RTL: hysteresis band */
    }
    (void)BATT_WARN_V;
}

void failsafe_notify_deadline_miss(void)
{
    f.deadline_misses++;
}

/* Active failsafe (SAF-001 priority order, re-derived each query). */
fc_failsafe_t failsafe_active(void)
{
    uint64_t now = hal_time_us();
    bool rc_lost  = (now - f.last_rc_us) > RC_TIMEOUT_US;
    bool imu_dead = f.imu_valid_seen && (now - f.last_imu_us) > IMU_TIMEOUT_US;

    if (rc_lost)              return FC_FAILSAFE_RC_LOSS;
    if (imu_dead)             return FC_FAILSAFE_IMU;
    if (f.batt_critical)      return FC_FAILSAFE_BATTERY;
    if (f.deadline_misses > DEADLINE_STOP_COUNT) return FC_FAILSAFE_IMU;  /* SAF-004 path */
    if (f.fence_violated)     return FC_FAILSAFE_GEOFENCE;
    if (f.est_ever_healthy && !f.est_healthy) return FC_FAILSAFE_ESTIMATOR;
    return FC_FAILSAFE_NONE;
}

/* Safety action by flyability (Phase 24, DEC-021, SAF-002).
 *
 * Precondition table, evaluated in this order:
 *   attitude authority lost  -> MOTOR_STOP (nothing else is flyable)
 *   RC lost                  -> RTL      (attitude + altitude available)
 *   battery critical         -> LAND (RTL when the pack crossed the RTL band)
 *   geofence breached        -> RTL
 *   estimator lost           -> LAND     (no position, so RTL is not flyable)
 *   companion lost           -> HOLD     (advisory only, DEC-007)
 *
 * The first rule is the one the old code got wrong: with RC lost AND the IMU
 * dead, the report said RC_LOSS (SAF-001 order) and the implied action was RTL,
 * while the vehicle in fact had zero thrust and no control authority at all. */
fc_safety_action_t failsafe_action(void)
{
    uint64_t now = hal_time_us();
    bool rc_lost = (now - f.last_rc_us) > RC_TIMEOUT_US;

    if (attitude_authority_lost()) return FC_ACTION_MOTOR_STOP;
    if (rc_lost)                   return FC_ACTION_RTL;
    /* SAF-030: crossing the RTL band (3.4 V/cell) must make the vehicle go
     * home, not just warn. The old code only produced an action once the pack
     * was already below the land threshold, so batt_rtl_latched was recorded
     * and then ignored — the requirement was unit-tested through
     * failsafe_mode_request(), which the application never called. A pack first
     * seen below the land threshold has no latch and lands (SAF-031). */
    if (f.batt_critical || f.batt_rtl_latched) {
        return f.batt_rtl_latched ? FC_ACTION_RTL : FC_ACTION_LAND;
    }
    if (f.fence_violated)          return FC_ACTION_RTL;
    if (f.est_ever_healthy && !f.est_healthy) return FC_ACTION_LAND;
    return FC_ACTION_NONE;
}

fc_mode_t failsafe_mode_request(void)
{
    switch (failsafe_action()) {
    case FC_ACTION_RTL:        return FC_MODE_RTL;
    case FC_ACTION_LAND:       return FC_MODE_LAND;
    case FC_ACTION_HOLD:       return FC_MODE_POS_HOLD;  /* FC-only fallback, SYS-004 */
    case FC_ACTION_MOTOR_STOP: return FC_MODE_FAILSAFE;   /* attitude not controllable */
    case FC_ACTION_NONE:
    default:                   return FC_MODE_DISARMED;
    }
}