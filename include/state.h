#ifndef STATE_H
#define STATE_H
#include "monitor.h"

/* NULL sensors publishes null readings, never stale last-good values. */
int state_write(health_t health, const sensor_snapshot_t *sensors,
                const char *recovery_action);
#endif
