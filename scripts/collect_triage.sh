#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
bundle="runtime/triage-$(date -u +%Y%m%d-%H%M%S)"
# Avoid overwriting a previous capture made in the same second.
if [[ -e "$bundle" ]]; then bundle="$bundle-$$"; fi
mkdir -p "$bundle"
{
    uname -a
    cat /etc/os-release
    gcc --version | sed -n '1p'
    make --version | sed -n '1p'
} > "$bundle/environment.txt"
{
    git rev-parse HEAD
    git status --short
} > "$bundle/git-revision.txt"
if [[ -f runtime/state.json ]]; then
    cp -- runtime/state.json "$bundle/sensor-state.json"
else
    printf '{"error":"No state available; start bmc-monitor"}\n' > "$bundle/sensor-state.json"
fi
if [[ -f runtime/events.log ]]; then
    tail -n 50 runtime/events.log > "$bundle/last-events.log"
else
    printf 'No event log available.\n' > "$bundle/last-events.log"
fi
cat > "$bundle/reproduction.txt" <<'EOF'
Run from the repository root with ./bin/bmc-monitor running.
Recover between independent faults. Allow one sampling interval.
python3 tools/bmcctl.py inject gpu-overheat
python3 tools/bmcctl.py inject fan-stall
python3 tools/bmcctl.py inject psu-undervolt
python3 tools/bmcctl.py inject sensor-missing
python3 tools/bmcctl.py recover

Expected: CRITICAL / FAN_DEMAND_100_PERCENT for GPU overheat;
CRITICAL / HARDWARE_INSPECTION_REQUIRED for fan stall;
CRITICAL / PSU_TRIAGE_REQUIRED for undervoltage;
SENSOR_ERROR / SENSOR_BUS_TRIAGE_REQUIRED for a missing sensor;
OK / NONE after recovery.
Recovery actions are simulated recommendations, not hardware operations.
See sensor-state.json and last-events.log for this capture's actual evidence.
EOF
printf 'Triage bundle: %s\n' "$bundle"
