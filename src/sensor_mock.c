#include "sensor.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_number(const char *path, double *real, long *integer,
                       double minimum, double maximum)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror(path);
        return -1;
    }
    char text[128];
    size_t n = fread(text, 1, sizeof text - 1, fp);
    int invalid = ferror(fp) || n == sizeof text - 1 ||
                  memchr(text, '\0', n) != NULL;
    if (fclose(fp) != 0)
        invalid = 1;
    text[n] = '\0';
    errno = 0;
    char *end;
    double number;
    long count = 0;
    if (integer) {
        count = strtol(text, &end, 10);
        number = (double)count;
    } else {
        number = strtod(text, &end);
    }
    if (errno || end == text || !isfinite(number) ||
        number < minimum || number > maximum)
        invalid = 1;
    while (isspace((unsigned char)*end))
        ++end;
    if (*end != '\0')
        invalid = 1;
    if (invalid) {
        fprintf(stderr, "%s: invalid sensor value\n", path);
        errno = EINVAL;
        return -1;
    }
    if (integer)
        *integer = count;
    else
        *real = number;
    return 0;
}

int sensor_read_all(sensor_snapshot_t *snapshot)
{
    if (!snapshot) {
        errno = EINVAL;
        return -1;
    }
    sensor_snapshot_t s = {0};
    if (read_number("mock_hwmon/cpu_temp", &s.cpu_temp_c, NULL, -40, 150) ||
        read_number("mock_hwmon/gpu_temp", &s.gpu_temp_c, NULL, -40, 150) ||
        read_number("mock_hwmon/fan_rpm", NULL, &s.fan_rpm, 0, 100000) ||
        read_number("mock_hwmon/psu_voltage", &s.psu_voltage_v, NULL, 0, 20))
        return -1;
    *snapshot = s;
    return 0;
}
