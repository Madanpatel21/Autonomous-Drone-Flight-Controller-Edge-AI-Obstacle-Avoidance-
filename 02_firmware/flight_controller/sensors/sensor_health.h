#ifndef SENSOR_HEALTH_H
#define SENSOR_HEALTH_H
#include "fc_types.h"

/* Sensor health pipeline stage (SEN-004): timeout/range/error counters,
 * unhealthy flag escalation to failsafe. One instance per sensor. */

typedef struct {
    uint64_t timeout_us;      /* staleness limit (CLOCK_AND_TIMING_BUDGET.md) */
    float    range_min;       /* -1 disables */
    float    range_max;
    uint32_t max_range_errors;/* consecutive before unhealthy */
} sensor_health_cfg_t;

typedef struct {
    sensor_health_cfg_t cfg;
    uint64_t last_good_us;
    uint64_t born_us;         /* init time: a sensor that never delivers is unhealthy */
    uint32_t range_errors;
    uint32_t timeouts;
    uint32_t total_errors;
    bool unhealthy;
} sensor_health_t;

void sensor_health_init(sensor_health_t *h, const sensor_health_cfg_t *cfg);
/* returns true if sample accepted (also updates health state) */
bool sensor_health_check(sensor_health_t *h, uint64_t now_us, float value, bool sample_valid);
/* periodic timeout poll for sensors not read every tick */
void sensor_health_poll(sensor_health_t *h, uint64_t now_us, bool new_data_arrived);

#endif
