#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
daemon_pid=""
owns_sensors=0
cleanup() {
    local rc=$?
    if [[ -n "$daemon_pid" ]]; then
        if kill -0 "$daemon_pid" 2>/dev/null; then
            if ! kill -TERM "$daemon_pid"; then rc=1; fi
        fi
        if ! wait "$daemon_pid"; then rc=1; fi
    fi
    if (( owns_sensors )); then
        if ! ./scripts/reset_mock.sh; then rc=1; fi
    fi
    if (( rc != 0 )); then
        printf 'regression: FAIL; daemon diagnostics follow\n' >&2
        tail -n 30 runtime/regression-daemon.log >&2
    fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p runtime
# Lock before removing stale results, so an existing monitor is not disturbed.
exec 9>runtime/monitor.lock
flock -n 9
owns_sensors=1
./scripts/reset_mock.sh
rm -f -- runtime/state.json runtime/events.log
flock -u 9
exec 9>&-
./bin/bmc-monitor >runtime/regression-daemon.log 2>&1 &
daemon_pid=$!
wait_for() {
    python3 scripts/wait_state.py "$1" "$2"
    kill -0 "$daemon_pid"
}
wait_for OK NONE
python3 tools/bmcctl.py inject gpu-overheat
wait_for CRITICAL FAN_DEMAND_100_PERCENT
grep -q 'severity=CRITICAL event=GPU_OVERHEAT value=98.00' runtime/events.log
python3 tools/bmcctl.py recover
wait_for OK NONE
python3 tools/bmcctl.py inject fan-stall
wait_for CRITICAL HARDWARE_INSPECTION_REQUIRED
grep -q 'event=FAN_STALL' runtime/events.log
python3 tools/bmcctl.py inject sensor-missing
wait_for SENSOR_ERROR SENSOR_BUS_TRIAGE_REQUIRED
python3 tools/bmcctl.py recover
wait_for OK NONE
python3 tools/bmcctl.py inject psu-undervolt
wait_for CRITICAL PSU_TRIAGE_REQUIRED
grep -q 'event=PSU_UNDERVOLT' runtime/events.log
python3 tools/bmcctl.py recover
wait_for OK NONE
kill -TERM "$daemon_pid"
wait "$daemon_pid"
daemon_pid=""
python3 tests/test_integration.py
printf 'regression: PASS\n'
