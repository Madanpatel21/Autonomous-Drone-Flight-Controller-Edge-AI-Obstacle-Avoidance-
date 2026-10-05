#include "sensor_health.h"
#include "hal_interfaces.h"
#include <string.h>

void sensor_health_init(sensor_health_t *h, const sensor_health_cfg_t *cfg)
{
    memset(h, 0, sizeof(*h));
    h->cfg = *cfg;
    h->last_good_us = 0;
    h->born_us = hal_time_us();
}

bool sensor_health_check(sensor_health_t *h, uint64_t now_us, float value, bool sample_valid)
{
    if (!h) return false;

    if (!sample_valid) {
        h->total_errors++;
        return false;
    }

    if (h->cfg.range_max > h->cfg.range_min &&
        (value < h->cfg.range_min || value > h->cfg.range_max)) {
        h->range_errors++;
        h->total_errors++;
        if (h->range_errors >= h->cfg.max_range_errors) h->unhealthy = true;
        return false;   /* SEN-002: reject out-of-range */
    }

    /* good sample */
    h->range_errors = 0;
    h->last_good_us = now_us;
    if (!h->unhealthy) return true;

    /* recovery: require fresh good data; unhealthy stays latched until
     * an explicit sensor_health_recover() from the subsystem layer */
    return true;
}

void sensor_health_poll(sensor_health_t *h, uint64_t now_us, bool new_data_arrived)
{
    if (!h) return;
    if (new_data_arrived) return;   /* check() handles freshness */

    uint64_t since = (h->last_good_us != 0) ? (now_us - h->last_good_us)
                                            : (now_us - h->born_us);
    if (since > h->cfg.timeout_us) {
        h->timeouts++;
        h->total_errors++;
        h->unhealthy = true;        /* SEN-004: stale or never-delivered -> unhealthy */
    }
}
