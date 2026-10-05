#ifndef HAL_INTERFACES_H
#define HAL_INTERFACES_H

/* HAL interface contract (ICD-01). Header-only; backends implement in hal/sim and hal/stm32.
 * Nothing in the application may include a backend header (FW-001).
 * All calls must be non-blocking in the control path (FW-003). */
#include "fc_types.h"

#define HAL_INTERFACE_VERSION 1

typedef enum {
    HAL_OK = 0,
    HAL_ERROR,
    HAL_TIMEOUT,
    HAL_BUSY,
    HAL_NOT_READY
} hal_status_t;

/* ---- time (DEC-004) ---- */
uint64_t hal_time_us(void);
void     hal_sleep_until_us(uint64_t deadline_us);

/* ---- IMU (HW-002) ---- */
hal_status_t hal_imu_read(fc_imu_sample_t *out);

/* ---- barometer (HW-003) ---- */
hal_status_t hal_baro_read(fc_baro_sample_t *out);

/* ---- GNSS (HW-004) ---- */
hal_status_t hal_gnss_read(fc_gnss_sample_t *out);

/* ---- ToF range (HW-005); id 0 = downward, 1 = forward ---- */
hal_status_t hal_tof_read(int id, fc_tof_sample_t *out);

/* ---- optical flow (HW-005) ---- */
hal_status_t hal_flow_read(fc_flow_sample_t *out);

/* ---- companion perception stream (Phase 17; ICD-02 OBSTACLE_SET class).
 * SIM backend: deterministic virtual companion. On STM32 this call site is
 * replaced by the UART framed link (DEC-003, Phase 19); the FC-side sink
 * (perception_feed_ai) does not change. */
hal_status_t hal_companion_ai_read(fc_ai_obstacle_set_t *out);

/* Byte-level companion UART source (Phase 19, DEC-003): the virtual companion
 * emits REAL ICD-02 frames so the FC link exercises serialize/CRC/parse.
 * STM32: USART6 + DMA RX task replaces this hook; returns 0 there. */
size_t hal_companion_uart_read(uint8_t *out, size_t cap, size_t *n_out);

/* ---- battery (HW-007) ---- */
hal_status_t hal_battery_read(fc_battery_sample_t *out);

/* ---- RC (ICD-04) ---- */
hal_status_t hal_rc_read(fc_rc_frame_t *out);

/* ---- actuators: motor commands 0..1, quad-X order M1..M4 (HW-006) ---- */
hal_status_t hal_actuator_write(const float motor[4]);

/* ---- nonvolatile parameters (FW-005) ---- */
hal_status_t hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
hal_status_t hal_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len);
hal_status_t hal_flash_erase(uint32_t addr);

/* ---- watchdog (SAF-004) ---- */
hal_status_t hal_wdg_init(uint32_t timeout_ms);
void         hal_wdg_feed(void);

/* ---- sim-side hooks: only hal/sim implements/uses these (fault injection, SIM-001) ---- */
void hal_sim_step(uint64_t dt_us);   /* advance virtual world */
void hal_sim_fault(const char *name, bool active);
/* Schedule a fault to activate at an absolute sim time (us). Scenario presets
 * use this so injection happens in flight, after arm + takeoff. */
void hal_sim_schedule_fault(const char *name, uint64_t at_us);

/* sim-only test hooks (SIM backend; declared unconditionally for link compat,
 * no-op/absent on STM32). Used by closed-loop tests. */
void hal_sim_reset(void);
void hal_sim_set_attitude(float w, float x, float y, float z);
void hal_sim_get_attitude(float q[4]);
void hal_sim_get_vertical(float *z_m, float *vz_ms);
void hal_sim_set_vertical(float z_m, float vz_ms);
void hal_sim_get_horizontal(float *x_m, float *y_m);            /* Phase 21 */
void hal_sim_get_horizontal_vel(float *vx_ms, float *vy_ms);
void hal_sim_set_horizontal(float x_m, float y_m, float vx_ms, float vy_ms);
void hal_sim_set_wind(float wx_ms, float wy_ms);   /* disturbance model */
float   hal_sim_obstacle_range_m(void);           /* sim truth: range to the
                                                    * virtual obstacle ahead */
void    hal_sim_set_obstacle_active(bool active);  /* freeze/advance obstacle */
void hal_sim_set_seed(uint32_t seed);
void hal_sim_set_noise(bool enabled);
void hal_sim_scenario(const char *name);

#endif /* HAL_INTERFACES_H */
