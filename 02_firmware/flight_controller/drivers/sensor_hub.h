#ifndef SENSOR_HUB_H
#define SENSOR_HUB_H
#include "fc_types.h"
#include "sensor_health.h"
#include "validation.h"

/* Sensor hub (SEN-001): driver->calibration->validation->filtering->timestamp->
 * health for every sensor. Rate/staleness limits per CLOCK_AND_TIMING_BUDGET.md. */

void        sensor_hub_init(void);
void        sensor_hub_poll(void);          /* low-rate (50 Hz) sensors */
bool        sensor_hub_imu(fc_imu_sample_t *out);       /* returns valid sample */
const fc_battery_sample_t* sensor_hub_battery(void);
const fc_baro_sample_t*    sensor_hub_baro(void);

extern const sensor_health_t *hub_health_imu(void);
extern const sensor_health_t *hub_health_baro(void);
extern const sensor_health_t *hub_health_battery(void);

#endif
