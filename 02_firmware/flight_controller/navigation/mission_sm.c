#include "mission_sm.h"
#include <math.h>
#include <string.h>

typedef struct {
    mission_cfg_t cfg;
    nav_state_t   state;
    int           wp_index;
    float         alt_sp;
    nav_xy_t      target;
    bool          rc_ok_last;
    bool          fence;
} mission_t;

static mission_t m;

static float dist2(nav_xy_t a, nav_xy_t b)
{
    float dx = a.x_m - b.x_m, dy = a.y_m - b.y_m;
    return sqrtf(dx*dx + dy*dy);
}

void mission_init(const mission_cfg_t *cfg)
{
    memset(&m, 0, sizeof(m));
    if (cfg) {
        m.cfg = *cfg;
    } else {
        m.cfg.takeoff_alt_m = 1.5f;
        m.cfg.arrival_radius_m = 1.5f;
        m.cfg.geofence_radius_m = 50.0f;
        m.cfg.geofence_alt_m = 30.0f;
    }
    m.state = NAV_IDLE;
    m.alt_sp = NAN;
    m.rc_ok_last = true;
}

int mission_load_waypoints(const nav_xy_t *wps, int n)
{
    if (!wps || n < 0 || n > MISSION_MAX_WP) return -1;
    memcpy(m.cfg.wps, wps, (size_t)n * sizeof(nav_xy_t));
    m.cfg.wp_count = n;
    return 0;
}

void mission_start(void)
{
    if (m.state != NAV_IDLE) return;
    m.wp_index = 0;
    m.state = NAV_TAKEOFF;
    m.alt_sp = m.cfg.takeoff_alt_m;
}

static void to_land(void)
{
    m.state = NAV_LAND;
    m.alt_sp = 0.0f;
}

void mission_abort(void)
{
    if (m.state == NAV_IDLE || m.state == NAV_DONE || m.state == NAV_ABORT) return;
    m.state = NAV_ABORT;
    m.alt_sp = NAN;
}

void mission_land_now(void)
{
    if (m.state == NAV_IDLE || m.state == NAV_DONE || m.state == NAV_ABORT ||
        m.state == NAV_LAND) return;
    to_land();
}

/* Explicit return-to-home request (Phase 24, SAF-030 RTL band / SAF-001).
 * The RC-loss and geofence paths already forced RTL inside mission_tick(); the
 * battery RTL band did not, so a pack between 3.1 and 3.4 V/cell kept flying
 * the mission: batt_rtl_latched was recorded but nothing acted on it, because
 * failsafe_active() only reports BATTERY below 3.1 V. The action model now
 * routes the latched band here, which is what SAF-030 actually requires.
 * Idempotent, and it never preempts LAND or a finished mission. */
void mission_request_rtl(void)
{
    if (m.state == NAV_IDLE || m.state == NAV_DONE || m.state == NAV_ABORT ||
        m.state == NAV_LAND) return;
    m.state = NAV_RTL;
    m.target = m.cfg.home;
    m.alt_sp = m.cfg.takeoff_alt_m;
}

void mission_tick(nav_xy_t pos, float alt_m, bool rc_ok)
{
    /* geofence check (NAV-004): breach -> RTL.
     * LAND must NOT be preempted: descending resolves the altitude breach and
     * escalating would oscillate RTL<->LAND forever (found in end-to-end sim). */
    float home_dist = dist2(pos, m.cfg.home);
    m.fence = (home_dist > m.cfg.geofence_radius_m) || (alt_m > m.cfg.geofence_alt_m);
    if (m.fence && m.state != NAV_IDLE && m.state != NAV_DONE &&
        m.state != NAV_RTL && m.state != NAV_LAND && m.state != NAV_ABORT) {
        m.state = NAV_RTL;
        m.target = m.cfg.home;
        m.alt_sp = m.cfg.takeoff_alt_m;
        return;
    }

    /* RC loss forces RTL from any airborne state (SAF-001) */
    if (!rc_ok && m.rc_ok_last) {
        if (m.state != NAV_IDLE && m.state != NAV_DONE && m.state != NAV_LAND &&
            m.state != NAV_ABORT) {
            m.state = NAV_RTL;
            m.target = m.cfg.home;
            m.alt_sp = m.cfg.takeoff_alt_m;
        }
    }
    m.rc_ok_last = rc_ok;

    switch (m.state) {
    case NAV_IDLE:
    case NAV_DONE:
    case NAV_ABORT:
        break;   /* aborted: no setpoints, no transitions */

    case NAV_TAKEOFF:
        m.alt_sp = m.cfg.takeoff_alt_m;
        if (alt_m >= m.cfg.takeoff_alt_m - 0.30f) {
            if (m.cfg.wp_count > 0) {
                m.state = NAV_WAYPOINT;
                m.target = m.cfg.wps[0];
            } else {
                m.state = NAV_HOLD;
                m.target = pos;
            }
        }
        break;

    case NAV_WAYPOINT:
        m.target = m.cfg.wps[m.wp_index];
        if (dist2(pos, m.target) <= m.cfg.arrival_radius_m) {
            m.wp_index++;
            if (m.wp_index >= m.cfg.wp_count) {
                m.state = NAV_RTL;
                m.target = m.cfg.home;
            }
        }
        break;

    case NAV_HOLD:
        /* HOLD latches the position captured at the TAKEOFF->HOLD transition
         * and keeps commanding it. Re-latching `pos` every tick (the previous
         * behaviour) made position hold a no-op that simply followed the
         * vehicle: the Phase 21 wind test measured a 4.4 m downwind drift with
         * the mission in HOLD, because the target drifted with it. */
        break;

    case NAV_RTL:
        m.target = m.cfg.home;
        m.alt_sp = m.cfg.takeoff_alt_m;
        if (dist2(pos, m.cfg.home) <= m.cfg.arrival_radius_m) {
            to_land();
        }
        break;

    case NAV_LAND:
        m.alt_sp = 0.0f;
        if (alt_m < 0.20f) {
            m.state = NAV_DONE;
            m.alt_sp = NAN;
        }
        break;
    }
}

nav_state_t mission_state(void) { return m.state; }
int mission_current_wp(void) { return m.wp_index; }
float mission_altitude_sp(void) { return m.alt_sp; }
nav_xy_t mission_target_xy(void) { return m.target; }
bool mission_geofence_violated(void) { return m.fence; }

const char* mission_state_name(void)
{
    switch (m.state) {
    case NAV_IDLE:     return "IDLE";
    case NAV_TAKEOFF:  return "TAKEOFF";
    case NAV_WAYPOINT: return "WAYPOINT";
    case NAV_HOLD:     return "HOLD";
    case NAV_RTL:      return "RTL";
    case NAV_LAND:     return "LAND";
    case NAV_DONE:     return "DONE";
    case NAV_ABORT:    return "ABORT";
    }
    return "?";
}
