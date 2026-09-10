#include "monitor.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int limits_valid(const thresholds_t *t)
{
    return t && isfinite(t->cpu_warn_c) && t->cpu_warn_c >= -40 &&
           t->cpu_warn_c <= 150 && isfinite(t->gpu_warn_c) &&
           t->gpu_warn_c >= -40 && isfinite(t->gpu_crit_c) &&
           t->gpu_warn_c < t->gpu_crit_c && t->gpu_crit_c <= 150 &&
           t->fan_min_rpm > 0 && t->fan_min_rpm <= 100000 &&
           isfinite(t->psu_min_v) && t->psu_min_v > 0 && t->psu_min_v <= 20;
}

health_t monitor_evaluate(const sensor_snapshot_t *s, const thresholds_t *t)
{
    if (!s || !limits_valid(t) ||
        !isfinite(s->cpu_temp_c) || s->cpu_temp_c < -40 || s->cpu_temp_c > 150 ||
        !isfinite(s->gpu_temp_c) || s->gpu_temp_c < -40 || s->gpu_temp_c > 150 ||
        s->fan_rpm < 0 || s->fan_rpm > 100000 ||
        !isfinite(s->psu_voltage_v) || s->psu_voltage_v < 0 ||
        s->psu_voltage_v > 20)
        return HEALTH_SENSOR_ERROR;
    if (s->gpu_temp_c >= t->gpu_crit_c || s->fan_rpm < t->fan_min_rpm ||
        s->psu_voltage_v < t->psu_min_v)
        return HEALTH_CRITICAL;
    if (s->cpu_temp_c >= t->cpu_warn_c || s->gpu_temp_c >= t->gpu_warn_c)
        return HEALTH_WARNING;
    return HEALTH_OK;
}

const char *health_to_string(health_t health)
{
    switch (health) {
    case HEALTH_OK: return "OK";
    case HEALTH_WARNING: return "WARNING";
    case HEALTH_CRITICAL: return "CRITICAL";
    case HEALTH_SENSOR_ERROR: return "SENSOR_ERROR";
    }
    return "SENSOR_ERROR";
}

int thresholds_load(const char *path, thresholds_t *limits)
{
    static const char *const keys[] = {
        "cpu_warn_c", "gpu_warn_c", "gpu_crit_c", "fan_min_rpm", "psu_min_v"
    };
    if (!path || !limits) {
        errno = EINVAL;
        return -1;
    }
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;
    thresholds_t parsed = {0};
    unsigned seen = 0;
    char line[256];
    int failed = 0;
    while (fgets(line, sizeof line, fp)) {
        if (!strchr(line, '\n') && !feof(fp)) {
            failed = 1;
            break;
        }
        char *comment = strchr(line, '#');
        if (comment)
            *comment = '\0';
        if (line[strspn(line, " \t\r\n")] == '\0')
            continue;
        char key[64], value[128], extra;
        if (sscanf(line, " %63[^= \t] = %127s %c", key, value, &extra) != 2) {
            failed = 1;
            break;
        }
        unsigned i;
        for (i = 0; i < 5 && strcmp(key, keys[i]) != 0; ++i) {}
        if (i == 5 || (seen & (1u << i))) {
            failed = 1;
            break;
        }
        errno = 0;
        char *end;
        if (i == 3) {
            parsed.fan_min_rpm = strtol(value, &end, 10);
        } else {
            double number = strtod(value, &end);
            if (!isfinite(number))
                failed = 1;
            switch (i) {
            case 0: parsed.cpu_warn_c = number; break;
            case 1: parsed.gpu_warn_c = number; break;
            case 2: parsed.gpu_crit_c = number; break;
            case 4: parsed.psu_min_v = number; break;
            }
        }
        if (errno || end == value || *end != '\0' || failed) {
            failed = 1;
            break;
        }
        seen |= 1u << i;
    }
    if (ferror(fp))
        failed = 1;
    if (fclose(fp) != 0)
        failed = 1;
    if (failed || seen != 31 || !limits_valid(&parsed)) {
        errno = EINVAL;
        return -1;
    }
    *limits = parsed;
    return 0;
}
