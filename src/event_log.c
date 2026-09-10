#define _POSIX_C_SOURCE 200809L
#include "event_log.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int event_log(const char *severity, const char *event,
              double value, const char *action)
{
    const char *tokens[] = {severity, event, action};
    for (size_t i = 0; i < 3; ++i) {
        if (!tokens[i] || !*tokens[i] ||
            strspn(tokens[i], "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
            strlen(tokens[i])) {
            errno = EINVAL;
            return -1;
        }
    }
    if (!isfinite(value)) {
        errno = EINVAL;
        return -1;
    }
    time_t now = time(NULL);
    struct tm utc;
    char stamp[32];
    if (now == (time_t)-1 || !gmtime_r(&now, &utc) ||
        !strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &utc))
        return -1;
    FILE *fp = fopen("runtime/events.log", "a");
    if (!fp)
        return -1;
    int failed = fprintf(fp, "%s severity=%s event=%s value=%.2f action=%s\n",
                         stamp, severity, event, value, action) < 0;
    if (fclose(fp) != 0)
        failed = 1;
    return failed ? -1 : 0;
}
