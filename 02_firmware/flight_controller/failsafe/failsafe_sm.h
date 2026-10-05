#ifndef FAILSAFE_SM_H
#define FAILSAFE_SM_H
#include "fc_types.h"

/* Failsafe hierarchy (SAF-001, DEC-006): RC loss > IMU/battery > geofence >
 * companion loss. That order ranks which condition is REPORTED.
 *
 * What the vehicle DOES is decided separately by flyability
 * (failsafe_action(), Phase 24 / DEC-021): every action except motor-stop needs
 * attitude authority, which needs the IMU. So a dead IMU forces
 * FC_ACTION_MOTOR_STOP even when RC loss is also present and out-ranks it in
 * the report. Both facts are published: `failsafe_active()` says why,
 * `failsafe_action()` says what can still be done. */

void failsafe_init(void);
void failsafe_monitor(void);                       /* 100 Hz (SYS-003) */
void failsafe_rc_keepalive(void);                  /* call on each valid RC frame */
void failsafe_battery_update(const fc_battery_sample_t *b);
void failsafe_imu_update(bool imu_valid);           /* SAF-003, each control tick */
/* SAF-001 geofence tier: a breached fence is a reported failsafe, not just a
 * mission transition, so it is visible to the operator on the wire. Level
 * (clears when the vehicle is back inside). */
void failsafe_notify_geofence(bool violated);
/* Estimator tier: level, but gated by an "ever healthy" latch so the GNSS
 * 3 s time-to-first-fix (or a pre-fix start) can never trip a failsafe. The
 * vehicle descends in place because RTL is not flyable without a position. */
void failsafe_notify_estimator(bool healthy);
void failsafe_notify_deadline_miss(void);          /* SAF-004 */

fc_failsafe_t      failsafe_active(void);
/* The action actually taken, selected by flyability (SAF-002, DEC-021). */
fc_safety_action_t failsafe_action(void);
/* Compatibility helper: mode implied by the active failsafe (used by tests and
 * by callers that only need a mode). Note that a flyability-forced motor stop
 * can coincide with a mode that implies a flyable return; prefer
 * failsafe_action() in the control path. */
fc_mode_t          failsafe_mode_request(void);

#endif