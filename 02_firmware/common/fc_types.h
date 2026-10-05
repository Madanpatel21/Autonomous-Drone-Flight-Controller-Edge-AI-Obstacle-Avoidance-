#ifndef FC_TYPES_H
#define FC_TYPES_H

/* Shared data types for the flight-control application. Hardware-independent. */
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FC_OK = 0,
    FC_ERROR,
    FC_TIMEOUT,
    FC_INVALID,
    FC_BUSY
} fc_status_t;

typedef enum {
    FC_MODE_DISARMED = 0,
    FC_MODE_STABILIZE,
    FC_MODE_ALT_HOLD,
    FC_MODE_POS_HOLD,
    FC_MODE_MISSION,
    FC_MODE_RTL,
    FC_MODE_LAND,
    FC_MODE_FAILSAFE
} fc_mode_t;

typedef enum {
    FC_FAILSAFE_NONE = 0,
    FC_FAILSAFE_RC_LOSS,
    FC_FAILSAFE_BATTERY,
    FC_FAILSAFE_GEOFENCE,
    FC_FAILSAFE_IMU,
    FC_FAILSAFE_ESTIMATOR,
    FC_FAILSAFE_COMPANION
} fc_failsafe_t;

/* Safety ACTION actually taken, chosen by flyability rather than by which
 * failure was detected first (Phase 24, DEC-021, SAF-002).
 *
 * The reported failsafe says WHY control was taken over; the action says WHAT
 * the vehicle can still do about it. Reporting only the failsafe name made a
 * motor-stop look like "returning to land" whenever the IMU had also failed,
 * which is the single most dangerous thing an operator can be told wrongly. */
typedef enum {
    FC_ACTION_NONE = 0,     /* no failsafe: mission flies normally */
    FC_ACTION_HOLD,         /* hover in place (attitude + altitude authority) */
    FC_ACTION_RTL,          /* return to home (attitude + altitude + position) */
    FC_ACTION_LAND,         /* descend and land in place (attitude + altitude) */
    FC_ACTION_MOTOR_STOP    /* no flyable action exists: cut thrust (SAF-002) */
} fc_safety_action_t;

/* vectors */
typedef struct { float x, y, z; } fc_vec3_t;
typedef struct { float w, x, y, z; } fc_quat_t;

/* raw/calibrated IMU sample, SI units */
typedef struct {
    uint64_t timestamp_us;
    fc_vec3_t gyro_rad_s;   /* body frame */
    fc_vec3_t accel_m_s2;   /* body frame */
    bool valid;
} fc_imu_sample_t;

typedef struct {
    uint64_t timestamp_us;
    float pressure_pa;
    float temperature_c;
    bool valid;
} fc_baro_sample_t;

typedef struct {
    uint64_t timestamp_us;
    double lat_deg, lon_deg;
    float alt_m;            /* MSL */
    float vn_ms, ve_ms, vd_ms; /* NED velocity */
    uint8_t sats;
    bool fix_valid;
    bool valid;
} fc_gnss_sample_t;

typedef struct {
    uint64_t timestamp_us;
    float range_m;
    bool valid;
} fc_tof_sample_t;

typedef struct {
    uint64_t timestamp_us;
    float flow_x_px, flow_y_px; /* integrated pixel flow */
    float quality;              /* 0..1 */
    bool valid;
} fc_flow_sample_t;

typedef struct {
    uint64_t timestamp_us;
    float pack_v;
    float current_a;
    float cell_v[4];
    float consumed_mah;
    bool valid;
} fc_battery_sample_t;

typedef struct {
    uint64_t timestamp_us;
    uint16_t channels[8];     /* normalized 1000..2000 */
    bool frames_valid;
    bool failsafe_active;     /* reported by RC link */
    bool valid;
} fc_rc_frame_t;

/* ---- perception (Phase 17, ICD-02 OBSTACLE_SET payload classes) ----
 * Wire frame is FLU body axes (DEC-013): +x forward, +y left, +z up;
 * positions m, velocities m/s, radius m, confidence 0..1. */
#define FC_AI_MAX_DETECTIONS 16u    /* ICD-02: count <= 16 per set */

typedef struct {
    fc_vec3_t pos_m;          /* FLU body frame of companion reference */
    fc_vec3_t vel_m_s;
    float radius_m;
    float confidence;         /* 0..1; <0.5 dropped at link layer (SAF-041) */
    uint8_t class_id;
} fc_ai_detection_t;

typedef struct {
    uint64_t timestamp_us;    /* companion capture time, FC clock domain */
    uint8_t count;
    fc_ai_detection_t det[FC_AI_MAX_DETECTIONS];
} fc_ai_obstacle_set_t;

#endif /* FC_TYPES_H */
