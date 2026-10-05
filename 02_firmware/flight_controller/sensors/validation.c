#include "validation.h"
#include <math.h>
#include <string.h>

void validator_init(validator_t *v, const validation_cfg_t *cfg)
{
    memset(v, 0, sizeof(*v));
    v->cfg = *cfg;
}

bool validator_check(validator_t *v, uint64_t now_us, float value)
{
    if (!v) return false;
    if (isnan(value) || isinf(value)) return false;              /* SEN-002 NaN */

    if (v->cfg.range_max > v->cfg.range_min &&
        (value < v->cfg.range_min || value > v->cfg.range_max)) {
        return false;                                             /* range violation */
    }

    if (v->have_last && fabsf(value - v->last_value) > v->cfg.max_jump) {
        return false;                                             /* impossible jump */
    }

    (void)now_us;  /* staleness enforced by sensor_health (SEN-004) */
    v->last_value = value;
    v->have_last = true;
    return true;
}
