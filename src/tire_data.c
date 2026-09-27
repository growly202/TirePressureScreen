#include "tire_data.h"

tire_state_t tire_pressure_state(uint16_t pressure)
{
    if (pressure < TIRE_LOW_LIMIT) return TIRE_LOW;
    if (pressure > TIRE_HIGH_LIMIT) return TIRE_HIGH;
    return TIRE_NORMAL;
}

void tire_simulation_sample(uint32_t second, tire_sample_t *sample)
{
    static const uint16_t baseline[TIRE_COUNT] = {231, 229, 234, 230};
    static const int8_t variation[] = {0, 1, 2, 1, 0, -1, -2, -1};
    uint32_t phase = second % 60;
    for (unsigned i = 0; i < TIRE_COUNT; ++i) {
        sample->pressure[i] = baseline[i] + variation[(phase + i * 2) % 8];
    }
    if (phase >= 15 && phase < 30) {
        sample->pressure[TIRE_FL] = 190 - (phase - 15) * 2;
    } else if (phase >= 30 && phase < 45) {
        sample->pressure[TIRE_RR] = 290 + (phase - 30) * 2;
    }
}
