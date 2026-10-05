#include "hal_interfaces.h"
#include "sim_model.h"
#include <string.h>

/* SIM backend (DEC-001). Thin HAL adapter over the deterministic virtual world
 * implemented in sim_model.c (Phase 22 extraction): this file owns HAL
 * semantics (non-blocking contract, RAM flash, no-op watchdog) and delegates
 * every world operation to the shared model.
 *
 * The SAME model file backs the HIL rig process, so a SIM run and a HIL run
 * integrate identical physics and identical sensor error models - a single
 * source of truth for the world instead of two drifting copies. */

/* ---- sim-side hooks (HAL contract) ---- */
void     hal_sim_step(uint64_t dt_us)                 { sim_model_step(dt_us); }
void     hal_sim_fault(const char *n, bool active)    { sim_model_fault(n, active); }
void     hal_sim_schedule_fault(const char *n, uint64_t at_us)
                                                     { sim_model_schedule_fault(n, at_us); }
void     hal_sim_scenario(const char *name)           { sim_model_scenario(name); }
void     hal_sim_reset(void)                          { sim_model_reset(); }
void     hal_sim_set_seed(uint32_t seed)              { sim_model_set_seed(seed); }
void     hal_sim_set_noise(bool enabled)              { sim_model_set_noise(enabled); }
void     hal_sim_set_attitude(float w, float x, float y, float z)
                                                     { sim_model_set_attitude(w, x, y, z); }
void     hal_sim_get_attitude(float q[4])             { sim_model_get_attitude(q); }
void     hal_sim_get_vertical(float *z, float *vz)    { sim_model_get_vertical(z, vz); }
void     hal_sim_set_vertical(float z, float vz)      { sim_model_set_vertical(z, vz); }
void     hal_sim_get_horizontal(float *x, float *y)   { sim_model_get_horizontal(x, y); }
void     hal_sim_get_horizontal_vel(float *vx, float *vy)
                                                     { sim_model_get_horizontal_vel(vx, vy); }
void     hal_sim_set_horizontal(float x, float y, float vx, float vy)
                                                     { sim_model_set_horizontal(x, y, vx, vy); }
void     hal_sim_set_wind(float wx, float wy)         { sim_model_set_wind(wx, wy); }
float    hal_sim_obstacle_range_m(void)               { return sim_model_obstacle_range_m(); }
void     hal_sim_set_obstacle_active(bool active)     { sim_model_set_obstacle_active(active); }

uint64_t hal_time_us(void) { return sim_model_time_us(); }

void hal_sleep_until_us(uint64_t deadline_us)
{
    /* deterministic virtual clock: simply advance (SIM-002) */
    sim_model_advance_to(deadline_us);
}

hal_status_t hal_imu_read(fc_imu_sample_t *out)        { return sim_model_read_imu(out); }
hal_status_t hal_baro_read(fc_baro_sample_t *out)      { return sim_model_read_baro(out); }
hal_status_t hal_battery_read(fc_battery_sample_t *out){ return sim_model_read_battery(out); }
hal_status_t hal_rc_read(fc_rc_frame_t *out)           { return sim_model_read_rc(out); }
hal_status_t hal_gnss_read(fc_gnss_sample_t *out)      { return sim_model_read_gnss(out); }
hal_status_t hal_tof_read(int id, fc_tof_sample_t *out){ return sim_model_read_tof(id, out); }

hal_status_t hal_companion_ai_read(fc_ai_obstacle_set_t *out)
{
    return sim_model_read_ai(out);
}

size_t hal_companion_uart_read(uint8_t *out, size_t cap, size_t *n_out)
{
    return sim_model_companion_uart(out, cap, n_out);
}

hal_status_t hal_flow_read(fc_flow_sample_t *out)       { (void)out; return HAL_NOT_READY; }

hal_status_t hal_actuator_write(const float motor[4])
{
    if (!motor) return HAL_ERROR;
    sim_model_set_motor(motor);
    return HAL_OK;
}

/* --- flash: RAM-backed model (param store works in SIM; file backing later) --- */
#define SIM_FLASH_SIZE 4096u
static uint8_t sim_flash[SIM_FLASH_SIZE];

hal_status_t hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!buf || addr + len > SIM_FLASH_SIZE) return HAL_ERROR;
    memcpy(buf, sim_flash + addr, len);
    return HAL_OK;
}

hal_status_t hal_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (!buf || addr + len > SIM_FLASH_SIZE) return HAL_ERROR;
    memcpy(sim_flash + addr, buf, len);
    return HAL_OK;
}

hal_status_t hal_flash_erase(uint32_t addr)
{
    if (addr >= SIM_FLASH_SIZE) return HAL_ERROR;
    memset(sim_flash, 0xFF, SIM_FLASH_SIZE);
    return HAL_OK;
}

hal_status_t hal_wdg_init(uint32_t timeout_ms) { (void)timeout_ms; return HAL_OK; }
void hal_wdg_feed(void) {}