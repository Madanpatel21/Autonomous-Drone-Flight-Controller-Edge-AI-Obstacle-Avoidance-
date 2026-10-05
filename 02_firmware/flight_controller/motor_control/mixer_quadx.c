#include "mixer_quadx.h"
#include <math.h>

/* Quad-X, FLU body frame (DEC-008). Order M1..M4 = FR, FL, RL, RR;
 * spin directions CW, CCW, CW, CCW -> torque map diag(4,4,4).
 */

void mixer_quadx_run(float thrust, float roll, float pitch, float yaw, float out[4])
{
    /* clamp inputs */
    if (thrust < 0.0f) thrust = 0.0f;
    if (thrust > 1.0f) thrust = 1.0f;
    if (roll  < -1.0f) roll  = -1.0f; if (roll  > 1.0f) roll  = 1.0f;
    if (pitch < -1.0f) pitch = -1.0f; if (pitch > 1.0f) pitch = 1.0f;
    if (yaw   < -1.0f) yaw   = -1.0f; if (yaw   > 1.0f) yaw   = 1.0f;

    /* FLU body frame (x fwd, y left, z up); M1 FR, M2 FL, M3 RL, M4 RR;
     * spins M1/M3 CW, M2/M4 CCW -> torque map diag(4,4,4) (DEC-008, sim-verified) */
    out[0] = thrust - roll - pitch - yaw;
    out[1] = thrust + roll - pitch + yaw;
    out[2] = thrust + roll + pitch - yaw;
    out[3] = thrust - roll + pitch + yaw;

    /* Saturation minimization (CTRL-003): priority collective > roll/pitch > yaw.
     * Step 1: shed excess via yaw authority (toward 0). Step 2: scale roll/pitch. */
    for (int pass = 0; pass < 2; pass++) {
        float excess = 0.0f;
        for (int i = 0; i < 4; i++) if (out[i] > excess) excess = out[i];
        if (excess <= 1.0f) break;
        if (pass == 0 && yaw != 0.0f) {
            /* how much can yaw absorption reduce the max motor */
            float step = excess - 1.0f;
            float yaw_step = (yaw > 0.0f) ? step : -step;
            float new_yaw = yaw - yaw_step;
            if ((yaw > 0.0f && new_yaw < 0.0f) || (yaw < 0.0f && new_yaw > 0.0f)) new_yaw = 0.0f;
            yaw = new_yaw;
        } else {
            /* scale roll+pitch together so max motor hits exactly 1.0 */
            float scale = 1.0f;
            float rp[4];
            for (int trial = 0; trial < 8; trial++) {
                rp[0] = thrust - roll*scale - pitch*scale - yaw;
                rp[1] = thrust + roll*scale - pitch*scale + yaw;
                rp[2] = thrust + roll*scale + pitch*scale - yaw;
                rp[3] = thrust - roll*scale + pitch*scale + yaw;
                float mx = rp[0];
                for (int i = 1; i < 4; i++) if (rp[i] > mx) mx = rp[i];
                if (mx <= 1.0f) { scale *= 0.5f; break; }  /* overshot */
                if (mx - 1.0f < 0.02f) break;
                scale *= 0.8f;
            }
            roll *= scale; pitch *= scale;
        }
        out[0] = thrust - roll - pitch - yaw;
        out[1] = thrust + roll - pitch + yaw;
        out[2] = thrust + roll + pitch - yaw;
        out[3] = thrust - roll + pitch + yaw;
    }

    for (int i = 0; i < 4; i++) {
        if (out[i] < 0.0f) out[i] = 0.0f;
        if (out[i] > 1.0f) out[i] = 1.0f;
    }
}
