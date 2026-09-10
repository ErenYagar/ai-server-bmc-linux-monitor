#include "monitor.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const thresholds_t t = {80, 80, 95, 1000, 11.4};
    const sensor_snapshot_t normal = {55, 65, 3200, 12.1};
    sensor_snapshot_t s = normal;
    assert(monitor_evaluate(&s, &t) == HEALTH_OK);
    s.cpu_temp_c = 80;
    assert(monitor_evaluate(&s, &t) == HEALTH_WARNING);
    s = normal; s.gpu_temp_c = 80;
    assert(monitor_evaluate(&s, &t) == HEALTH_WARNING);
    s.gpu_temp_c = 94.999;
    assert(monitor_evaluate(&s, &t) == HEALTH_WARNING);
    s.gpu_temp_c = 95;
    assert(monitor_evaluate(&s, &t) == HEALTH_CRITICAL);
    s = normal; s.fan_rpm = 0;
    assert(monitor_evaluate(&s, &t) == HEALTH_CRITICAL);
    s.fan_rpm = 1000;
    assert(monitor_evaluate(&s, &t) == HEALTH_OK);
    s.fan_rpm = 999;
    assert(monitor_evaluate(&s, &t) == HEALTH_CRITICAL);
    s = normal; s.psu_voltage_v = 11.399;
    assert(monitor_evaluate(&s, &t) == HEALTH_CRITICAL);
    s.psu_voltage_v = 11.4;
    assert(monitor_evaluate(&s, &t) == HEALTH_OK);
    s = normal; s.cpu_temp_c = 80; s.fan_rpm = 0;
    assert(monitor_evaluate(&s, &t) == HEALTH_CRITICAL);
    assert(monitor_evaluate(NULL, &t) == HEALTH_SENSOR_ERROR);
    assert(monitor_evaluate(&normal, NULL) == HEALTH_SENSOR_ERROR);
    s = normal; s.cpu_temp_c = NAN;
    assert(monitor_evaluate(&s, &t) == HEALTH_SENSOR_ERROR);
    s = normal; s.psu_voltage_v = INFINITY;
    assert(monitor_evaluate(&s, &t) == HEALTH_SENSOR_ERROR);
    thresholds_t invalid = t; invalid.gpu_warn_c = 96;
    assert(monitor_evaluate(&normal, &invalid) == HEALTH_SENSOR_ERROR);
    assert(strcmp(health_to_string(HEALTH_OK), "OK") == 0);
    assert(strcmp(health_to_string(HEALTH_WARNING), "WARNING") == 0);
    assert(strcmp(health_to_string(HEALTH_CRITICAL), "CRITICAL") == 0);
    assert(strcmp(health_to_string(HEALTH_SENSOR_ERROR), "SENSOR_ERROR") == 0);
    assert(strcmp(health_to_string((health_t)99), "SENSOR_ERROR") == 0);
    puts("test_monitor: PASS");
    return 0;
}
