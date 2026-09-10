#define _POSIX_C_SOURCE 200809L
#include "state.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int state_write(health_t health, const sensor_snapshot_t *s, const char *action)
{
    if (!action || !*action ||
        strspn(action, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(action) ||
        (s && (!isfinite(s->cpu_temp_c) || !isfinite(s->gpu_temp_c) ||
               !isfinite(s->psu_voltage_v)))) {
        errno = EINVAL;
        return -1;
    }
    time_t now = time(NULL);
    struct tm utc;
    char stamp[32];
    if (now == (time_t)-1 || !gmtime_r(&now, &utc) ||
        !strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &utc))
        return -1;
    FILE *fp = fopen("runtime/state.json.tmp", "w");
    if (!fp)
        return -1;
    int failed = fprintf(fp, "{\n  \"health\": \"%s\",\n  \"sensors_valid\": %s,\n",
                         health_to_string(health), s ? "true" : "false") < 0;
    if (s) {
        if (fprintf(fp, "  \"cpu_temp_c\": %.17g,\n  \"gpu_temp_c\": %.17g,\n"
                        "  \"fan_rpm\": %ld,\n  \"psu_voltage_v\": %.17g,\n",
                    s->cpu_temp_c, s->gpu_temp_c, s->fan_rpm, s->psu_voltage_v) < 0)
            failed = 1;
    } else if (fprintf(fp, "  \"cpu_temp_c\": null,\n  \"gpu_temp_c\": null,\n"
                          "  \"fan_rpm\": null,\n  \"psu_voltage_v\": null,\n") < 0) {
        failed = 1;
    }
    if (fprintf(fp, "  \"recovery_action\": \"%s\",\n  \"timestamp\": \"%s\"\n}\n",
                action, stamp) < 0)
        failed = 1;
    if (fclose(fp) != 0)
        failed = 1;
    if (!failed && rename("runtime/state.json.tmp", "runtime/state.json") == 0)
        return 0;
    int saved = errno;
    if (unlink("runtime/state.json.tmp") != 0 && errno != ENOENT)
        perror("remove failed state temporary file");
    errno = saved;
    return -1;
}
