#ifndef PARAMETERS_H
#define PARAMETERS_H
#include "fc_types.h"
#include <stddef.h>

/* Persistent parameter store (FW-005): CRC-protected, safe defaults on corruption.
 * Storage layout: [params blob][crc16 u16]. Applied at loop boundaries (CTRL-006). */

#define PARAM_MAGIC 0x50415231u  /* "PAR1" */

typedef struct {
    uint32_t magic;
    uint16_t version;
    float rate_roll_kp, rate_roll_ki, rate_roll_kd;
    float rate_pitch_kp, rate_pitch_ki, rate_pitch_kd;
    float rate_yaw_kp, rate_yaw_ki, rate_yaw_kd;
    float batt_warn_v, batt_rtl_v, batt_land_v;
    uint32_t reserved[8];
} params_t;

void         params_defaults(params_t *p);
fc_status_t  params_load(params_t *p);              /* from hal_flash; defaults on CRC fail */
fc_status_t  params_store(const params_t *p);       /* erase+write+CRC */
uint16_t     params_crc(const params_t *p);

#endif
