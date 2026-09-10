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
        printf 'demo: FAIL\n' >&2
        if [[ -f runtime/demo-daemon.log ]]; then cat runtime/demo-daemon.log >&2; fi
    fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p runtime
exec 9>runtime/monitor.lock
flock -n 9
owns_sensors=1
printf '\n[1/5] Build and unit tests\n'
make --no-print-directory clean all
make --no-print-directory test
./scripts/reset_mock.sh
flock -u 9
exec 9>&-
./bin/bmc-monitor >runtime/demo-daemon.log 2>&1 &
daemon_pid=$!
python3 scripts/wait_state.py OK NONE
kill -0 "$daemon_pid"
printf '\n[2/5] Normal telemetry\n'
python3 tools/bmcctl.py status
printf '\n[3/5] GPU fault and simulated recovery policy\n'
python3 tools/bmcctl.py inject gpu-overheat
python3 scripts/wait_state.py CRITICAL FAN_DEMAND_100_PERCENT
python3 tools/bmcctl.py status
python3 tools/bmcctl.py logs
printf '\n[4/5] Capture debugging evidence\n'
./scripts/collect_triage.sh
printf '\n[5/5] Recover and verify\n'
python3 tools/bmcctl.py recover
python3 scripts/wait_state.py OK NONE
python3 tools/bmcctl.py status
kill -TERM "$daemon_pid"
wait "$daemon_pid"
daemon_pid=""
printf '\ndemo: PASS\n'
