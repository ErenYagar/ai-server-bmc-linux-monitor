#ifndef MONITOR_H
#define MONITOR_H

typedef enum {
    HEALTH_OK,
    HEALTH_WARNING,
    HEALTH_CRITICAL,
    HEALTH_SENSOR_ERROR
} health_t;

typedef struct {
    double cpu_temp_c;
    double gpu_temp_c;
    long fan_rpm;
    double psu_voltage_v;
} sensor_snapshot_t;

typedef struct {
    double cpu_warn_c;
    double gpu_warn_c;
    double gpu_crit_c;
    long fan_min_rpm;
    double psu_min_v;
} thresholds_t;

health_t monitor_evaluate(const sensor_snapshot_t *sensors,
                          const thresholds_t *limits);
const char *health_to_string(health_t health);
int thresholds_load(const char *path, thresholds_t *limits);

#endif
