#include "motor_output.h"
#include "esc_dshot.h"
#include "hal_interfaces.h"
#include <string.h>

/* Command timeout: if the control loop stops calling update() the ESC must not
 * keep running on a stale command (SAF-003). 50 ms >> 1 ms loop period. */
#define MOTOR_CMD_TIMEOUT_US 50000u

typedef struct {
    bool armed;
    bool timed_out;
    uint16_t frames[4];
    uint64_t last_update_us;
    bool have_update;
} motor_out_t;

static motor_out_t m;

void motor_output_init(void)
{
    memset(&m, 0, sizeof(m));
    m.timed_out = true;   /* fail-safe default: stopped until first update */
}

void motor_output_set_armed(bool armed)
{
    m.armed = armed;
    if (!armed) {
        /* immediate stop frames on disarm (CTRL-005) */
        for (int i = 0; i < 4; i++) m.frames[i] = dshot_build_frame(DSHOT_CMD_MOTOR_STOP, false);
    }
}

bool motor_output_is_armed(void) { return m.armed; }

fc_status_t motor_output_update(const float motor[4], uint64_t now_us)
{
    if (!motor) return FC_INVALID;

    m.have_update = true;
    m.last_update_us = now_us;

    /* stale-command detection happens in motor_output_poll() */

    if (!m.armed) {
        m.timed_out = false;
        for (int i = 0; i < 4; i++) m.frames[i] = dshot_build_frame(DSHOT_CMD_MOTOR_STOP, false);
        return FC_OK;
    }

    m.timed_out = false;
    for (int i = 0; i < 4; i++) {
        m.frames[i] = dshot_build_frame(dshot_throttle_from_unit(motor[i]), false);
    }
    return FC_OK;
}

bool motor_output_apply(float motor[4], uint64_t now_us)
{
    if (!motor) return false;

    /* A call to apply() IS a fresh command by definition; the stale-command
     * watchdog is poll() (called from the failsafe task). This also gives a
     * recovery path after a watchdog stop (fresh commands resume). */
    m.have_update = true;
    m.last_update_us = now_us;
    m.timed_out = (bool)false;

    if (!m.armed) {
        for (int i = 0; i < 4; i++) {
            motor[i] = 0.0f;
            m.frames[i] = dshot_build_frame(DSHOT_CMD_MOTOR_STOP, false);
        }
        return false;
    }

    for (int i = 0; i < 4; i++) {
        m.frames[i] = dshot_build_frame(dshot_throttle_from_unit(motor[i]), false);
    }
    return true;
}

/* Call at a known rate (e.g. failsafe monitor, 100 Hz). */
void motor_output_poll(uint64_t now_us)
{
    if (!m.have_update) { m.timed_out = true; return; }
    if ((now_us - m.last_update_us) > MOTOR_CMD_TIMEOUT_US) {
        m.timed_out = true;
        for (int i = 0; i < 4; i++) m.frames[i] = dshot_build_frame(DSHOT_CMD_MOTOR_STOP, false);
    }
}

void motor_output_frames(uint16_t frames[4])
{
    if (!frames) return;
    memcpy(frames, m.frames, sizeof(m.frames));
}

bool motor_output_timed_out(void) { return m.timed_out; }
