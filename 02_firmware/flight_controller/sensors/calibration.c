#include "calibration.h"
#include "crc16.h"
#include "hal_interfaces.h"
#include <math.h>
#include <string.h>

#define CAL_FLASH_ADDR 0x00000400u
#define CAL_GRAVITY 9.80665f

typedef struct {
    cal_gyro_t  g;
    cal_accel_t a;
    uint16_t crc;
} cal_blob_t;

static cal_blob_t c;

void cal_init(void)
{
    memset(&c, 0, sizeof(c));
    uint8_t buf[sizeof(cal_blob_t)];
    if (hal_flash_read(CAL_FLASH_ADDR, buf, sizeof(buf)) == HAL_OK) {
        cal_blob_t tmp;
        memcpy(&tmp, buf, sizeof(tmp));
        if (tmp.crc == crc16_ccitt((const uint8_t *)&tmp, sizeof(tmp) - sizeof(uint16_t))) {
            c = tmp;
        }
    }
}

const cal_gyro_t*  cal_gyro(void)  { return &c.g; }
const cal_accel_t* cal_accel(void) { return &c.a; }

bool cal_gyro_compute(const fc_imu_sample_t *samples, uint32_t n)
{
    if (!samples || n < 100u) return false;
    double sum[3] = {0,0,0};
    for (uint32_t i = 0; i < n; i++) {
        if (!samples[i].valid) return false;
        sum[0] += samples[i].gyro_rad_s.x;
        sum[1] += samples[i].gyro_rad_s.y;
        sum[2] += samples[i].gyro_rad_s.z;
    }
    for (int k = 0; k < 3; k++) c.g.bias[k] = (float)(sum[k] / n);
    c.g.valid = true;
    return true;
}

void cal_gyro_apply(fc_imu_sample_t *s)
{
    if (!s || !s->valid || !c.g.valid) return;
    s->gyro_rad_s.x -= c.g.bias[0];
    s->gyro_rad_s.y -= c.g.bias[1];
    s->gyro_rad_s.z -= c.g.bias[2];
}

bool cal_accel_compute(const float minmax[3][2])
{
    /* standard 6-face: offset=(max+min)/2, scale=g/((max-min)/2) */
    for (int k = 0; k < 3; k++) {
        float mx = minmax[k][1], mn = minmax[k][0];
        if (mx <= mn) return false;
        c.a.offset[k] = (mx + mn) * 0.5f;
        float half = (mx - mn) * 0.5f;
        if (half < 0.5f * CAL_GRAVITY) return false;  /* implausible span */
        c.a.scale[k] = CAL_GRAVITY / half;
    }
    c.a.valid = true;
    return true;
}

void cal_accel_apply(fc_imu_sample_t *s)
{
    if (!s || !s->valid || !c.a.valid) return;
    float v[3] = { s->accel_m_s2.x, s->accel_m_s2.y, s->accel_m_s2.z };
    for (int k = 0; k < 3; k++) {
        v[k] = (v[k] - c.a.offset[k]) * c.a.scale[k];
    }
    s->accel_m_s2.x = v[0]; s->accel_m_s2.y = v[1]; s->accel_m_s2.z = v[2];
}

fc_status_t cal_store(void)
{
    if (!c.g.valid || !c.a.valid) return FC_INVALID;
    c.crc = crc16_ccitt((const uint8_t *)&c, sizeof(c) - sizeof(uint16_t));
    uint8_t buf[sizeof(cal_blob_t)];
    memcpy(buf, &c, sizeof(buf));
    if (hal_flash_erase(CAL_FLASH_ADDR) != HAL_OK) return FC_ERROR;
    if (hal_flash_write(CAL_FLASH_ADDR, buf, sizeof(buf)) != HAL_OK) return FC_ERROR;
    return FC_OK;
}
