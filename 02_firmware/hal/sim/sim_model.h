#ifndef SIM_MODEL_H
#define SIM_MODEL_H

/* Deterministic vehicle + sensor model (Phase 22 extraction).
 *
 * This module holds ALL simulation state and physics and depends on no HAL
 * entry point: it is a pure model, not a backend. Two hosts share it:
 *   - hal/sim/hal_sim.c   -> SIM target (in-process world behind the HAL)
 *   - hil_rig.c           -> HIL rig process (world on the far side of the
 *                           HIL link, exactly as a real HIL host would be)
 * Keeping ONE implementation is the Phase 15 HIL design rule: the rig must not
 * invent a second physics model, or SIM-vs-HIL differences become untraceable.
 *
 * Nothing here is identified from a real vehicle: the constants are sim-class
 * placeholders documented in SIM_TUNING_RECORD.md. */

#include "fc_types.h"
#include "hal_interfaces.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- lifecycle / clock ---- */
void     sim_model_reset(void);
uint64_t sim_model_time_us(void);
void     sim_model_advance_to(uint64_t t_us);  /* clock jump only (no physics) */
void     sim_model_set_seed(uint32_t seed);
void     sim_model_set_noise(bool enabled);

/* ---- state accessors (rig correlation + tests) ---- */
void sim_model_set_attitude(float w, float x, float y, float z);
void sim_model_get_attitude(float q[4]);
void sim_model_get_vertical(float *z_m, float *vz_ms);
void sim_model_set_vertical(float z_m, float vz_ms);
void sim_model_get_horizontal(float *x_m, float *y_m);
void sim_model_get_horizontal_vel(float *vx_ms, float *vy_ms);
void sim_model_set_horizontal(float x_m, float y_m, float vx_ms, float vy_ms);
void sim_model_set_wind(float wx_ms, float wy_ms);
float sim_model_obstacle_range_m(void);
void sim_model_set_obstacle_active(bool active);
void sim_model_set_motor(const float motor[4]);
void sim_model_get_motor(float motor[4]);
float sim_model_battery_used_mah(void);

/* ---- world advance + fault injection (same vocabulary as SIM scenarios) ---- */
void sim_model_step(uint64_t dt_us);
void sim_model_fault(const char *name, bool active);
/* Sim time at which a fault was last ACTIVATED (0 = never). The HIL rig uses
 * it to measure injection-to-observation latency in the shared time base. */
uint64_t sim_model_last_fault_us(void);
void sim_model_schedule_fault(const char *name, uint64_t at_us);
void sim_model_scenario(const char *name);

/* ---- sensor generation (identical error model to the SIM sensor classes) ---- */
hal_status_t sim_model_read_imu(fc_imu_sample_t *out);
hal_status_t sim_model_read_baro(fc_baro_sample_t *out);
hal_status_t sim_model_read_battery(fc_battery_sample_t *out);
hal_status_t sim_model_read_rc(fc_rc_frame_t *out);
hal_status_t sim_model_read_gnss(fc_gnss_sample_t *out);
hal_status_t sim_model_read_tof(int id, fc_tof_sample_t *out);
hal_status_t sim_model_read_ai(fc_ai_obstacle_set_t *out);
/* Real ICD-02 byte stream (HEARTBEAT 1 Hz / OBSTACLE_SET 25 Hz / HEALTH 1 Hz) */
size_t      sim_model_companion_uart(uint8_t *out, size_t cap, size_t *n_out);

#endif /* SIM_MODEL_H */