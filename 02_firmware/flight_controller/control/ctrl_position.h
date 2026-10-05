#ifndef CTRL_POSITION_H
#define CTRL_POSITION_H
#include "fc_types.h"
#include "est_position.h"

/* Horizontal position/velocity controller (Phase 21, CTRL-001/003, DEC-017).
 *
 * Completes the cascade that earlier phases left open: mission/avoidance give
 * a horizontal velocity or position request, this controller produces the
 * acceleration, maps it to a bounded TILT setpoint (theta = atan(a/g), the
 * standard small-angle quad mapping), and hands it to the existing attitude
 * loop. That is what makes the SIM position loop — and therefore the Phase 18
 * avoidance setpoint and the Phase 14 mission — actually flown.
 *
 * Limits are FC-owned constants (AF class): velocity, acceleration and tilt
 * are clamped here, so no upstream request (mission, avoidance, ground
 * station) can demand more than the airframe limits allow.
 *
 * Gains: kp_pos / kp_vel were tuned in the Phase 21 sim sweep (see
 * SIM_TUNING_RECORD.md); they are sim-verified, not hardware-verified. */

#define AF_VEL_MAX_M_S   2.5f    /* horizontal speed limit */
#define AF_ACC_MAX_M_S2  3.0f    /* horizontal acceleration limit */
#define AF_TILT_MAX_RAD  0.4363f /* 25 deg */

void ctrl_position_init(void);

/* Mission target (world frame, metres, home-relative). */
void ctrl_position_setpoint_xy(float x_m, float y_m);

/* Velocity override (world frame) from the avoidance layer; when active it
 * replaces the position-loop velocity request (DEC-014 advisory contract). */
void ctrl_position_vel_override(const fc_vec3_t *v_world_m_s, bool active);

/* 50 Hz: estimate -> velocity -> acceleration -> tilt -> attitude setpoint. */
void ctrl_position_loop(float dt_s, const fc_quat_t *att_q);

/* last commanded velocity/acceleration (tests, telemetry, diagnostics) */
fc_vec3_t ctrl_position_vel_sp(void);
fc_vec3_t ctrl_position_acc_sp(void);
float     ctrl_position_tilt_rad(void);

#endif /* CTRL_POSITION_H */
