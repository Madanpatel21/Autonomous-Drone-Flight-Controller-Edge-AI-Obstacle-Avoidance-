#ifndef MISSION_SM_H
#define MISSION_SM_H
#include "fc_types.h"

/* Mission manager (NAV-001/003/004, Phase 14). Deterministic states:
 * IDLE -> TAKEOFF -> WAYPOINT* -> RTL -> LAND -> DONE.
 * RC loss in flight forces RTL (SAF-001 hook). Geofence checked each tick. */

#define MISSION_MAX_WP 16

typedef struct { float x_m; float y_m; } nav_xy_t;   /* NED horizontal, home-relative */

typedef enum {
    NAV_IDLE = 0,
    NAV_TAKEOFF,
    NAV_WAYPOINT,
    NAV_HOLD,
    NAV_RTL,
    NAV_LAND,
    NAV_DONE,
    NAV_ABORT     /* unrecoverable failure (e.g. IMU loss): stop commanding */
} nav_state_t;

typedef struct {
    nav_xy_t home;
    nav_xy_t wps[MISSION_MAX_WP];
    int      wp_count;
    int      wp_index;
    float    takeoff_alt_m;
    float    arrival_radius_m;
    float    geofence_radius_m;
    float    geofence_alt_m;
} mission_cfg_t;

void mission_init(const mission_cfg_t *cfg);
int  mission_load_waypoints(const nav_xy_t *wps, int n);
void mission_start(void);                       /* only from IDLE */
/* Unrecoverable failure: stop commanding attitude/altitude (SAF-001/003).
 * Used when attitude control is impossible (IMU loss) — the vehicle can no
 * longer be flown to a controlled landing, so the mission must not keep
 * issuing setpoints. Distinct from NAV_DONE (successful landing). */
void mission_abort(void);
/* Force an immediate controlled descent from any airborne state (SAF-030
 * battery-critical, estimator loss). No-op when idle/done/aborted. */
void mission_land_now(void);
/* Explicit RTL request from the safety action model (SAF-030 RTL band, SAF-001
 * geofence/RC tiers). Idempotent; never preempts LAND/DONE/ABORT. */
void mission_request_rtl(void);

/* 50 Hz tick. pos/alt from the localization layer; rc_ok from the RC driver. */
void mission_tick(nav_xy_t pos, float alt_m, bool rc_ok);

nav_state_t mission_state(void);
int         mission_current_wp(void);

/* outputs for the control layer */
float       mission_altitude_sp(void);          /* NAN when not applicable */
nav_xy_t    mission_target_xy(void);

/* monitoring */
bool        mission_geofence_violated(void);
const char* mission_state_name(void);

#endif
