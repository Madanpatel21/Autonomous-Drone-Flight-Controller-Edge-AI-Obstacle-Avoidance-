#include "parameters.h"
#include "crc16.h"
#include "hal_interfaces.h"
#include <string.h>

#define PARAM_FLASH_ADDR 0x00000000u  /* backend-relative offset */

uint16_t params_crc(const params_t *p)
{
    /* CRC over everything except trailing padding determinism: whole struct */
    return crc16_ccitt((const uint8_t *)p, sizeof(*p));
}

void params_defaults(params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->magic = PARAM_MAGIC;
    p->version = 1;
    /* UNVERIFIED placeholder gains — Phase 12 sweep (same values as ctrl_cascade.c) */
    p->rate_roll_kp  = 0.135f; p->rate_roll_ki  = 0.05f; p->rate_roll_kd  = 0.003f;
    p->rate_pitch_kp = 0.135f; p->rate_pitch_ki = 0.05f; p->rate_pitch_kd = 0.003f;
    p->rate_yaw_kp   = 0.20f;  p->rate_yaw_ki   = 0.03f; p->rate_yaw_kd   = 0.0f;
    p->batt_warn_v = 3.5f; p->batt_rtl_v = 3.4f; p->batt_land_v = 3.1f; /* SAF-030 */
}

fc_status_t params_load(params_t *p)
{
    if (!p) return FC_INVALID;
    uint8_t buf[sizeof(params_t) + sizeof(uint16_t)];
    if (hal_flash_read(PARAM_FLASH_ADDR, buf, sizeof(buf)) != HAL_OK) {
        params_defaults(p);
        return FC_ERROR;
    }
    params_t tmp;
    memcpy(&tmp, buf, sizeof(tmp));
    uint16_t stored;
    memcpy(&stored, buf + sizeof(tmp), sizeof(stored));

    if (tmp.magic != PARAM_MAGIC || stored != params_crc(&tmp)) {
        params_defaults(p);        /* FW-005: safe defaults on corruption */
        return FC_INVALID;
    }
    *p = tmp;
    return FC_OK;
}

fc_status_t params_store(const params_t *p)
{
    if (!p || p->magic != PARAM_MAGIC) return FC_INVALID;
    uint8_t buf[sizeof(params_t) + sizeof(uint16_t)];
    memcpy(buf, p, sizeof(*p));
    uint16_t crc = params_crc(p);
    memcpy(buf + sizeof(*p), &crc, sizeof(crc));

    if (hal_flash_erase(PARAM_FLASH_ADDR) != HAL_OK) return FC_ERROR;
    if (hal_flash_write(PARAM_FLASH_ADDR, buf, sizeof(buf)) != HAL_OK) return FC_ERROR;
    return FC_OK;
}
