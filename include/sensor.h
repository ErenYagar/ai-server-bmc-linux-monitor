#ifndef SENSOR_H
#define SENSOR_H
#include "monitor.h"

/* On failure, leave snapshot unchanged and return nonzero. */
int sensor_read_all(sensor_snapshot_t *snapshot);
#endif
