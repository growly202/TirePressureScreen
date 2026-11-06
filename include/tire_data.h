#ifndef TIRE_DATA_H
#define TIRE_DATA_H

#include <stdint.h>

enum { TIRE_FL, TIRE_FR, TIRE_RL, TIRE_RR, TIRE_COUNT };
/* Demo thresholds only, in hundredths of a bar. */
enum { TIRE_LOW_LIMIT = 200, TIRE_HIGH_LIMIT = 280 };
/* Demo policy: pressure in 0.01 bar, durations in milliseconds. */
enum {
    TIRE_SAMPLE_INTERVAL_MS = 300000,
    TIRE_CHART_INTERVAL_MS = 300000,
    TIRE_SIGNAL_TIMEOUT_MS = 1500000,
    TIRE_SUDDEN_CHANGE = 20,
    TIRE_CHANGE_ALERT_MS = 30000
};
typedef enum { TIRE_NORMAL, TIRE_LOW, TIRE_HIGH } tire_state_t;
typedef struct { uint16_t pressure[TIRE_COUNT]; } tire_sample_t;

tire_state_t tire_pressure_state(uint16_t pressure);
/* Deterministic 60-second cycle; no sensor or random-number dependency. */
void tire_simulation_sample(uint32_t second, tire_sample_t *sample);

#endif
