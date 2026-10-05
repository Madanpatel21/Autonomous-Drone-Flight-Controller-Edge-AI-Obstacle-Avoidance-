#ifndef MIXER_QUADX_H
#define MIXER_QUADX_H

/* Quad-X mixer, prop-out order M1..M4 (CTRL-003). thrust 0..1, moments -1..1. */
void mixer_quadx_run(float thrust, float roll, float pitch, float yaw, float out[4]);

#endif
