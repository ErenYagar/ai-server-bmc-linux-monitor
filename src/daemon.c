#define _POSIX_C_SOURCE 200809L
#include "monitor.h"
#include "sensor.h"
#include "event_log.h"
#include "state.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* Only the signal handler writes this flag; monitoring state is local to main. */
static volatile sig_atomic_t stopping = 0;
static void stop_handler(int signum)
{
    (void)signum;
    stopping = 1;
}

enum {
    CPU_WARN = 1, GPU_WARN = 2, GPU_HOT = 4,
    FAN_LOW = 8, PSU_LOW = 16, SENSOR_BAD = 32
};

static unsigned fault_mask(const sensor_snapshot_t *s, const thresholds_t *t)
{
    unsigned mask = 0;
    if (s->cpu_temp_c >= t->cpu_warn_c) mask |= CPU_WARN;
    if (s->gpu_temp_c >= t->gpu_crit_c) mask |= GPU_HOT;
    else if (s->gpu_temp_c >= t->gpu_warn_c) mask |= GPU_WARN;
    if (s->fan_rpm < t->fan_min_rpm) mask |= FAN_LOW;
    if (s->psu_voltage_v < t->psu_min_v) mask |= PSU_LOW;
    return mask;
}

static const char *recovery_policy(unsigned mask)
{
    /* One primary recommendation; all concurrent faults are logged. */
    if (mask & SENSOR_BAD) return "SENSOR_BUS_TRIAGE_REQUIRED";
    if (mask & FAN_LOW) return "HARDWARE_INSPECTION_REQUIRED";
    if (mask & PSU_LOW) return "PSU_TRIAGE_REQUIRED";
    if (mask & GPU_HOT) return "FAN_DEMAND_100_PERCENT";
    return "NONE";
}

static int log_faults(unsigned mask, const sensor_snapshot_t *s,
                      health_t health, const char *action, int first)
{
    if (!mask)
        return event_log("OK", first ? "MONITOR_STARTED" : "RECOVERED", 0, action);
    if (event_log(health_to_string(health), "FAULT_SET_CHANGED", 0, action))
        return -1;
    if ((mask & SENSOR_BAD) &&
        event_log("SENSOR_ERROR", "SENSOR_READ_ERROR", 0, action)) return -1;
    if ((mask & CPU_WARN) &&
        event_log("WARNING", "CPU_TEMPERATURE_HIGH", s->cpu_temp_c, action)) return -1;
    if ((mask & GPU_WARN) &&
        event_log("WARNING", "GPU_TEMPERATURE_HIGH", s->gpu_temp_c, action)) return -1;
    if ((mask & GPU_HOT) &&
        event_log("CRITICAL", "GPU_OVERHEAT", s->gpu_temp_c, action)) return -1;
    if ((mask & FAN_LOW) &&
        event_log("CRITICAL", "FAN_STALL", (double)s->fan_rpm, action)) return -1;
    if ((mask & PSU_LOW) &&
        event_log("CRITICAL", "PSU_UNDERVOLT", s->psu_voltage_v, action)) return -1;
    return 0;
}

int main(void)
{
    thresholds_t limits;
    if (thresholds_load("config/thresholds.conf", &limits)) {
        perror("load config/thresholds.conf (run from repository root)");
        return 1;
    }
    if (mkdir("runtime", 0755) != 0 && errno != EEXIST) {
        perror("create runtime");
        return 1;
    }
    int lock = open("runtime/monitor.lock", O_CREAT | O_WRONLY, 0600);
    if (lock < 0) {
        perror("open monitor lock");
        return 1;
    }
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) {
        perror("lock monitor (another daemon may be running)");
        close(lock);
        return 1;
    }
    struct sigaction sa = {0};
    sa.sa_handler = stop_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) {
        perror("install signal handlers");
        close(lock);
        return 1;
    }
    unsigned previous = UINT_MAX;
    int result = 0;
    fprintf(stderr, "bmc-monitor: started; interval=1s; recovery is simulated\n");
    while (!stopping) {
        sensor_snapshot_t sensors = {0};
        int valid = sensor_read_all(&sensors) == 0;
        health_t health = valid ? monitor_evaluate(&sensors, &limits) :
                                  HEALTH_SENSOR_ERROR;
        unsigned mask = valid ? fault_mask(&sensors, &limits) : SENSOR_BAD;
        const char *action = recovery_policy(mask);
        if (mask != previous) {
            fprintf(stderr, "bmc-monitor: health=%s action=%s\n",
                    health_to_string(health), action);
            if (log_faults(mask, &sensors, health, action, previous == UINT_MAX)) {
                perror("write runtime/events.log");
                result = 1;
                break;
            }
            previous = mask;
        }
        if (state_write(health, valid ? &sensors : NULL, action)) {
            perror("write runtime/state.json");
            result = 1;
            break;
        }
        if (!stopping)
            sleep(1);
    }
    if (close(lock) != 0) {
        perror("close monitor lock");
        result = 1;
    }
    fprintf(stderr, "bmc-monitor: stopped\n");
    return result;
}
