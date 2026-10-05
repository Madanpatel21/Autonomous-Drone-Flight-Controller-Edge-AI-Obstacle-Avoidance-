#include "sensor_hub.h"
#include "calibration.h"
#include "hal_interfaces.h"
#include <math.h>
#include <string.h>

static sensor_health_t h_imu, h_baro, h_batt;
static fc_imu_sample_t last_imu;
static fc_baro_sample_t last_baro;
static fc_battery_sample_t last_batt;
static bool imu_ok, baro_ok, batt_ok;

static const sensor_health_cfg_t cfg_imu = {
    .timeout_us = 3000u, .range_min = -100.0f, .range_max = 100.0f, .max_range_errors = 5 };
static const sensor_health_cfg_t cfg_baro = {
    .timeout_us = 200000u, .range_min = 30000.0f, .range_max = 120000.0f, .max_range_errors = 5 };
static const sensor_health_cfg_t cfg_batt = {
    .timeout_us = 200000u, .range_min = 9.0f, .range_max = 17.2f, .max_range_errors = 5 };

void sensor_hub_init(void)
{
    sensor_health_init(&h_imu, &cfg_imu);
    sensor_health_init(&h_baro, &cfg_baro);
    sensor_health_init(&h_batt, &cfg_batt);
    imu_ok = baro_ok = batt_ok = false;
    cal_init();
}

bool sensor_hub_imu(fc_imu_sample_t *out)
{
    fc_imu_sample_t s;
    if (hal_imu_read(&s) != HAL_OK) {
        sensor_health_poll(&h_imu, hal_time_us(), false);
        return false;
    }
    /* pipeline: calibration -> validation -> health (SEN-001) */
    cal_gyro_apply(&s);
    cal_accel_apply(&s);

    float mag = s.accel_m_s2.x*s.accel_m_s2.x + s.accel_m_s2.y*s.accel_m_s2.y +
                s.accel_m_s2.z*s.accel_m_s2.z;
    float mag_r = sqrtf(mag);
    bool ok = sensor_health_check(&h_imu, s.timestamp_us, mag_r, s.valid);

    if (ok) {
        last_imu = s;
        imu_ok = true;
        if (out) *out = s;
        return true;
    }
    return false;
}

void sensor_hub_poll(void)
{
    fc_baro_sample_t b;
    if (hal_baro_read(&b) == HAL_OK && b.valid &&
        sensor_health_check(&h_baro, b.timestamp_us, b.pressure_pa, true)) {
        last_baro = b;
        baro_ok = true;
    } else {
        sensor_health_poll(&h_baro, hal_time_us(), false);
    }

    fc_battery_sample_t bt;
    if (hal_battery_read(&bt) == HAL_OK && bt.valid &&
        sensor_health_check(&h_batt, bt.timestamp_us, bt.pack_v, true)) {
        last_batt = bt;
        batt_ok = true;
    } else {
        sensor_health_poll(&h_batt, hal_time_us(), false);
    }
}

const fc_battery_sample_t* sensor_hub_battery(void) { return batt_ok ? &last_batt : NULL; }
const fc_baro_sample_t*    sensor_hub_baro(void)    { return baro_ok ? &last_baro : NULL; }

const sensor_health_t *hub_health_imu(void)     { return &h_imu; }
const sensor_health_t *hub_health_baro(void)    { return &h_baro; }
const sensor_health_t *hub_health_battery(void) { return &h_batt; }
