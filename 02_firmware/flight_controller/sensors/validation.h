#ifndef VALIDATION_H
#define VALIDATION_H
#include "fc_types.h"
#include <stdbool.h>

/* Sample validation (SEN-002): range, NaN, jump and stale rejection. */

typedef struct {
    float    range_min, range_max;   /* plausible magnitude envelope */
    float    max_jump;               /* max allowed step per sample */
    uint64_t stale_us;
} validation_cfg_t;

typedef struct {
    validation_cfg_t cfg;
    float    last_value;
    bool     have_last;
} validator_t;

void validator_init(validator_t *v, const validation_cfg_t *cfg);
/* returns true when the sample passes all checks */
bool validator_check(validator_t *v, uint64_t now_us, float value);

#endif
