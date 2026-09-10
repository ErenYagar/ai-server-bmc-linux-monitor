#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p mock_hwmon runtime
# Each file replacement is atomic; the set of four files is not a transaction.
write_sensor() {
    printf '%s\n' "$2" > "mock_hwmon/$1.tmp"
    mv -- "mock_hwmon/$1.tmp" "mock_hwmon/$1"
}
if [[ -e mock_hwmon/gpu_temp.missing ]]; then
    mv -- mock_hwmon/gpu_temp.missing mock_hwmon/gpu_temp
fi
write_sensor cpu_temp 55.0
write_sensor gpu_temp 65.0
write_sensor fan_rpm 3200
write_sensor psu_voltage 12.1
