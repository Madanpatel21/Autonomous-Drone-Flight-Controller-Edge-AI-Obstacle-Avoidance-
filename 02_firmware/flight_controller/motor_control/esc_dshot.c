#include "esc_dshot.h"

uint16_t dshot_crc4(uint16_t payload12)
{
    uint16_t v = payload12 & 0x0FFFu;
    return (uint16_t)((v ^ (v >> 4) ^ (v >> 8)) & 0x0Fu);
}

uint16_t dshot_build_frame(uint16_t throttle, bool telemetry_request)
{
    uint16_t t = throttle & 0x07FFu;                 /* 11-bit throttle */
    uint16_t payload = (uint16_t)((t << 1) | (telemetry_request ? DSHOT_TELEMETRY_BIT : 0u));
    uint16_t crc = dshot_crc4(payload);
    return (uint16_t)((payload << 4) | crc);
}

uint16_t dshot_throttle_from_unit(float unit)
{
    if (unit <= 0.0f) return DSHOT_CMD_MOTOR_STOP;
    if (unit > 1.0f) unit = 1.0f;
    float span = (float)(DSHOT_MAX_THROTTLE - DSHOT_MIN_THROTTLE);
    return (uint16_t)(DSHOT_MIN_THROTTLE + (uint16_t)(unit * span + 0.5f));
}

bool dshot_decode_frame(uint16_t frame, uint16_t *throttle, bool *telemetry_request)
{
    uint16_t payload = (uint16_t)((frame >> 4) & 0x0FFFu);
    uint16_t crc     = (uint16_t)(frame & 0x000Fu);
    if (dshot_crc4(payload) != crc) return false;
    if (throttle) *throttle = (uint16_t)((payload >> 1) & 0x07FFu);
    if (telemetry_request) *telemetry_request = (payload & DSHOT_TELEMETRY_BIT) != 0u;
    return true;
}

float dshot_unit_from_throttle(uint16_t throttle)
{
    if (throttle <= DSHOT_MIN_THROTTLE) return 0.0f;
    if (throttle > DSHOT_MAX_THROTTLE) throttle = DSHOT_MAX_THROTTLE;
    float span = (float)(DSHOT_MAX_THROTTLE - DSHOT_MIN_THROTTLE);
    return ((float)throttle - (float)DSHOT_MIN_THROTTLE) / span;
}
